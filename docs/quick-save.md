# Quick save and load

For testing, single player only. During gameplay F5 saves and F9 loads (`quick_save` / `quick_load` in `[keys]`); a
short message at the top of the picture reports the result. The keys do nothing in menus, on loading screens or in
co-op.

One slot, kept in memory and written in the background to `quicksave\MOHRS.qsv` beside the executable (about
36 MB). After a restart, start or continue any mission and press F9: the first load reads the file and replaces the
running mission. Only the same build can load a file; otherwise F9 says "Quick save is from elsewhere".

A quick save is not the game's save or checkpoint but a snapshot of the whole emulated machine: level, enemies,
scripts, timers and sounds. Saves take 25-52 ms, loads 12-46 ms, the background write 42-99 ms.

## How it works

Host call stacks cannot be saved (each guest thread runs on a host thread, with a host frame per guest call), so
both save and load happen at the entry of the gameplay frame function (`0xA4A90`, called once a frame from the
level loop at `0x128B9`). The main thread's host call chain is the same there every frame, and recompiled code
keeps every guest register in `X86Context`, so the host frames below hold only return addresses.

`snapshot::Save` and `snapshot::Load` (`sdk/runtime/app/snapshot.h`) first stop the world: every other guest thread
parks in a kernel wait, delay or yield; host threads that run guest code (vertical blank, DPCs, the DirectSound
callback pump) wait before their next guest call; host timers do not fire; the renderer is drained. A save or load
is refused if a thread does not park within 2 seconds. Each subsystem with state outside guest memory saves a part
(`sdk/runtime/core/snapshot.h`) that can be written to the file and read by a later run:

| Part | What it holds |
|---|---|
| `mem::SaveMemory` | All 64 MB of physical memory, every committed virtual page, the allocators and page commit map |
| `kernel::SaveObjects` | The handle table, dispatcher objects by guest address with their signal and owner, files with their positions, armed timers, queued DPCs |
| `dsound::SaveSound` | Every voice and stream with its queued packets and play position, the listener, completed packets' callbacks still due |
| `d3d::SaveDevice` | The device state, shaders, vertex program memory, push buffer position, the guest memory the device made for itself |
| `xapi::SavePads` | The controller handles the game opened |

A load restores the main thread's registers, all guest memory, the allocators, objects, voices and device state,
then reruns the frame from its start. Left as they are:

- Other guest threads' stacks and thread blocks: those threads are parked at host-level waits their stacks must
  match.
- The render thread's header copies in guest memory (`mem::KeepOnRestore`).
- Host-only caches: textures and surfaces are re-compared with their bytes (`gpu::MarkAllDirty`), the renderer gets
  the whole device state again (`Tracked::MarkAll`).

### Guest memory the host keeps

The host hands the game guest memory of its own: the menu packages it builds (`src/common/options_menu.cpp`: the
pause screen's ObjectivesOverlay, Notes, Preferences, PC Options and Cheats, the shell's), the strings it passes
(screen names, the Cheats rows' labels) and the added weapon bank's file name and size (`level_weapons.cpp`). A load
puts back the guest memory and the system allocator of the save, which may come from another instance (a save made
before the game relaunched into this one). A block the host allocated after that point is gone: its address is free
in the allocator or already another allocation's, and its bytes are the save's.

The pause screen packages are built the first time the game asks for them. In an instance that loaded a save from
the front end that is the first pause, after the load; a later load of the same save took the package, and the next
pause handed the game zeros. Building it failed (`0x92700` returned 0), the package loader (`0x92F40`) passed the
missing package to the shell's callback (`0x125EF0`, the loader object's vtable `0x1EF720` slot `+4`), and that read
`+0x40` of it: a crash on pausing.

Such blocks are `GuestBlock`s (`src/common/guest_block.h`). Each ends in 16 bytes this instance makes up and is used
again only while its address is still allocated with its size (`mem::AllocationSize`) and those bytes are still
there; otherwise a new block is allocated and filled from the host's copy (the old address is left to the save). The
menu log says "copied again, a quick load took the last copy" when that happens. Blocks allocated at the same point of
every boot, before any load can happen (the bank block from the heap's allocation, `HeapAlloc`), are in every save.

### Kernel objects and parked threads

Objects are saved as descriptions. A load writes each into the object the running game already has under the same
handle or guest address if type and file match; otherwise it recreates it (files reopened by disc entry or host
path, never truncated; events and semaphores new; an ended thread stays ended). Handle numbers are the save's,
because guest memory holds them.

Handle numbering depends on the run's history (menus first or a direct mission start), so a parked thread can wait
on a different object than the restored memory names. Each guest thread's wait therefore lists its objects, a save
records them per thread, and a load first maps the save's objects of each waiting thread onto the objects the same
thread waits on now, in order. Files from before this have no such records and load as before. An event or
semaphore that only a parked thread still waits on after a load, with no handle or guest address naming it, is set
once so the thread wakes and re-checks.

A thread busy at save or load time has stack and registers pointing into memory the load replaces. So a save also
records where each other guest thread is parked (stack pointer and code addresses on its stack), and a load waits,
retrying each frame for up to 5 seconds, until every thread is parked at the same place; then it fails with "Quick
load failed: the game stays busy". The resource loader's worker (`0x162E30`) keeps its table slot in a register
read once from `[0x399758]`; `LoaderSlot` (`src/game/hooks.cpp`, top of its loop at `0x162E70`) re-reads it so it
serves the restored table. `XBE_SNAPSHOT_TRACE` logs each parked thread's waits at a load.

Guest clocks (interrupt time, tick count, performance counter, `rdtsc`) run from `kernel::GuestClockNs`. A load
sets them back to the save, and armed timers keep their remaining time, so the 100 Hz sound thread does not replay
missed ticks.

### Timer events

The XDK's `timeSetEvent` (`0x1A7A94`) runs its events on a thread of its own (`0x1A78AC`), which keeps the table of
events on its stack (`[0x3A6BDC]`: 0x3F events of 0x44 bytes; kernel timer `+0`, callback `+0x28`, in use `+0x30`,
flags `+0x31` with bit 0 periodic, due interrupt time `+0x34`, period `+0x3C`, negative). When a periodic event
fires, the thread adds the period to its due time and sets the timer for due - now. A load leaves that stack as it
is but moves the guest clocks to the save's moment, so after a load of a save made 200 seconds earlier the next
event came 200 seconds late. The sound library's 100 Hz tick (`0x16E010`, made by `0x16E050`) is such an event; its
tick count (`0x39CABC`) drives the library's tasks (`0x16E670`), among them the sound bank load (`0x152E40`, polled
through `0x1589F0`). A death after such a load restarts the level (`0xA3B30`), which loads the checkpoint's sound
bank (`0x104F90`) and waits for it (`0x386FBC` = 1): the loading screen stayed up.

`snapshot::SetAfterRestore` (`sdk/runtime/app/snapshot.h`) runs once memory and the parts are back, before the other
threads go on, with how far the clocks moved. The game's hook (`src/common/time_events.{h,cpp}`) moves the due time
of every event in use by that much, so each is as far from now as at the save. The log says "quick load: N timer
events moved".

### Loading in a later run

Guest memory comes back whole, so the running mission does not matter. What must match:

- **The host call chain below the frame hook**, as module and offset per return address. A rebuilt executable
  changes it, which is how a file from another build is refused. `XBE_SNAPSHOT_ANY_BUILD=1` skips this check for
  probe builds of the same XBE.
- **Every guest and host thread's id, stack, TLS and thread blocks.** The game creates its threads identically at
  each boot. Thread blocks (KPCR and ETHREAD) have a fixed slot by thread id at the top of system memory
  (`mem::AllocThreadBlocks`); files from before the fixed slots do not load. A refused load logs each differing
  thread.
- **The host's own guest memory**: the render thread's header ring and the sound effects image's state.

Guest memory the host allocates later, on first use (the vertical blank callback's block, the DrawPrimitiveUP ring,
SwitchTexture's headers), is part of the device's saved state and comes back at the save's address. The file starts
with the XBE's SHA-1, and each part checks its structure sizes; a mismatch refuses the load before anything changes,
with a log line saying why.

## Status

Verified: save and reload within a 1_3 run, and loading that file in new processes started on 1_3 and on 1_1; a
rebuilt executable refuses the old file. Not tested: loading in combat, across a checkpoint, with music streaming,
after a death, or in a run that went through the menus before its first mission.

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/time_events_test.cpp src/common/time_events.cpp -o re/time_events_test.exe"
& re/time_events_test.exe
```

A stall of 50 ms or more can shift the emulated vertical blank against the host frame cap, giving alternating 13/25 ms
intervals at `fps_cap = 60` with vsync off. `fps_cap = 240` shows the same pattern without any snapshot.

```powershell
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_3 -Seconds 30 -QuickSave "15:save,22:load"
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_1 -Seconds 35 -QuickSave "8:load" -QuickSaveFrom <first run's folder>
```

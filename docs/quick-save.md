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

A stall of 50 ms or more can shift the emulated vertical blank against the host frame cap, giving alternating 13/25 ms
intervals at `fps_cap = 60` with vsync off. `fps_cap = 240` shows the same pattern without any snapshot.

```powershell
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_3 -Seconds 30 -QuickSave "15:save,22:load"
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_1 -Seconds 35 -QuickSave "8:load" -QuickSaveFrom <first run's folder>
```

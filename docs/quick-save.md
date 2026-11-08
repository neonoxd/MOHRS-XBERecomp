# Quick save and load

For testing, single player only. During gameplay F5 saves and F9 loads. A short message at the top of the
picture says what happened. The keys can be rebound as `quick_save` and `quick_load` in the `[keys]` table of
`settings.toml`. F5 and F9 do nothing in menus, on loading screens or in co-op.

There is one slot. It is kept in memory and also written in the background to `quicksave\MOHRS.qsv` beside the
executable, about 36 MB, which the next F5 replaces. After a restart, start or continue any mission, then press F9: the
first load reads the file. It does not matter which mission is running, as the load replaces it. Only the same build
of the executable can load a file: after a rebuild F9 says "Quick save is from elsewhere".

A quick save is not the game's own save or checkpoint. It is a snapshot of the whole emulated machine, and a load
puts it all back: the level, enemies, scripts, timers and sounds as they were.

## How it works

The host's call stacks cannot be saved: every guest thread runs on a host thread, and the recompiled code
nests a host frame for each guest call. So both the save and the load happen at the entry of the gameplay frame
function (`0xA4A90`, called once a frame from the level's loop at `0x128B9`). The main thread's host call chain is
the same there every frame, and recompiled code keeps every guest register in `X86Context` (flags are recomputed
after each call), so the host frames below only hold return addresses.

`snapshot::Save` and `snapshot::Load` (`sdk/runtime/app/snapshot.h`) first stop the world. Every other guest
thread parks in a kernel wait, delay or yield. While stopped, it takes no object and returns to no guest code. Host
threads that run guest code (vertical blank, DPCs, the DirectSound callback pump) wait before their next call into
guest code, and host timers do not fire. The renderer is drained, so the render thread is no longer reading guest
memory. Each subsystem with state outside guest memory saves a part of it (`sdk/runtime/core/snapshot.h`). Each
part can write itself to the file and be read back by a later run:

| Part | What it holds |
|---|---|
| `mem::SaveMemory` | All 64 MB of physical memory, every committed virtual page, the allocators and page commit map |
| `kernel::SaveObjects` | The handle table, dispatcher objects by guest address with their signal and owner, files with their positions, armed timers, queued DPCs |
| `dsound::SaveSound` | Every voice and stream with its queued packets and play position, the listener, completed packets' callbacks still due |
| `d3d::SaveDevice` | The device state, shaders, vertex program memory, push buffer position, the guest memory the device made for itself |
| `xapi::SavePads` | The controller handles the game opened |

A load puts the main thread's registers back, then every byte of guest memory, the allocators, objects, voices
and device state. The frame then runs again from its start. Three things are left as they are:

- **Other guest threads' stacks and thread blocks.** Those threads are parked at host-level waits, and their stacks
  must match. Objects inside them keep their present state.
- **The render thread's header copies in guest memory** (`mem::KeepOnRestore`).
- **Host-only caches.** Textures and surfaces are all compared with their bytes again (`gpu::MarkAllDirty`). The
  renderer is sent the whole device state again (`Tracked::MarkAll`).

Kernel objects are saved as descriptions. A load puts each one into the object the running game already has under
the same handle or guest address, if its type and file match, so a thread parked on an event keeps waiting on the
object that will be signalled. Otherwise the load makes the object again: files are reopened by their disc entry or
host path (never truncated), events and semaphores are made new, and a thread that had ended stays ended. The
handle numbers are the save's, because guest memory holds them.

Handle numbers depend on what a run did before: a run that went through the menus numbers its objects differently
from one that started a mission directly. A thread parked in a wait stays on this run's object, while the memory put
back names the save's. In 1_1 that left the resource loader's worker (`0x162E30`) asleep on an event nobody set any
more, and the end of the mission waited for its file job forever (`0x163580`). So each guest thread's wait now lists
the objects it waits on, a save records them per thread, and a load first gives the save's objects of each waiting
thread to the objects the same thread waits on in this run, in the same order; handles and guest addresses match the
rest. Files saved before this have no such records and load as before.

Matching objects is not enough when a thread was busy at the save or at the load: its kept stack and registers then
point into memory the load replaces. At the end of 1_1 the loader worker was blocked entering a lock inside its own
table, which the saving run had allocated at another address, and never got in. So a save also records where each
other guest thread is parked (its stack pointer and the code addresses on its stack), and a load first checks that
each is parked at the same place. If not, it lets the game run and tries again each frame for up to 5 seconds, then
says "Quick load failed: the game stays busy". The loader worker also keeps its table slot in a register, taken from
`[0x399758]` once at its start; a hook at the top of its loop (`0x162E70`, `LoaderSlot` in `src/game/hooks.cpp`)
re-reads it, so after a load it serves the table that was put back. An event or semaphore that only a parked thread
still waits on after a load, with no handle or guest address naming it, is set once so that thread wakes and looks
again. `XBE_SNAPSHOT_TRACE` logs each parked thread's waits at a load.

The guest clocks (interrupt time, tick count, performance counter, `rdtsc`) run from `kernel::GuestClockNs`. A
load sets them back to the moment of the save, and armed timers stay as far from expiring as they were. Without
this, the game's 100 Hz sound thread would replay every tick missed since the save.

### Loading in a later run

Guest memory is put back whole, so the running mission does not matter. What must match is what a load leaves alone
or cannot move:

- **The host call chain below the frame hook**, as each return address's module and offset. A rebuilt executable
  changes it, which is how a file from another build is refused.
- **Every guest and host thread's id, stack, TLS and thread blocks.** The game makes its threads the same way at
  every boot, so they land at the same addresses. Each thread's blocks (KPCR and ETHREAD) have a fixed slot by
  thread id at the top of system memory (`mem::AllocThreadBlocks`). They used to come from the system heap, where
  one earlier allocation more or less moved a later thread's blocks: a save made late in 1_1 did not load at the
  start of a mission because thread 30's were 0x40 bytes apart. So files saved before the fixed slots do not
  load. A refused load logs each thread that differs.
- **The host's own guest memory**: the render thread's header ring and the sound effects image's state.

Guest memory the host allocates later, on first use, is part of the device's saved state instead, so it comes back
at the save's address with the save's allocators. That is the vertical blank callback's block, the DrawPrimitiveUP
ring and SwitchTexture's headers. The file starts with the XBE's SHA-1, and each part checks the size of its
structures. A load that does not fit is refused before anything changes, with a log line saying why.
For testing, `XBE_SNAPSHOT_ANY_BUILD=1` skips the call chain comparison, so a probe build can load a save made by
another build of the same XBE. The main thread is at the same point of the same function chain there; the thread
layout and every other check still apply.

A save or load is refused if a thread does not park within 2 seconds. Each refusal shows a message and writes a log
line.

## Tested

In a probe of 1_3 at 60 FPS, with no input, the game saved at 19 s and loaded at 26 s. Saves took 25-52 ms, loads
12-46 ms, and writing the 36 MB file took 42-99 ms in the background. After each load the mission ticks went back to
the save's (about 907) and the player to the saved position on the boat. The scene then played out again as it had
the first time.

New processes loaded that file:

- **Starting 1_3:** at its own tick 435, a load took it to tick 902 at the saved position. It ran on past where the
  saving run stopped, then loaded the save again.
- **Starting 1_1:** while playing the start of 1_1, a load took it into 1_3 at the saved position, playing on at
  60 FPS.

Neither run logged errors. After a rebuild, the old file was refused because of the call chain. The co-op
executable boots and runs as before.

A stall of 50 ms or more can shift the emulated vertical blank against the host's frame cap. With `fps_cap = 60`
and vsync off, pacing then alternated between 13 and 25 ms intervals, still averaging 60 FPS. With `fps_cap = 240`,
pacing showed that pattern from the start without any save, so it is not state left by the snapshot.

Not tested yet: a load in combat, across a checkpoint, with music streaming, after a death, or in a run that went
through the menus before its first mission.

```powershell
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_3 -Seconds 30 -QuickSave "15:save,22:load"
powershell -NoProfile -File tools/fps_probe.ps1 -Build profile -Cap 60 -Level 1_1 -Seconds 35 -QuickSave "8:load" -QuickSaveFrom <first run's folder>
```

`-QuickSave` sets `MOHRS_QUICKSAVE_PROBE`: steps in seconds after the first gameplay frame. `-QuickSaveFrom` copies
an earlier run's `quicksave` folder into the new one.

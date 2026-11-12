# Debugging and test runs

## Crash dumps

A crash on a guest thread writes, beside the executable:

- **The log** (`logs\mohrs.log`): exception and address, the host call stack with each frame named, and the
  crashed guest thread's registers and the code addresses on its stack. Game code frames are named by the DLL's
  exports, e.g. `MOHRS.dll!__imp__sub_001A9D78+0x54A (game code 001A9D78)`, without symbols; host frames come from
  the executable's PDB.
- **The dump** (`CrashDumps\MOHRS.<date>_<time>.dmp`, about 100 MB): host threads and stacks, every committed guest
  page and the 64 MB of guest physical memory at the guest base (`0x100000000` unless taken). Its comment stream
  gives the base and the crashed thread: `guest base 0000000100000000; guest thread 14, X86Context at ...`.
- **Symbols**: `MOHRS.pdb` for the executable and `MOHRS_code.pdb` for the game code, with line tables naming lines
  of `generated/game/x86_recomp.<n>.cpp`, whose comments hold the guest instructions. Releases compile the game code
  on the player's PC (`runtime/app/game_code.cpp`) with the same line tables.

`tools/crash_report.ps1` turns a dump into a report with cdb (Windows SDK Debugging Tools):

```powershell
powershell -NoProfile -File tools/crash_report.ps1 out\build\release\CrashDumps\MOHRS.<time>.dmp -Guest 83EA2A10:40
```

It prints the host stack with game functions and lines, the guest thread's registers and stack, and the requested
guest memory (`address:bytes`, hex). Symbols must match the executable and DLL: by default the folder holding
`CrashDumps`, else `-Symbols <folder>`. In cdb, guest address `A` is at `base + A`. Dumps from before 2026-11-08
hold no guest memory or game-code symbols. The probe pad word `crash` crashes on purpose to test this.

## Probe scripts

Probes run a copy of the release build in its own folder with fresh settings and saves, never send OS input, and
stop only their own processes. Ask before running them while the user may be using the PC.

`tools/fps_probe.ps1` (folders under `re/fps-probes`; runs reuse one `run` folder so the volume mixer keeps one
entry, and each run's logs and settings are copied to its own results folder):

| Parameter | Effect |
|---|---|
| `-Cap`, `-Unlocked`, `-Fps30` | `fps_cap`, unlocked mode, original 30 FPS mode (default 60 FPS) |
| `-Level 1_3`, `-Seconds`, `-Coop` | start level (`MOHRS_START_LEVEL`), run length, co-op executable |
| `-Width`, `-Height`, `-RenderPercent`, `-Fov` | output size, render scale, field of view |
| `-NoPerf` | no `XBE_PERF` renderer instrumentation |
| `-Build <folder>` | run another build folder, e.g. `profile` with `-gline-tables-only` |
| `-Visible` | show the window (for `tools/screenshot.ps1`) |
| `-Sound` | not muted (`XBE_MUTE` otherwise) |
| `-Pad "9/6:forward,12:a"` | virtual pad inside the game: word at seconds after the first poll, held for the time after `/` (0.1 s default). Words: `forward`, `backward`, `strafeleft`, `straferight`, `lookleft`, `lookright`, `jump`, `crouch`, the menu buttons, `crash` |
| `-QuickSave "15:save,22:load,30:capture"` | `MOHRS_QUICKSAVE_PROBE`: steps in seconds after the first gameplay frame |
| `-QuickSaveFrom <folder>`, `-AnyBuild` | copy an earlier run's `quicksave` folder; load a save from another build (`XBE_SNAPSHOT_ANY_BUILD`) |
| `-Report 0.25` | timing report interval (`MOHRS_TIMING`) |

`tools/menu_probe.ps1` (folders under `re/menu-probes`) runs `MOHRS_MENU_PROBE`, which presses controller buttons
inside the game at given times, with `capture` saving frames to the folder's `captures\`:

```powershell
powershell -NoProfile -File tools/menu_probe.ps1 -Level 1_1 -Script "8:start,10:right,11.5:right,13:capture" -Seconds 16
```

`-CopyProfiles` copies the save partition into the probe folder to get past profile creation (never written back);
`-Rate 30|60|unlocked`, `-Cap`, `-Coop` and `-EventTrace` are also available.

Other scripts: `tools/start_level.ps1`, `tools/level_tour.ps1`, `tools/screenshot.ps1`, `tools/run_test.ps1`.

## Hitch recorder

`XBE_HITCH` (`sdk/runtime/core/hitch.cpp`) keeps the last 32,768 timed host events in a ring:

| Event | Where | Recorded |
|---|---|---|
| shader | `D3D12Backend::Compile` (`D3DCompile` on a cache miss), profile, hash, HLSL size | always |
| pipeline | `EnsurePipeline` (`CreateGraphicsPipelineState` on a cache miss) | always |
| texture, target | `CreateTexture`, `EnsureTarget` making a resource | always |
| upload | texture bytes converted and uploaded | 0.2 ms or longer |
| buffer | an upload too big for the frame's ring, given its own buffer | always |
| open, read | `NtCreateFile`/`NtOpenFile`, `NtReadFile`/`NtWriteFile` (and scatter/gather), with path, size and offset | always |
| title wait | the title waiting for the render thread, with the cause | 0.5 ms or longer |
| swap, gpu wait, read-back | swap chain `Present`, frame fence wait, GPU pixels brought back | 1 ms, 1 ms, 0.5 ms |
| frame, title frame | present to present on the render thread and on the title | always |
| stack | each guest thread's stack: esp and the code addresses on it, `[wait]` inside a kernel wait | while a present is overdue |
| precompile | a vertex program compiled (or read from the disk cache) on a background thread | always |

A present later than the threshold logs a line in `logs\mohrs.log`: the frame's length, the totals by kind, then
its 12 longest events (time from the frame's start, thread, kind, length, detail) and the first stack sample of the
threads not waiting. Without a number the threshold is the largest of 10 ms, 1.75 times the median of the last 256
frames and the median plus 5 ms. The first 64 frames are not checked; past four slow frames in a second only the
summary line is logged, past 20 nothing. A sampler thread checks every 2 ms and samples the stacks up to six times
per overdue present, at least half the threshold (8 ms minimum) apart.

F8 samples the stacks, writes `logs\hitch_<date>_<time>.txt` and beeps. The file has every slow frame of the last
6 s described as in the log, every event by start time (seconds before the press) and every frame and title frame
interval. Times in both are seconds since the recorder started.

```powershell
$env:XBE_HITCH = "1"; & outuild
elease\MOHRS.exe
```

`tests/hitch_test.cpp` runs the recorder with a faked kernel.

## Threads that ran one at a time on the Xbox

Guest threads run on host threads at once, where the Xbox's one CPU switched between them. Code that relied on no
switch landing in a short window races here; these run under a lock:

| | default.xbe | moh3rdc.xbe | |
|---|---:|---:|---|
| EA stream packet take, under its stream's lock (`src/common/stream_lock.cpp`) | `0x165390` | `0x15E760` | read a stream's count before its packet; crashed opening a level's intro movie |
| Small-block pool search and free (operator new and MEM_alloc up to 0x40 bytes), under one host lock (`src/common/pool_lock.cpp`) | `0x111BE0`, `0x111C30` | `0x10A760`, `0x10A7B0` | a free bit tested and cleared with no lock: a level loading during its intro movie got a block another thread held (crash in a skeleton's bone map, `0x72410`) |

## Environment variables

| Variable | Effect |
|---|---|
| `XBE_PERF=1` | per-second renderer timings and Present interval percentiles in release builds |
| `XBE_HITCH=1` (or `=<ms>`) | the [hitch recorder](#hitch-recorder): slow frames logged with what ran in them, F8 writes the last 6 s; a number of 2 or more is the slow frame threshold in ms |
| `XBE_SHADER_CACHE=0` | no [disk shader cache](performance.md#shader-cache): every shader compiled, nothing written |
| `XBE_SHADER_PRECOMPILE=0` | vertex programs compiled at their first draw only, not when created |
| `XBE_SHADER_OPT=0`..`3` or `skip` | `D3DCompile` optimization level (default 1); the cache keeps each setting in its own folder |
| `XBE_SHADER_SOURCES=<folder>` | each shader's HLSL, once a run, as `<hash>_<entry>_<profile>.hlsl` (for `tests/shader_compile_bench.cpp`) |
| `XBE_LOG_INFO` | release builds also log info and warnings |
| `XBE_MUTE` | no sound output |
| `XBE_CAPTURE_DIR` | folder for F3 frame captures |
| `XBE_D3D12_DEBUG` | D3D12 debug layer |
| `XBE_DISC_OVERRIDE=<folder>` | a file under the folder replaces the disc's file at the same path |
| `XBE_SNAPSHOT_TRACE` | each parked thread's waits at a quick load |
| `XBE_SNAPSHOT_ANY_BUILD=1` | quick load skips the call chain check |
| `MOHRS_START_LEVEL` | start this mission_level instead of 1_1 |
| `MOHRS_CHEATS` | the pause Cheats switches, hex bit mask (1 Give All Weapons, 2 Infinite Ammo, 4 God Mode, 8 One Shot Kills, 10 Bullet Shield, 20 Achilles Head, 40 Invisible Soldiers, 80 Rubber Grenades); set by the game for the instance it relaunches into |
| `MOHRS_CHEATS_AT=15:1` | sets the cheat switches (hex mask) that many seconds after the first gameplay frame, as the pause menu would |
| `MOHRS_LEVEL_WEAPONS=0` | campaign levels as they are on the disc (no weapons from other levels) |
| `MOHRS_HEAP_EXTRA_MB` | the main heap's extra megabytes (default up to 12, 0 none) |
| `MOHRS_SOUND_TRACE` | logs every sound event lookup (`0x108370`): its hash and the result, -1 when the level has no such event |
| `MOHRS_WEAPON_CYCLE` | presses the weapon switch for player 0 every 2.5 s from 8 s on (with `-Pad` `fire` words: every weapon switched to and fired) |
| `MOHRS_TIMING=1` (or `0.1` for ten reports a second) | frame rate, ticks, look updates, mission ticks, player position |
| `MOHRS_QUICKSAVE_PROBE`, `MOHRS_MENU_PROBE` | scripted quick save/load and controller presses (set by the probe scripts) |
| `MOHRS_MENU_MOUSE_PROBE="8:Preferences text,10:Audio button"` | hover/click named rendered objects, timed from the first FEng input update |
| `MOHRS_MENU_MOUSE_TRACE=1` | packages, rectangles and clicks of the menu mouse adapter |
| `MOHRS_MENU_TRACE` | every screen the game makes, by package name |
| `MOHRS_FOV_TRACE=1` | camera angles before and after the FoV adjustment |
| `MOHRS_NEAR_PLANE_ORIGINAL=1` | keep the player camera's near plane at 0.4 when the view is widened (walls clip at the sides) |
| `MOHRS_EVENT_TRACE=1` | every animation event handled, with the guest call chain |
| `MOHRS_ANIM_TRACE=<event tables>` or `all` | animation channels playing them and who starts them |
| `MOHRS_GRAPH_TRACE` | animation graph state requests (`0x6A120`, `0xCE030`) |
| `MOHRS_NATIVE_TRACE=find\|<owner>` | a script's native calls |
| `MOHRS_MESSAGE_TRACE` (or `=0x28` for one message) | posted script messages (`0x4ADB0`) |
| `MOHRS_ENTITY_DUMP=<seconds>[:<radius>]` | entities near the player, once; `MOHRS_DUMP=<addr>:<bytes>` adds a memory dump |
| `MOHRS_CONTACT_TRACE` | objects the player's contact handler (`0xDEFA0`) sees |
| `MOHRS_STEP_TRACE=x,y,radius` | movement steps (`0xE0E50`) and collision resolves (`0xD8260`) near a point |
| `MOHRS_WATCH=<from>-<to>:<addr>:<bytes>,...` | each dword's summed changes over a window; `MOHRS_SERIES=<addr>:<n>,...` prints floats every frame of it |
| `MOHRS_HANG_TRACE=<seconds>` | every guest thread's stack pointer and stack code addresses, periodically; `MOHRS_HANG_HANDLES=<hex>,...` adds object types and signal state, `MOHRS_HANG_DUMP=[*]<addr>:<bytes>,...` memory |
| `MOHRS_*_EVERY_FRAME` (`SCRIPT`, `SHAKE`, `PARTICLE_DRAG`, `PUFF`, `BOT`) | old per-frame behaviour of a [frame timing](frame-timing.md) fix |

`MOHRS_CONTACT_TRACE` and `MOHRS_STEP_TRACE` override game functions, so the DLL must be relinked after the
executable for them to take effect.

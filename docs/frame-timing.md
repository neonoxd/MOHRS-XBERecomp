# Frame timing and FPS above 60

The optional `unlocked_fps` setting removes the game's vertical-blank wait and supplies elapsed time to its
existing floating-point update path. It is experimental and defaults to false. Both single-player and co-op
have the hooks. The existing 30/60 FPS modes remain available.

In `settings.toml` beside the executable, before any `[keys]` table:

```toml
unlocked_fps = true
fps_cap = 144
vsync = false
fps_counter = true
```

`fps_cap = 0` removes the host cap; `1` uses the monitor's refresh rate minus three. With vsync enabled the
display can still limit presentation. `unlocked_fps` takes precedence over `fps60`; when it is false,
`fps60 = true` gives the tested 60 FPS patch and `fps60 = false` restores the original two-blank wait.

## Executable evidence

The engine attribution is unverified. No Unreal-specific setting is used by this implementation. The local XBE
has no readable strings matching Unreal/UObject, MaxFPS or DeltaTime; this alone does not identify its engine.

The handover's earlier inference that interpolation is required was too strong. There is a float delta path:

| Operation | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Frame end, returns whole vertical blanks passed, minimum 1 | 0x13A90 | 0x13AE0 |
| Wait compares against 2, EAX / ECX | 0x13AAF / 0x13ACA | 0x13AFF / 0x13B1A |
| Main gameplay frame | 0xA4A90 | 0x98A40 |
| Global float frame delta | 0x1FF528 | 0x1F7648 |
| PAL/NTSC delta multiplier, initially 1 | 0x1FC678 | 0x1F54D8 |
| Player look update, float delta at EBP+8 | 0xE0A50 | 0xD8980 |
| Integer mission timer conversion, hook before PUSH EAX | 0xA4C32 | 0x98C88 |
| Integer UI delta conversion, hook before PUSH EAX | 0xA4CEC | 0x98D94 |
| Entity delayed-message delta, hook before SUBSS XMM0,XMM3 | 0xFABB4 | 0xF3684 |

There are nine callers of frame end in each image. Each converts EAX with `cvtsi2ss xmm0,eax`, multiplies it by
the saved PAL/NTSC scale and stores the global float delta. `FrameDelta` runs **after the conversion**, on all
nine paths (gameplay, loading, front end and shutdown). It replaces XMM0 with steady-clock elapsed seconds
times 60. The original scale still applies. The first delta is one tick, and a stall is capped at six ticks,
matching the entity manager's existing maximum; discarded stall time does not leave a catch-up backlog.

`FrameWait` lets zero blanks pass in unlocked mode. The SDK still generates normal 60 Hz vertical blank
callbacks, so console time and other users of that signal are unchanged. Present still uses the SDK's existing
`fps_cap`/vsync controls. No SDK changes are needed.

The main update already passes float deltas to entities, animations, camera, fades and player turning. Its entity
manager subdivides deltas above one tick. Three conversions needed explicit treatment for deltas below one:

- Mission elapsed time is stored as an integer. Accumulate fractional ticks before handing it whole ticks.
- The UI/frontend update also accepts whole ticks. Give it a separate remainder in its own scaled units.
- Delayed entity messages store float countdowns but truncate their update delta to an integer and back to a
  float. Use the original float delta in XMM2 instead of the truncated copy in XMM3, so messages still expire.

Each remainder is advanced once at its original conversion site, rather than once per entity or player.
Render frame counters are left alone: some readers use their parity to distribute work across frames.

## Verification

```powershell
python tools/timing_audit.py
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/frame_clock_test.cpp -o re/frame_clock_test.exe"
& re/frame_clock_test.exe
cmd /c "tools\dev.bat cmake --build out\build\release -- -k 0 -j 12"
```

The audit checks all nine frame-end callers and the wait/timer hook instructions against both supplied XBEs.
The standalone clock tests check ten seconds at 30, 60, 120, 144, 240 and 1,000 FPS, conservation of integer
timer fractions, zero/negative timer deltas, and stall recovery.

`MOHRS_TIMING=1` logs frame rate, time advanced, player look-update rate, player time advanced, mission elapsed
ticks and player position once per second. A stable 144 FPS run should report about 60 frame/player ticks per
second. During a scripted sequence the look update can be inactive and the mission timer paused.

`tools/fps_probe.ps1` creates an isolated run folder under `re/fps-probes`, copies the release executables/DLLs
and uses fresh settings/saves. It never sends OS input and only stops processes from that run folder. Ask before
running it while the user may be using the PC. For example:

```powershell
& tools/fps_probe.ps1 -Cap 60 -Seconds 18
& tools/fps_probe.ps1 -Cap 120 -Unlocked -Seconds 18
& tools/fps_probe.ps1 -Cap 144 -Unlocked -Seconds 18
```

2026-10-26 release smoke results, level 1_1, no injected input: stable 120 and 144 FPS, approximately
60.000 player ticks/s at both rates. The mission timer advanced by about 60 ticks/s; the opening scripted
movement reached the same final player position as the 60 FPS baseline. No errors or crashes in these runs.
An uncapped level 1_4 run reached roughly 570-725 FPS with about 60 player ticks/s and an advancing mission
timer. The co-op executable on 1_4 and the front-end level 1_16 both reached 144 FPS without logged errors.
These checks use windowed 1280x720 output and say nothing about performance at other resolutions.

These are timing and boot checks. Movement distance, weapon cadence, physics, mounted guns, pause/resume,
cutscene synchronization and full two-controller co-op still need interactive play-testing. Frame-dependent
behavior elsewhere in the game may need further hooks; this is why the mode is opt-in.

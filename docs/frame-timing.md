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
| Loading animation initialization, resets phase | 0xFC9FD | 0xF550D |
| Loading animation float phase, hook after CVTSI2SS | 0xFD3DC | 0xF5EEC |
| Profile/save popup animation initialization | 0xF6B74 | 0xEF594 |
| Profile/save popup float phase, hook after CVTSI2SS | 0xF6C5A | 0xEF67A |
| Animation event scan | 0x6FA30 | 0x62470 |
| Its loop wrap branch, hook jumping to the forward scan | 0x6FB74 -> 0x6FB0D | 0x625B4 -> 0x6254D |

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

The loading-screen stars/medal have a separate draw counter at loading-object offset `+0x10`. The loading
renderer (`0xFD280`, co-op `0xF5D90`) converts it to a float, increments it every draw and wraps at 33. It
multiplies the float by `2*pi/33` before its sine/cosine calculations. This animation therefore speeds up with
the render rate even though gameplay receives the correct time delta. In the 60 FPS and unlocked modes,
`LoadingAnimation` supplies a continuous phase at 30 steps/second, preserving the console's 1.1-second cycle.
Texture initialization resets the host animation clock, and the first draw starts at phase zero. The original
integer counter and all draw calls remain intact. The original 30 FPS mode uses its unchanged guest phase.

Animation events (dialogue lines, opcode `0xCAF0`, sounds, effects) are fired by the scan at `0x6FA30` (co-op
`0x62470`) to the entity's listener (vtable `0x1E84F0`, handler `0xCF780`). It turns the previous and current
animation times into whole frames at 30 a second (`ebx`, `edx`) and keeps a started flag at `+0xE4`. Previous
below current fires the events between them, and on the first scan every event up to the current frame. Previous
at or above current is taken as a loop wrap, and with the flag clear that branch fires every event of the
animation. At 30 FPS a first scan always moves at least one frame on. At 60 FPS and unlocked it can stay on frame
0, so every event of a new animation fired at once and then again at its own time: dialogue lines were heard
twice. `AnimEventFirstScan` sends equal frames, which only reach that branch on a first scan, to the forward scan
instead. `MOHRS_EVENT_TRACE=1` logs every event the listener handles with the guest call chain. In 1_1's first 40
seconds the 60 FPS run handled 256 events before the fix and 135 after it, against 138 at 30 FPS and 134 unlocked
at 144 FPS. The remaining differences are a repeating event on looping animations, within one per listener, and
depend on where each run stopped.

Profile checking and save-game prompts share another medal/star renderer (`0xF6C30`, co-op `0xEF650`),
used by the `MemoryCardPopup.fng` hierarchy. It loads the same `data\loading\medal.xsh` texture and uses the
same 33-frame cycle and angle constant, with its counter at object `+0x14`. `PopupAnimation` supplies the
same elapsed-time phase with a separate clock; popup initialization resets it. The modal state, input, profile
checking, save operations and prompt lifetime continue through the original game code.

## Verification

```powershell
python tools/timing_audit.py
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/frame_clock_test.cpp -o re/frame_clock_test.exe"
& re/frame_clock_test.exe
cmd /c "tools\dev.bat cmake --build out\build\release -- -k 0 -j 12"
```

The audit checks all nine frame-end callers and the wait/timer/loading-animation hook instructions against both supplied XBEs.
The standalone clock tests check ten seconds at 30, 60, 120, 144, 240 and 1,000 FPS, conservation of integer
timer fractions, zero/negative timer deltas, and stall recovery.
Loading-animation checks compare phase after five seconds at each frame rate, verify the 1.1-second period,
ensure repeated draws at the same time leave the phase unchanged, and check reset on a new loading screen.

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

Baseline commit: `2866645`. The user reports that almost everything looks and feels correct and has visually
confirmed both cosmetic timing fixes: mission-loading stars and the shared profile-check/save-prompt stars.
Both release images build, the clock/animation tests and XBE hook audit pass, and isolated 240 FPS level 1_4
and front-end boots pass. Those input-free boots do not exercise the animated prompts themselves; their visual
verification came from the user's interactive tests.

These are timing and boot checks. Movement distance, weapon cadence, physics, mounted guns, pause/resume,
cutscene synchronization and full two-controller co-op still need interactive play-testing. Frame-dependent
behavior elsewhere in the game may need further hooks; this is why the mode is opt-in.

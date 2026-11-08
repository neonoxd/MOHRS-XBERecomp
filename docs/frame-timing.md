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

`fps_cap = 0` removes the host cap down to 2400 FPS, the most any cap allows (`app::kMaxFps`); `1` uses the
monitor's refresh rate minus three. Above 3,840 FPS a frame is shorter than the entity manager's smallest physics
step (1/64 tick, `0xFAB00`), which then only runs zero-length steps; the 2400 ceiling keeps every run below it. With vsync enabled the
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
| In-game menu units (0x11DD20 / 0x118150), hook before PUSH EAX | 0x11DDCB | 0x11823D |
| In-game menu modal loop units (end-of-mission screens), hook before PUSH EAX | 0x11DC48 | 0x118078 |
| Save/profile popup units (0xF68A0 / 0xEF2C0), hook before PUSH EAX | 0xF68D6 | 0xEF2F6 |
| Entity delayed-message delta, hook before SUBSS XMM0,XMM3 | 0xFABB4 | 0xF3684 |
| Loading animation initialization, resets phase | 0xFC9FD | 0xF550D |
| Loading animation float phase, hook after CVTSI2SS | 0xFD3DC | 0xF5EEC |
| Profile/save popup animation initialization | 0xF6B74 | 0xEF594 |
| Profile/save popup float phase, hook after CVTSI2SS | 0xF6C5A | 0xEF67A |
| Soldier brain goal ticks, hook after CVTTSS2SI | 0x197C6 | 0x19056 |
| Threat memory decay, hook after the 0.01 load | 0x223A8 | 0x21F58 |
| Player floor ray: collision world before the cast | 0xDAC19 | 0xD1581 |
| Player floor ray result, hook at cmp edi, 1 | 0xDAC2A | 0xD1592 |
| Script system frame update, function override | 0x4C0F0 | 0x4F220 |
| Entity manager update, function override | 0xFAB00 | 0xF35D0 |
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
- In-game menus (post-mission stats and save screens, `0x11DD20`) and the save/profile popup (`0xF68A0`, vtable
  `0x1EA340` beside its star renderer) convert the delta to FEng units the same way, `(int)(dt * 20)`, 1,200 a
  second. That is 4% slow at 144 FPS, 17% at 1,000 and zero above 1,200 FPS: highlights stopped blinking, and
  transitions and button responses waited for an occasional long frame. The in-game menu feeds the same FEng
  manager as the front end and shares its remainder (`MenuTicks`); the popup has its own (`PopupTicks`).
  The end-of-mission screens run in the in-game menu's modal loop instead (from `0x11DA95`, ending each pass in
  `0xA2E20`), which converts the global frame delta at `0x11DC42` (co-op `0x118072`); the
  first fix missed it, so above 1,200 FPS those screens' highlight stayed invisible and "Save Profile and
  Continue" never moved on. It shares the same remainder.
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

Soldier AI brains choose between goals (attack, take cover, idle, ...). Each brain update (`0x197C0`, co-op
`0x19050`) truncates its float delta to whole ticks (`cvttss2si eax,[esp+4]`) and adds them to every active goal's
counter (`0x18D40` -> `0x243C0`). Once a counter passes the goal's period (60, 10 or 1 ticks) the goal is re-scored,
and a change posts message `0x10C` to the NPC's script; `0x192C0` picks the best goal. The console added 2 a frame and
the 60 FPS mode 1, but above 60 FPS the truncation gives 0: goals were only re-scored when a level script changed the
NPC's AI state (`0x3ED90` -> `0x18F90`), so NPCs kept their first choice (for example a plane long gone).
`BrainTicks` (hook right after the conversion) hands each brain whole ticks from its own `KeyedTickRemainder`,
keyed by the brain in ECX. The same update calls `0x223A0` (co-op `0x21F50`), which lowers each of 15 remembered
threats (priority up to 2.0 plus a distance term, set by `0x22410`) by 0.01 and forgets those below zero. It runs
once per update and ignores the delta it is passed (`ret 4`), so threats were forgotten after 6.7 s at 30 FPS, 3.3 s
at 60 and 1.4 s at 144. `ThreatDecay` (hook after the 0.01 load into XMM2) scales the step by ticks / 2. Both hooks
apply in the 60 FPS and unlocked modes; the 60 FPS mode's exact one-tick frames give it the same goal timing as
before, and the original 30 FPS mode is untouched.

Each player physics step ends in `0xDA7D0` (co-op `0xD104A`), which casts a ray straight down from 0.39 above the
player to z = -10000. If it hits nothing (result 1) the game takes the player to be off the world: it zeroes the
velocity and calls the movement step with no time, which puts the player back where the previous step ended. A seam
in the collision mesh misses as well: in 1_1 a vertical ray at x -41.88 to -41.90, on the lip of a doorway early in
the ship, finds no floor. Every step from the same place with the same length lands in the seam again, so the player
stays put while forward is held. One-tick steps (0.067 units) seldom land in it, while unlocked steps (0.028 at
144 FPS, far less at 2,400) almost always do. Players saw the doorway as sometimes impassable at 30 FPS, more often
at 60 and always unlocked. `FloorRayWorld` keeps the collision world the cast uses, and `FloorRayRetry` casts again
0.05 and then 0.15 units to each side before a miss stands; it calls the game's own segment (`0x144C30`), hit record
(`0x142AB0`, `0x142910`) and flag (`0x7A8A0`) functions. In the 60 FPS and unlocked modes, probes of the 1_1 doorway
from three quick saves crossed at 60, 144, 240, 1,000 and 2,400 FPS (one to four retries) where unlocked runs had
stood still. The original 30 FPS mode is untouched.

The script system's frame update (`0x4C0F0`, co-op `0x4F220`, called from the gameplay frame at `0xA4B52` /
`0x98B07`) runs the scripts (`0x48470`), their messages (`0x4A700`), 11 condition watch lists (`0x2E0C0`), the
timer wheel (`0x4BD10`, in time) and frame queues (`0x511B0`). Each watch list (`0x4B700`) counts calls and polls its
waiting conditions every n calls, posting a message (0x7B, 0x132, ...) for each that holds. The console called it 30
times a second, so those periods are console frames: at 144 FPS conditions were polled 4.8 times as often, and AI
scripts woken by them ran their loops that much faster. In 1_1 a soldier's script restarted his reload (play
animation state, native `0x303A0`) every 0.15 s instead of every 0.7 s, before the 0.6-second animation could
finish, so he looked stuck reloading; at 60 FPS it was every 0.4 s. The function override `ScriptFrame` runs the update
only when the running total of ticks enters a new two-tick console frame, with all the time since the last run, so it
runs 30 times a second at any frame rate and the timer wheel still gets every tick. With it the soldier's reload
cadence (13 requests in about 10 s) is the same at 30, 60 and 144 FPS. `MOHRS_SCRIPT_EVERY_FRAME` restores the old
per-frame update for comparison. The 60 FPS and unlocked modes use it; the original 30 FPS mode is untouched.

The entity manager (`0xFAB00`, co-op `0xF35D0`) steps physics in steps of at most one tick, but when its first step
is shorter than 1/64 tick (`0x1EAA2C`) it only runs a zero-length step and returns before the post-step and attached
entity updates: nothing moves that frame. Above 3,840 FPS every frame was that short. Slow motion makes it happen at
ordinary rates: `0xA4F30` multiplies the frame's time by a scale the scripts ramp down for a while (`0x1FF614`..
`0x1FF620`). At the end of 1_1 the player is thrown into the air and the scene slows while a plane passes; at 240 FPS
and above the scaled steps fell under 1/64 tick, so the player, the plane and the other aircraft stood still until the
slow motion ended (144 FPS stayed just above it, 30 and 60 far above). `EntityStep` (a function override) carries
such steps over and runs the manager once the carried time reaches 1/64 tick, with all of it. With it the pause at 240
and about 1,100 FPS moves as at 144: the player drifts and seven aircraft move during it, the plane in view. The
60 FPS and unlocked modes use it; the original 30 FPS mode is untouched.

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

The audit checks all nine frame-end callers and the wait/timer/loading-animation/AI brain hook instructions against
both supplied XBEs, and the floor ray's stack offsets and helper calls, the script update's and the entity manager's calls and overrides. The clock tests also give menus 1,200 FEng units a second at 60-5,000 FPS. The clock tests also model the goal counter (re-scores per 10 s at 60 FPS kept within one at
60-1,000 FPS; the old truncation gives none at 144) and threat memory (6.7 s at every rate).
The standalone clock tests check ten seconds at 30, 60, 120, 144, 240 and 1,000 FPS, conservation of integer
timer fractions, zero/negative timer deltas, and stall recovery.
Loading-animation checks compare phase after five seconds at each frame rate, verify the 1.1-second period,
ensure repeated draws at the same time leave the phase unchanged, and check reset on a new loading screen.

`MOHRS_TIMING=1` logs frame rate, time advanced, player look-update rate, player time advanced, mission elapsed
ticks and player position once per second. A stable 144 FPS run should report about 60 frame/player ticks per
second. During a scripted sequence the look update can be inactive and the mission timer paused.

Development switches for test runs (environment variables): `MOHRS_TIMING=0.1` reports ten times a second;
`MOHRS_ENTITY_DUMP=<seconds>[:<radius>]` lists the entities near the player once; `MOHRS_CONTACT_TRACE` logs each
object the player's contact handler (`0xDEFA0`) sees; `MOHRS_STEP_TRACE=x,y,radius` logs every movement step
(`0xE0E50`) and collision resolve (`0xD8260`) near a point. The last two override game functions, so the DLL must
be relinked after the executable for them to take effect. `MOHRS_ANIM_TRACE=<event tables>` logs the animation
channels playing them and who starts them; `MOHRS_GRAPH_TRACE` logs animation graph state requests (`0x6A120`,
`0xCE030`); `MOHRS_NATIVE_TRACE=find|<owner>` logs a script's native calls; `MOHRS_MESSAGE_TRACE` logs posted script
messages (`0x4ADB0`); `MOHRS_WATCH=<from>-<to>:<addr>:<bytes>,...` sums each dword's changes over a window
(`MOHRS_SERIES=<addr>:<n>` prints floats every frame of it); `MOHRS_DUMP=<addr>:<bytes>` dumps memory with
`MOHRS_ENTITY_DUMP`. `XBE_MUTE` silences the game's sound output.

`tools/fps_probe.ps1` creates an isolated run folder under `re/fps-probes`, copies the release executables/DLLs
and uses fresh settings/saves. It never sends OS input and only stops processes from that run folder. Ask before
running it while the user may be using the PC. For example:

```powershell
& tools/fps_probe.ps1 -Cap 60 -Seconds 18
& tools/fps_probe.ps1 -Cap 120 -Unlocked -Seconds 18
& tools/fps_probe.ps1 -Cap 144 -Unlocked -Seconds 18
```

Runs reuse one folder, `reps-probes
un`, so the Windows volume mixer keeps one entry for them, and are muted
(`XBE_MUTE`) unless `-Sound` is given; each run's logs and settings are copied to its own results folder. `-Fps30`
uses the original 30 FPS mode. `-Pad "9/6:forward,12:a"` drives the virtual pad inside the game (no OS
input): a word at a time in seconds after the first poll, held for the time after `/` (0.1 s otherwise); words are
`forward`, `backward`, `strafeleft`, `straferight`, `lookleft`, `lookright`, `jump`, `crouch` and the menu buttons.
`-Report 0.25` sets the timing report interval. `-AnyBuild` loads a quick save made by another build. For example,
the 1_1 doorway: `-Unlocked -Cap 240 -QuickSave "6:load" -QuickSaveFrom <folder> -AnyBuild -Pad "10/40:forward"`.

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

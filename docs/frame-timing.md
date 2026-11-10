# Frame timing: 60 FPS and unlocked

`settings.toml`, before any `[keys]` table:

```toml
unlocked_fps = true   # experimental, default false; takes precedence over fps60
fps60 = true          # when unlocked_fps is false: the 60 FPS patch; false is the original two-blank wait
fps_cap = 144         # 0 = no cap below 2400 (app::kMaxFps), 1 = monitor refresh minus three
vsync = false
fps_counter = true
```

The 2400 FPS ceiling keeps frames longer than the entity manager's smallest step (1/64 tick, about 3,840 FPS).
Both executables have every hook below. The original 30 FPS mode runs unchanged guest code for all of them.

## Mechanism

The game already has a float delta path. Frame end (`0x13A90`) returns the whole vertical blanks passed (minimum 1);
each of its nine callers (gameplay, loading, front end, shutdown) converts that with `cvtsi2ss xmm0,eax`, multiplies
it by the PAL/NTSC scale and stores the global float delta. One tick is 1/60 s.

- `FrameDelta` runs after the conversion on all nine paths and replaces XMM0 with steady-clock elapsed seconds
  times 60. The first delta is one tick; a stall is capped at six ticks (the entity manager's maximum) and the rest
  is dropped, not caught up.
- `FrameWait` lets zero blanks pass in unlocked mode. The SDK still raises 60 Hz vertical blank callbacks, so console
  time is unchanged. Presentation uses the SDK's `fps_cap`/vsync.

The main update passes float deltas to entities, animations, camera, fades and player turning, and the entity
manager subdivides deltas above one tick. Everything below is a system that counted frames or whole ticks instead.
Render frame counters are left alone: some readers use their parity to spread work across frames. Each remainder is
advanced once at its conversion site, not per entity.

## Addresses

| Operation | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Frame end, returns whole vertical blanks passed, minimum 1 | `0x13A90` | `0x13AE0` |
| Wait compares against 2, EAX / ECX | `0x13AAF` / `0x13ACA` | `0x13AFF` / `0x13B1A` |
| Main gameplay frame | `0xA4A90` | `0x98A40` |
| Global float frame delta | `0x1FF528` | `0x1F7648` |
| PAL/NTSC delta multiplier, initially 1 | `0x1FC678` | `0x1F54D8` |
| Player look update, float delta at EBP+8 | `0xE0A50` | `0xD8980` |
| Integer mission timer conversion, hook before PUSH EAX | `0xA4C32` | `0x98C88` |
| Integer UI delta conversion, hook before PUSH EAX | `0xA4CEC` | `0x98D94` |
| In-game menu units (`0x11DD20` / `0x118150`), hook before PUSH EAX | `0x11DDCB` | `0x11823D` |
| In-game menu modal loop units (end-of-mission screens), hook before PUSH EAX | `0x11DC48` | `0x118078` |
| Save/profile popup units (`0xF68A0` / `0xEF2C0`), hook before PUSH EAX | `0xF68D6` | `0xEF2F6` |
| Entity delayed-message delta, hook before SUBSS XMM0,XMM3 | `0xFABB4` | `0xF3684` |
| Loading animation initialisation, resets phase | `0xFC9FD` | `0xF550D` |
| Loading animation float phase, hook after CVTSI2SS | `0xFD3DC` | `0xF5EEC` |
| Profile/save popup animation initialisation | `0xF6B74` | `0xEF594` |
| Profile/save popup float phase, hook after CVTSI2SS | `0xF6C5A` | `0xEF67A` |
| Soldier brain goal ticks, hook after CVTTSS2SI | `0x197C6` | `0x19056` |
| Threat memory decay, hook after the 0.01 load | `0x223A8` | `0x21F58` |
| Player floor ray: collision world before the cast | `0xDAC19` | `0xD1581` |
| Player floor ray result, hook at cmp edi, 1 | `0xDAC2A` | `0xD1592` |
| Script system frame update, function override | `0x4C0F0` | `0x4F220` |
| Entity manager update, function override | `0xFAB00` | `0xF35D0` |
| Player view shake and recoil update, function override | `0xD6700` | `0xCB440` |
| Mounted gun firing kick block, skip hook to its end | `0xDDA3F` -> `0xDDC23` | `0xD75F5` -> `0xD7848` |
| Its oscillator steps (time), hook after MULSS by [EBP+8] | `0xDDB92` | `0xD774D`, `0xD77BC` |
| Animation event scan | `0x6FA30` | `0x62470` |
| Its loop wrap branch, hook jumping to the forward scan | `0x6FB74` -> `0x6FB0D` | `0x625B4` -> `0x6254D` |
| Timed particle update (`0xA7ED0` / `0x9C110`), hook after keep = 1 - drag | `0xA7F16` | `0x9C156` |
| Pooled particle update (`0xABD10` / `0xA0690`), hook after keep = 1 - drag | `0xABD70` | `0xA06E0` |
| Weapon muzzle puff spawn, function override | `0xE2CF0` | `0xDAE10` |

Addresses in the text below are default.xbe unless a co-op one is given.

## Frame-dependent systems

Several fixes run a system once per **console frame** (two ticks, 1/30 s) with all the time since its last run.
Where a `*_EVERY_FRAME` switch is named, it restores the old per-frame behaviour for comparison.

### Whole-tick conversions

- **Mission timer** is an integer: fractional ticks accumulate until whole.
- **UI/front-end update** takes whole ticks: separate remainder in its own scaled units.
- **In-game menus** (post-mission stats, save screens, `0x11DD20`) and the **save/profile popup** (`0xF68A0`, vtable
  `0x1EA340`) convert the delta to FEng units as `(int)(dt * 20)`, 1,200 a second. Truncated per frame that is 4%
  slow at 144 FPS and zero above 1,200 FPS (no blinking highlights, stalled transitions and button responses). The
  in-game menu shares the front end's FEng remainder (`MenuTicks`), the popup has its own (`PopupTicks`). The
  end-of-mission screens run in the in-game menu's modal loop (from `0x11DA95`, each pass ending in `0xA2E20`),
  which converts at `0x11DC42` (co-op `0x118072`) and shares `MenuTicks`.
- **Delayed entity messages** keep float countdowns but truncated their delta to an integer: the original float
  delta in XMM2 is used instead of the truncated copy in XMM3.

### Loading and popup animations

The loading stars/medal renderer (`0xFD280`, co-op `0xF5D90`) increments a float draw counter at loading-object
`+0x10` every draw, wraps at 33 and multiplies by `2*pi/33`, so it sped up with the render rate. `LoadingAnimation`
supplies a continuous phase at 30 steps a second (the console's 1.1-second cycle), reset at texture initialisation.
The profile/save popup renderer (`0xF6C30`, co-op `0xEF650`, the `MemoryCardPopup.fng` hierarchy) uses the same
`medal.xsh` texture, cycle and constant with its counter at `+0x14`; `PopupAnimation` does the same with its own
clock, reset at popup initialisation. Integer counters and draw calls are untouched.

### Animation events

The scan at `0x6FA30` fires an animation's events (dialogue lines, opcode `0xCAF0`, sounds, effects) to the entity's
listener (vtable `0x1E84F0`, handler `0xCF780`). It turns previous and current animation time into whole frames at 30
a second and keeps a started flag at `+0xE4`. Previous below current fires the events in between (on the first scan,
all up to current); previous at or above current is a loop wrap, which with the flag clear fires every event. At 30
FPS a first scan always advances a frame; faster, it can stay on frame 0 and fire every event at once and again on
time (dialogue heard twice). `AnimEventFirstScan` sends equal frames to the forward scan. `MOHRS_EVENT_TRACE=1` logs
every event with the guest call chain.

### Soldier AI

- **Goal re-scoring.** The brain update (`0x197C0`, co-op `0x19050`) truncates its delta to whole ticks
  (`cvttss2si eax,[esp+4]`) and adds them to each active goal's counter (`0x18D40` -> `0x243C0`). Past the goal's
  period (60, 10 or 1 ticks) the goal is re-scored, a change posts message `0x10C` to the NPC's script and `0x192C0`
  picks the best goal. Above 60 FPS the truncation gives 0 and goals were only re-scored when a script changed the AI
  state (`0x3ED90` -> `0x18F90`). `BrainTicks` gives each brain whole ticks from a `KeyedTickRemainder` keyed by ECX.
- **Threat memory.** The same update calls `0x223A0` (co-op `0x21F50`), which lowers each of 15 remembered threats
  (priority up to 2.0 plus a distance term, set by `0x22410`) by 0.01 per call and forgets those below zero, ignoring
  its delta (`ret 4`): 6.7 s at 30 FPS, 1.4 s at 144. `ThreatDecay` scales the step by ticks / 2.

Both apply in the 60 FPS and unlocked modes; at 60 FPS the exact one-tick frames give the same goal timing as before.

### Player floor ray

Each player physics step ends in `0xDA7D0` (co-op `0xD104A`), casting a ray down from 0.39 above the player to
z = -10000. A miss (result 1) is taken as off the world: velocity is zeroed and the movement step runs with no time,
putting the player back. Seams in the collision mesh also miss (1_1: a doorway lip early in the ship, x -41.88 to
-41.90), and the same step length from the same place hits the seam again, so the player stops. Short unlocked
steps land in seams far more often than one-tick steps. `FloorRayWorld` keeps the cast's collision world and
`FloorRayRetry` casts again 0.05 and then 0.15 units to each side before a miss stands, using the game's segment
(`0x144C30`), hit record (`0x142AB0`, `0x142910`) and flag (`0x7A8A0`) functions.

### Script system

The script frame update (`0x4C0F0`, called from the gameplay frame at `0xA4B52` / co-op `0x98B07`) runs scripts
(`0x48470`), their messages (`0x4A700`), 11 condition watch lists (`0x2E0C0`), the timer wheel (`0x4BD10`, in time)
and frame queues (`0x511B0`). Each watch list (`0x4B700`) polls its conditions every n calls and posts a message
(0x7B, 0x132, ...) for each that holds, so polling and the AI loops it wakes scaled with frame rate (a 1_1 soldier
restarted his 0.6 s reload animation every 0.15 s at 144 FPS instead of 0.7 s). `ScriptFrame` runs the update once
per console frame; the timer wheel still gets every tick. `MOHRS_SCRIPT_EVERY_FRAME`.

### View shake and recoil

The view shake and recoil update (`0xD6700`; thiscall, yaw and pitch offset pointers and the time, `ret 0xC`) is
called each frame on foot (`0xE0AE9` / co-op `0xD8A19`) and on a mounted gun (`0xDC90C` / `0xD614C`). Its two shake
slots (`+0xD58`, `+0xD7C`, started by script natives through `0xD5F70`) advance in time, but each call adds a random
jolt to the persistent view offsets `+0x284`/`+0x288`, and while a mounted gun fires (`+0xD4C` bit 0x40) steps its
shake level `+0xDA8` by a fixed amount, so shake scaled with frame count. `ShakeFrame` runs it once per console frame
per player. `MOHRS_SHAKE_EVERY_FRAME`.

### Mounted gun kick

While a mounted gun fires, the player update (`0xDB530`, co-op `0xD4BF0`) moves the gun along its axis by 0.006
times an oscillator, or for a gun with a seat (player `+0xD46` bit 8, the 1_11 boat turret) the seat matrix `+0xEE0`
(co-op `+0xFC0`) by 0.02 times it, cumulatively each frame. The oscillator (`0x27465C`, step `0x201F0C`, limit
`0x201F08`; co-op also a seat one at player `+0xE78`) moves its step of 1 a tick and flips at +-1: at 30 FPS it flips
every frame (+-0.02 twitch), faster it ramps and the additions pile up into a violent swing. `MountedKick` skips the
block between console frames and `MountedKickStep` gives the oscillator exactly two ticks per console frame (fewer
would miss a flip and let the seat drift). `MOHRS_SHAKE_EVERY_FRAME`.

### Entity manager minimum step

The entity manager (`0xFAB00`) steps physics in steps of at most one tick, but a first step under 1/64 tick
(`0x1EAA2C`) only runs a zero-length step and returns before the post-step and attached updates: nothing moves.
Besides very high frame rates, slow motion triggers it: `0xA4F30` scales frame time by a script-ramped factor
(`0x1FF614`..`0x1FF620`), and at the end of 1_1 everything froze at 240 FPS and above. `EntityStep` carries short
steps over and runs the manager once the carried time reaches 1/64 tick.

### Particles

The timed class (vtable `0x1E5E78`, update `0xA8810`, callback `0xA7ED0`; co-op `0x9C110`) and pooled class (vtable
`0x1E61E8`, `0xACC10` -> `0xABD10`; co-op `0xA0690`) compute `keep = 1 - drag` and apply `v = v * keep + accel *
seconds` once per frame, so damping scaled with frame rate. `ParticleDrag`, after the subtraction (keep in XMM7 /
XMM1, seconds in XMM3 / XMM0), raises keep to the power `seconds * 30`. The stateless class (`0x1E6460`, update
`0xB2AF0`, draw `0xB1110`) computes particles from their age and needs nothing. `MOHRS_PARTICLE_DRAG_EVERY_FRAME`.
Not yet checked in a scene that uses these classes.

### Muzzle puffs and the effect pool

Effects come from a pool of 128 emitters (`0xB0120`, in use at `0x270E10`). An effect of priority 0 is only created
while more than nine are free; otherwise its spawn (`0xB55B0`) returns nothing. A weapon's muzzle puff (`0xE2CF0`,
from the weapon update `0xE3330`) spawns a 0.54 s emitter every frame while its flash flag (`+0x340` bit 0x20) is
set: about 16 alive at 30 FPS, about 130 at 240, exhausting the pool so script effects failed (1_1: the floor-fire
burst, hash `0xC4DE5EC2`, spawned through native `0x43830`, stopped by `0x38B90`). `MuzzlePuffFrame` spawns at most
one puff per weapon per console frame. `MOHRS_PUFF_EVERY_FRAME`.

## Verification

```powershell
python tools/timing_audit.py
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/frame_clock_test.cpp -o re/frame_clock_test.exe"
& re/frame_clock_test.exe
cmd /c "tools\dev.bat cmake --build out\build\release -- -k 0 -j 12"
```

`timing_audit.py` checks the nine frame-end callers and every hook instruction above against both XBEs, including
the floor ray's stack offsets and helper calls and the script and entity manager overrides. The clock tests check
ten seconds at 30-1,000 FPS, integer timer remainders, zero/negative deltas, stall recovery, 1,200 FEng units a
second at 60-5,000 FPS, goal re-scoring and threat memory at every rate, and the loading animation's phase, period
and reset.

`MOHRS_TIMING=1` logs frame rate, ticks advanced, player look-update rate and ticks, mission ticks and player
position once a second; a steady run reports about 60 ticks a second at any frame rate. Other diagnostics and
`tools/fps_probe.ps1` are in [debugging.md](debugging.md).

Still to play-test: movement distance, weapon cadence, physics, pause/resume, cutscene sync and two-controller
co-op at high frame rates. Other frame-dependent behaviour may need hooks, which is why unlocked stays opt-in.

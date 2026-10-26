# Adjustable field of view

Set `fov` in `settings.toml` beside the executable, before the `[keys]` table, then restart the game:

```toml
fov = 90
```

The default is `70`, which preserves the existing view exactly. Values are whole horizontal degrees on the
game's original **4:3 reference frame**, like the BLACK port's setting. Widescreen applies the existing Hor+
expansion afterward. The range is 45-140; values outside it are clamped when settings load and written back.

| Setting | Horizontal view at 4:3 | Horizontal view at 16:9 |
|---|---:|---:|
| `fov = 70` (default) | 70° | 86.07° |
| `fov = 90` | 90° | 106.26° |
| `fov = 110` | 110° | 124.59° |

This adjusts the player's world camera in both executables, including registered split-screen player cameras.
Both projection tangents are scaled by `tan(fov/2) / tan(70°/2)`, preserving the original aspect and the game's
zoom magnification relative to the unzoomed view. Aiming and scope views still follow their native FoV changes;
they receive the same tangent multiplier. Mouse sensitivity and the player's native zoom/FoV fields are unchanged.

The HUD and menus are unchanged. The independent menu/cinematic camera and temporary first-person-model
projection are excluded by checking the camera against the player registry. Scripted first-person sequences
that use the player's world camera receive the selected FoV too. There is no separate viewmodel-FoV setting yet.

## Implementation and checks

The existing `SetFov` overrides remain at `0x82BA0` in default.xbe and `0x754F0` in moh3rdc.xbe. They apply the
existing widescreen correction and then the new player-camera correction, before projection/frustum caches
are rebuilt. Each call starts from the game's newly computed tangents, so adjustments do not accumulate.

| Layout | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Player pointer list | 0x270108 | 0x265278 |
| Player count | 0x1FC658 | 0x1F54B8 |
| World camera within player | +0x1150 | +0x1230 |
| Camera horizontal / vertical tangent | +0x40 / +0x44 | +0x40 / +0x44 |

`tests/fov_test.cpp` checks default identity, bounds, requested hip FoV, projection aspect, finite widescreen
angles and preservation of zoom ratios for several aimed/scoped values:

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/fov_test.cpp -o re/fov_test.exe"
& re/fov_test.exe
```

`MOHRS_FOV_TRACE=1` logs the actual horizontal and vertical angles before/after adjustment once a second.
`tools/fps_probe.ps1 -Fov 90` sets it for an isolated test run and restores the caller's environment afterward.

2026-10-26 checks: both release images built without errors; single-player 1_1 at FoV 90 ran through its
scripted opening into gameplay, logging 86.07° -> 106.26° horizontally. The co-op executable on 1_4 logged the
same change with its different player-camera offset. A single-player FoV 70 run logged unchanged angles
(86.07° horizontal / 52.50° vertical). All three ran at about 144 FPS with normal game time and no logged errors.
The user accepted the feature on 2026-10-26 and requested its commit. The automated probes do not exercise
aiming, sniper scopes, mounted guns or two-controller split screen.

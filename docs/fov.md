# Field of view

`fov` in `settings.toml` (before `[keys]`) or PC Options: whole horizontal degrees on the game's original 4:3 frame,
45-140, default 70 (the original view). Out-of-range values are clamped on load and written back. Widescreen adds
its Hor+ widening afterwards:

| `fov` | Horizontal at 4:3 | Horizontal at 16:9 |
|---|---:|---:|
| 70 | 70° | 86.07° |
| 90 | 90° | 106.26° |
| 110 | 110° | 124.59° |

Applies to the player world cameras in both executables, including split-screen players. Both projection tangents
are scaled by `tan(fov/2) / tan(70°/2)`, which keeps the aspect and the game's zoom ratios: aiming and scopes still
make their native FoV changes and get the same multiplier. Mouse sensitivity and the player's zoom/FoV fields are
unchanged. HUD, menus, the menu/cinematic camera and the temporary first-person-model projection are excluded by
checking the camera against the player registry; scripted first-person sequences on the player's world camera get
the selected FoV. There is no separate viewmodel FoV.

## Implementation

The `SetFov` overrides (`0x82BA0` default.xbe, `0x754F0` moh3rdc.xbe) apply the widescreen correction and then the
player-camera correction, before the projection/frustum caches are rebuilt. Each call starts from the game's freshly
computed tangents, so nothing accumulates. `src/common/fov.{h,cpp}`.

| Layout | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Player pointer list | `0x270108` | `0x265278` |
| Player count | `0x1FC658` | `0x1F54B8` |
| World camera within player | `+0x1150` | `+0x1230` |
| Camera horizontal / vertical tangent | `+0x40` / `+0x44` | `+0x40` / `+0x44` |

## Tests

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/fov_test.cpp -o re/fov_test.exe"
& re/fov_test.exe
```

Covers default identity, bounds, requested hip FoV, projection aspect, finite widescreen angles and zoom ratios for
several aimed/scoped values. `MOHRS_FOV_TRACE=1` logs the horizontal and vertical angles before and after the
adjustment once a second (`tools/fps_probe.ps1 -Fov 90` sets the FoV for a probe). Not covered: aiming, scopes,
mounted guns and two-controller split screen in game.

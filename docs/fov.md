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

## Near plane

The player camera's near plane is 0.4 (camera `+0x38`, far 200 at `+0x3C`; set for each player at level start,
`0xA2C29`). The original 4:3 view's near plane is 0.28 wide on each side; widened, its sides reach further (0.37 with
widescreen at `fov` 70, 0.49 at 85) and cut into a wall the player stands against, showing what is behind it. While
widescreen is on or `fov` is not 70, the near plane is pulled in by `min(tan 35° / across, tan 26.25° / down)`, so
its half width and height never exceed the original's (0.229 at 85 on 16:9). Aiming and scopes (narrower) keep 0.4.

It is changed only for the duration of the two builds that read it, the camera's frustum (`0x82E50`, co-op
`0x757A0`) and the view's projection (`0x84290`, co-op `0x76BE0`, camera at `[view + 0xE4]`), and put back after, so
the game keeps and saves its own value. Only registered player world cameras; the first-person-model camera copies
the world camera's 0.4. The host depth buffer is D24S8, so the closer near plane costs no visible precision.
`MOHRS_NEAR_PLANE_ORIGINAL=1` keeps 0.4.

## Tests

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/fov_test.cpp -o re/fov_test.exe"
& re/fov_test.exe
```

Covers default identity, bounds, requested hip FoV, projection aspect, finite widescreen angles and zoom ratios for
several aimed/scoped values, and that the scaled near plane is never wider or taller than the original.
`MOHRS_FOV_TRACE=1` logs the horizontal and vertical angles before and after the adjustment once a second (`tools/fps_probe.ps1 -Fov 90` sets the FoV for a probe). Not covered: aiming, scopes,
mounted guns and two-controller split screen in game.

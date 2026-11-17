# Widescreen

The game draws a 640x480 4:3 frame only and never reads the console's widescreen flag. `widescreen` in
`settings.toml` (default on) or PC Options shows that frame stretched to the output's shape and widens the 3D view to
match. Off, or on an output narrower than 3:2, the frame is shown 4:3 with bars at the sides.

## Frame shape

`app::FrameAspect()` (`sdk/runtime/app/settings.cpp`) is the shape the frame is shown at: 4:3 without widescreen,
else the output's width over height (the resolution setting, or the desktop's), up to `GameInfo::widest_aspect`.
MOHRS sets that to 32:9, so 16:10, 16:9, 21:9 and 32:9 outputs are filled; a wider output gets bars at the sides.
With `widest_aspect` 0 (the SDK default) a widescreen frame is 16:9. The output is read again when the resolution
setting changes; resizing a window does not change the shape, the picture is fitted into it with bars.

`widen = FrameAspect / (4/3)`. Everything below uses it:

| Part | Where | What |
|---|---|---|
| Presentation | `BlitFrame`, `sdk/runtime/gpu/d3d12.cpp` | the frame fitted into the window at `FrameAspect` |
| 3D view | `WidenView`, after `SetFov` (default.xbe `0x82BA0`, moh3rdc.xbe `0x754F0`) | the camera's horizontal tangent (`+0x40`) times `widen`, vertical kept (Hor+) |
| HUD, text, menus | `Hud::ScreenX`, `src/common/widescreen.cpp` | 2D draws on the frame squeezed toward the centre by `1/widen`, so they keep their shape in the middle 4:3 |
| Full-screen masks | `Hud::Decoded` | textures with an opaque black border (the sniper scope) keep their shape, the border fills the sides |
| Menu mouse, overlays, cursor | `menu_pointer.h`, `game_info.cpp`, `quick_save.cpp`, `software_cursor.cpp` | placed on the same fitted picture |

The squeeze leaves a vertex on the frame's left or right edge where it is, so full-width 2D quads (loading screens,
the cinematic letterbox bars, fades) stay full width.

The player camera's near plane is pulled in while the view is widened, see [fov.md](fov.md). The render scale is one
factor for both axes (from the output's height), so the frame's horizontal resolution is stretched by `widen`.

## Movies

Mission intro and outro movies (`.mad`, EA MAD) come in two shapes:

| Movies | Frame | Shape |
|---|---|---|
| `lv_Nin` for 1_2 to 1_9 and 1_11 | 320x144 | 20:9 |
| `lv_Nout`, `Lv_1out`, except 1_9 | 640x288 | 20:9 |
| `lv_1in`, `lv_1inlg`, `logo` (1_16) | 320x224 | 10:7 |
| `demo` (1_16), `Lv_9out` | 640x448 | 10:7 |

The movie player (default.xbe `0xFC170`, moh3rdc.xbe `0xF4C80`, its only caller `0xFC5F0`) fits the movie to the
screen with one scale, `min(screen w / movie w, screen h / movie h)`, centres it and draws it as one quad through
the 2D renderer (renderer `0x1A39E0`, vtable `0x1FAD64`: `+4` position, `+8` x and y scale, `+0xC` draw; quad
`0x1A3560`). On the 640x480 screen a 20:9 movie is 640x288 with bars above and below.

The `[[hook]]` `MovieScale` at the FLD after the fit (default.xbe `0xFC372`, moh3rdc.xbe `0xF4E82`) replaces the two
scales the player passes on. Its stack there:

| | Offset |
|---|---|
| x scale (also used for the x position) | `[esp+0x14]` |
| screen width, height | `[esp+0x18]`, `[esp+0x1C]` |
| y scale (also used for the y position) | `[esp+0x20]` |
| movie width, height (floats) | `[esp+0x24]`, `[esp+0x2C]` |

`MovieFit` (`src/common/widescreen.h`): `y scale = min(screen h / movie h, screen w * widen / movie w)`,
`x scale = y scale / widen`. On the stretched frame the movie keeps its shape at the largest size that fits; at 4:3
it is the game's own fit. A 20:9 movie fills the width at 16:9 (bars above and below) and the height at 21:9 (narrow
bars at the sides); a 10:7 movie fills the height.

The hook also records the movie's size, and the extension's `Sampled` marks a draw that samples a texture of exactly
that size as the movie: `ScreenX` does not squeeze it. The size is cleared when the player returns
(`X86_FUNC` override of `0xFC170` / `0xF4C80`). The first movie draw of each size logs
`widescreen: movie WxH drawn for the A frame`.

## Tests

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc -Isdk/runtime tests/movie_fit_test.cpp -o re/movie_fit_test.exe"
& re/movie_fit_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/menu_pointer_test.cpp -o re/menu_pointer_test.exe"
& re/menu_pointer_test.exe
```

`movie_fit_test` checks the game's fit at 4:3 and, for each movie shape from 16:10 to 32:9, that the shape is kept,
the movie stays inside the frame and fills its width or height. `menu_pointer_test` maps pointer positions at 4:3,
16:9 and 21:9.

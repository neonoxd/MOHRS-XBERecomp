# Menus: PC Options and mouse control

The menus are EA FEng packages (`.fng`) in `data\shell\shell.viv` and `data\pausscrn\pause.viv` / `mppause.viv`,
each screen paired with a C++ handler that the shell's factory picks by package name. Nothing on disc changes and no
game data is in the repository: packages are modified in memory when the game first asks its archive for them.

`src/common/feng.{h,cpp}` reads and writes FEng packages: chunks with four-character tags (`PkHd`, `Obj`, `FOb`,
`PkgR`, `Targ`, ...) holding fields with two-character tags (`On` name, `OP` GUID/name hash/flags, `SA` colour and
position, `St`/`SL` text and label, `MsgR` message responses, `EV` script events). All 132 packages of the shell,
pause and multiplayer pause archives round-trip byte for byte.

## PC Options

The shell's **Options** and the pause screen's **Preferences** get a **PC Options** entry under Audio, opening a
screen in the game's menu art laid out like HUD Options:

| Row | Values | `settings.toml` |
|---|---|---|
| Display Mode | Fullscreen, Borderless, Windowed (applied on confirm) | `display_mode` |
| Render Scale | 50% to 200% of the output height | `render_percent` |
| Frame Rate | 30 FPS, 60 FPS, Unlocked | `fps60`, `unlocked_fps` |
| FPS Limit | Monitor, 30 to 480, Off | `fps_cap` (1 Monitor, 0 Off; Off and higher stop at 2400) |
| VSync | On, Off | `vsync` |
| Field of View | 45 to 140 in steps of 5 (70 original) | `fov` |
| Widescreen | On, Off | `widescreen` |
| FPS Counter | On, Off | `fps_counter` |
| Mouse Sensitivity | 0.1 to 5.0 | `mouse_sensitivity` |
| Invert Mouse | On, Off | `invert_mouse` |

Left/right change the focused row at once (display mode only on confirm). Confirm (A, Enter) writes `settings.toml`;
back (B, Backspace) restores every row as it was when the screen opened. Settings without a row stay file-only.

- `src/common/options_package.{h,cpp}` builds `PCOptions.fng` from the archive's `HUD.fng`: the Crosshairs row is
  the pattern for ten rows (name, value, a button group with flashing arrows), the HUD rows are removed, the title
  becomes PC OPTIONS and the focus/highlight targets point at the new buttons. It adds the entry to
  `Option_overlay.fng` (between Audio and Difficulty, moving the entries below down a row) and to `Preferences.fng` /
  `MPPreferences.fng` (under HUD Options) by copying the Audio entry with its path pointing at `PCOptions.fng`.
- `src/common/options_menu.{h,cpp}` holds the rows. The factory gives `PCOptions.fng` the HUD screen's handler class
  (vtable `0x1ED40C`), whose event function hands that package's events over: `HUD Setup` fills the rows,
  `Pad_Left`/`Pad_Right` step the focused row (FEPackage `+0x80`, the object's name hash at `+0x10`), `HUD Config
  Confirm` saves, `HUD Config Revert` restores. Up/down are the game's own navigation.

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Archive file lookup (cdecl: archive, path, flags) | `0x1132E0` | `0x10BDA0` |
| Screen handler factory (cdecl: package name) | `0x120F00` | `0x11B1B0` |
| HUD screen handler's event (thiscall, ret 0xC) | `0x12A620` | `0x124C20` |
| SetText (stdcall: package, object, ASCII text) | `0x138A40` | `0x131D10` |
| RunScript (stdcall: package, object, script) | `0x138330` | `0x131600` |
| Menu sound (cdecl) | `0x123C10` | `0x11DC90` |

The screen is English in every language.

## Mouse in menus

- Hover focuses an entry, left click selects it, the wheel moves through entries. A stationary pointer leaves
  focus to keyboard/controller navigation.
- On settings screens (PC Options, stock Audio, HUD, controller options) clicking a label or value focuses its row,
  even when its arrows are hidden; the arrows or the wheel change the value. The displayed confirm hint saves, the
  back hint cancels, right click goes back.
- Pause headings are clickable (including the multiplayer leaderboard/preferences/exit headings) and open that page
  directly. Right click on a pause page resumes, as does clicking the Start/resume prompt. Escape still pauses.
- On the mission map, clicking the bottom mission name confirms the selection and opens its details; disabled
  missions stay disabled.
- Mouse navigation follows the player KBM is assigned to (see [input.md](input.md)) and never takes over a menu
  owned only by another controller.
- The pointer is `cursor.png`, embedded at build time and drawn in software, scaled with the picture (24 pixels at
  720p) with its tip at the hit-test point. The Windows pointer is hidden over the client area. The software pointer
  is only drawn over freshly drawn interactive menu controls: not on loading screens, cutscenes, gameplay, while
  unfocused or outside the window.

### Implementation

`src/common/menu_mouse.cpp` records the actual rendered font bounds and sprite quads (localised text,
justification, parent transforms, animation, safe area) and hit-tests against them. It uses the game's own focus
setter and object/package/handler messages, so normal routing is kept. A click is bound to its package: it cannot
activate the next package or turn into fire/aim on resume. All guest reads and calls happen on the UI's guest
thread; the window side only records pointer state.

The pause adapter finds KBM's port 0 in the input manager's player-to-port table (`0x271188` / co-op `0x266618`)
and derives its FEng player mask, which package selection, geometry, queued messages and pause-tab actions all use.

Pause pages get mouse-only package responses cloned from their native left/right transitions, with the destination
changed to the requested tab and the handler and overlay cleanup commands kept. Response kind `0x200` queues a
native replace-package operation (`0x93300` / co-op `0x85C60`) with the original input mask, parent linkage and
lifecycle. The click enqueues the response after the native update has dispatched initialisation, preserving event
order without waiting for animations. `MenuFile` injects these responses alongside the PC Options changes.

| Role | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| FEng update (polls controllers, optionally mouse, runs packages and message queues) | `0x95220` | `0x87B80` |
| Per-package controller handling | `0x94330` | `0x86C90` |
| Per-package mouse handling (native, unused) | `0x94E50` | `0x877B0` |
| Set package focus | `0x96900` | `0x892A0` |
| Queue FEng message | `0x93260` | `0x85BC0` |
| Button position helper | `0x88590` | `0x7AF10` |
| FEng render | `0x93EE0` | `0x86840` |
| Base FEng render object callback | `0x1164E0` | `0x10EFB0` |
| ShellManager render override (the live shell/pause callback) | `0x125E10` | `0x11FE90` |
| Font drawing after positioning/justification | `0x162AE0` | `0x15BEB0` |
| ASCII font bounds (x/y/width/height outputs) | `0x162B20` | `0x15BEF0` |
| Transformed sprite quad | `0xB6B90` | `0xAB430` |
| Object message lookup | `0x958D0` | `0x88270` |
| Directional button navigation | `0x88630` | `0x7AFB0` |

ShellManager overrides the base render callback, so both are hooked. In default.xbe the FEng update is called from
the shell manager (`0x1262B0`) and the pause UI (`0xF68A0`).

### Why the native mouse path stays off

The FEng update runs per-package mouse handling only when engine byte `+3` is set: it calls interface vtable slot
`+0x48` to fill an eight-byte sample (four 16-bit values: x, y, an unverified third value, button bits) and updates
mouse state through `0x91DB0`. In default.xbe's platform interface vtable (`0x1ECAD0`) slot `+0x48` is an empty
function (`0x6AD80`) and slots `+0x4C` (object hit test) and `+0x50` are a shared return-zero stub (`0x186070`).
Setting byte `+3` would read an uninitialised sample and reject every object, and the stubs have other callers, so
the adapter uses narrow function overrides instead.

### Layout and calling conventions

- Engine: interface pointer `+0x10C`, package-list head `+0xE4`, input-enabled byte `+2`, mouse-path byte `+3`.
- Package: next `+4`, name pointer `+0x0C`, input mask `+0x1C`, focused object `+0x80`, button array `+0x84`, button
  count `+0x88`.
- Object: GUID `+0x0C`, name hash `+0x10`, type `+0x18`, flags `+0x1C` (the native hover scan skips `0x04000000`),
  parent transform index `+0x20`, current attributes `+0x2C`.
- `SetFocus`: thiscall, `ECX = package`, `(engine, object, emit_messages)`, `ret 0xC`; queues unhighlight/highlight
  to the object and package handler before writing `+0x80`.
- `QueueMessage`: thiscall, `ECX = engine`, `(message_hash, source_object, package, target, input_mask)`.
- Controller and native mouse paths send `Button_Pressed` to a focused object that handles it, else `Pad_Accept`,
  and notify the package handler with target `0xFFFFFFFB`.

| Message (`feng::Hash`) | Hash |
|---|---:|
| `Button_Pressed` | `0x0C407210` |
| `Button_Released` | `0x936A6A7F` |
| `Button_Highlight` | `0xABC08912` |
| `Button_Unhighlight` | `0x55D1E635` |
| `Pad_Accept` | `0x406415E3` |
| `Pad_Back` | `0x911AB364` |
| `Pad_Left` | `0x9120409E` |
| `Pad_Right` | `0xB5971BF1` |

Package contents that shape the adapter: `MainMenu.fng` (six buttons) and `Option_overlay.fng` (ten) are text
objects with button flags, not hit boxes. `Pause.fng` has no button-flagged objects; its backdrop holds the
Start/resume and controller hints, and the Objectives/Load/Preferences/Notes headings are plain text objects. In
`HUD.fng` the left/right arrows are decorative children with no press response.

### Pointer coordinates

`BlitFrame` (`sdk/runtime/gpu/d3d12.cpp`) fits a centred 4:3 or 16:9 picture into the backbuffer, and in widescreen
`Hud::ScreenX` (`src/common/widescreen.cpp`) compresses menu/HUD x around the centre by `1/kWiden` (`kWiden = 4/3`).
A client point is mapped to the displayed picture, then `x = centre + (x - centre) * kWiden` in widescreen, then
into the object's space through the recorded render transforms. Points outside the picture hit nothing.

## Tests

`tests/options_package_test.cpp` needs the archives' `.fng` files from your copy of the game:

```powershell
python tools/xiso.py get "assets/<image>.iso" DATA/SHELL/shell.viv re/viv/shell.viv
python tools/xiso.py get "assets/<image>.iso" DATA/PAUSSCRN/pause.viv re/viv/pause.viv
python -I tools/viv.py re/viv/shell.viv re/viv/shell .fng
python -I tools/viv.py re/viv/pause.viv re/viv/pause .fng
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/options_package_test.cpp src/common/feng.cpp src/common/options_package.cpp -o re/options_package_test.exe"
& re\options_package_test.exe re\viv\shell re\viv\pause [--out <folder>]
```

Mouse adapter host tests (no game launch):

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/menu_pointer_test.cpp -o re/menu_pointer_test.exe"
& re/menu_pointer_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/menu_mouse_test.cpp src/common/menu_mouse.cpp src/common/feng.cpp -o re/menu_mouse_test.exe"
& re/menu_mouse_test.exe
```

They cover presentation coordinates, widescreen compression, row and button bounds, hover focus, routing, modal and
focus transitions, disabled controls, controller ownership with KBM as each of players 1-4, pause headings with
reused package allocations and direct forward/reverse tab requests during opening animations, the seven pause
pages' destinations and cleanup commands, settings rows, arrows, the wheel, mission-map selection, and that the
guest's CPU/FP context survives helper calls. `tests/software_cursor_test.cpp` checks the embedded cursor's pixels,
atlas UVs and coexistence with the FPS font.

Game probes: see [debugging.md](debugging.md) (`tools/menu_probe.ps1`, `MOHRS_MENU_MOUSE_PROBE`). The isolated
profile flow stops at name entry, so the full main-menu path needs a manual check.

# PC Options in the game's menus

The shell's **Options** list and the pause screen's **Preferences** get a **PC Options** entry under Audio. It opens a
screen drawn with the game's own menu art, laid out like the HUD Options screen:

| Row | Values | Setting in `settings.toml` |
|---|---|---|
| Display Mode | Fullscreen, Borderless, Windowed (applied on confirm) | `display_mode` |
| Render Scale | 50% to 200% of the output height | `render_percent` |
| Frame Rate | 30 FPS, 60 FPS, Unlocked | `fps60`, `unlocked_fps` |
| FPS Limit | Monitor, 30 to 480, Off | `fps_cap` (1 is Monitor, 0 Off) |
| VSync | On, Off | `vsync` |
| Field of View | 45 to 140 in steps of 5 (70 is the original view) | `fov` |
| Widescreen | On, Off | `widescreen` |
| FPS Counter | On, Off | `fps_counter` |
| Mouse Sensitivity | 0.1 to 5.0 | `mouse_sensitivity` |
| Invert Mouse | On, Off | `invert_mouse` |

Left and right change the focused row; the change takes effect at once, except the display mode. Confirm (A, Enter)
writes `settings.toml` and switches the display mode; back (B, Backspace) puts every row back as it was when the screen
opened, as the game's own option screens do. Settings without a row (skip_intros, key bindings, vertical mouse
sensitivity and so on) stay in the settings file.

## How it works

The menus are EA FEng packages (`.fng`) in `data\shell\shell.viv` and `data\pausscrn\pause.viv` / `mppause.viv`, each
screen paired with a C++ handler the shell's factory picks by package name. Nothing on disc is changed and no game
data is in the repository: the packages are made in memory when the game first asks its archive for them.

- `src/common/feng.{h,cpp}` reads and writes FEng packages: chunks with four character tags (`PkHd`, `Obj`, `FOb`,
  `PkgR`, `Targ`, ...) holding fields with two character tags (`On` name, `OP` GUID/name hash/flags, `SA` colour and
  position, `St`/`SL` text and label, `MsgR` message answers, `EV` script events). All 132 packages of the shell, pause
  and multiplayer pause archives read and write back byte for byte.
- `src/common/options_package.{h,cpp}` makes `PCOptions.fng` from the archive's `HUD.fng`: the Crosshairs row is the
  pattern for ten rows (name, value, a button group with flashing arrows), the HUD rows go, the title becomes PC
  OPTIONS, the focus and highlight targets point at the new buttons. It adds the entry to `Option_overlay.fng`
  (between Audio and Difficulty, the entries below move down a row) and to `Preferences.fng` / `MPPreferences.fng`
  (under HUD Options), copying the Audio entry with its path pointing at `PCOptions.fng` in the same folder.
- `src/common/options_menu.{h,cpp}` holds the rows and three hooks per image:

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Archive file lookup (cdecl: archive, path, flags) | 0x1132E0 | 0x10BDA0 |
| Screen handler factory (cdecl: package name) | 0x120F00 | 0x11B1B0 |
| HUD screen handler's event (thiscall, ret 0xC) | 0x12A620 | 0x124C20 |
| SetText (stdcall: package, object, ASCII text) | 0x138A40 | 0x131D10 |
| RunScript (stdcall: package, object, script) | 0x138330 | 0x131600 |
| Menu sound (cdecl) | 0x123C10 | 0x11DC90 |

The factory gives `PCOptions.fng` the HUD screen's handler class (vtable 0x1ED40C); its event function hands that
package's events to `options_menu.cpp`: `HUD Setup` fills the rows, `Pad_Left`/`Pad_Right` step the row whose button
has the focus (FEPackage + 0x80, the object's name hash at + 0x10), `HUD Config Confirm` saves, `HUD Config Revert`
puts the rows back. Up and down are the game's own button navigation.

## Checks

`tests/options_package_test.cpp` needs the `.fng` files of the archives, extracted from your copy of the game:

```powershell
python tools/xiso.py get "assets/<image>.iso" DATA/SHELL/shell.viv re/viv/shell.viv
python tools/xiso.py get "assets/<image>.iso" DATA/PAUSSCRN/pause.viv re/viv/pause.viv
python -I tools/viv.py re/viv/shell.viv re/viv/shell .fng
python -I tools/viv.py re/viv/pause.viv re/viv/pause .fng
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/options_package_test.cpp src/common/feng.cpp src/common/options_package.cpp -o re/options_package_test.exe"
& re\options_package_test.exe re\viv\shell re\viv\pause [--out <folder>]
```

`tools/menu_probe.ps1` runs the game in its own folder with `MOHRS_MENU_PROBE`, which presses controller buttons
inside the game at given times (no keyboard or mouse input of the PC's) and saves frames with `capture`.

# Medal of Honor: Rising Sun - XBE Recomp

An unofficial native PC port of the original Xbox version of Medal of Honor: Rising Sun, in progress.

The game's XBE is statically recompiled from x86 to C++ and built into a native 64-bit Windows executable with the
[Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) SDK, vendored in `sdk/`. The Xbox kernel and XDK
libraries run as host code, graphics go through Direct3D 12 and audio through XAudio2.

> [!NOTE]
> This project does not include any game assets or recompiled game code. You must provide the files from your own
> legally acquired copy of the game to build or run it.

## Status

Single-player and co-op boot and run with graphics, audio, saves, controller and keyboard/mouse support.
KBM and the first controller share player 1 in singleplayer. Co-op and split-screen multiplayer setup give KBM
and controllers separate inputs automatically; see [controller routing](docs/controller-routing.md).
Widescreen and 60 FPS are implemented. An experimental mode above 60 FPS uses elapsed-time updates;
see [frame timing and settings](docs/frame-timing.md). The game uses XDK 5455, supported by this repo's SDK.
Adjustable [field of view](docs/fov.md) is available through `fov` in `settings.toml` (70 is the original view).
See [performance measurements](docs/performance.md) for the packed-vertex optimization and benchmark commands.
Display, frame rate, field of view and mouse settings can also be changed in the game's own menus under
[PC Options](docs/pc-options.md), in Options and in the pause screen's Preferences.
The [menu mouse adapter](docs/menu-mouse.md) adds hover/click navigation, settings arrows and wheel control, and
clickable pause headings alongside keyboard and controller input.
See [docs/PLAN.md](docs/PLAN.md) for the milestones.

## Repository layout

| Path | What |
|---|---|
| `sdk/` | XboxXBE-Recomp, vendored and patched here (base commit in `sdk/UPSTREAM.txt`) |
| `config/game.toml` | Recompiler config for `default.xbe`: symbols, data, hooks |
| `src/common/` | Host code shared by every image (`game_info.cpp` describes the game to the runtime) |
| `src/game/` | Hooks for `default.xbe` (`X86_FUNC(sub_XXXXXXXX)` overrides) |
| `tools/` | `xiso.py` (list/extract XDVDFS images), `xbe_info.py` (XBE header, XDK versions, sections), `viv.py` (list/extract EA .viv archives), `dev.bat` (toolchain environment), RE helpers `disasm.py`, `lib_inventory.py`, `symbols_compare.py`, `hle_audit.py`, `dsp_image_check.py` (see [docs/xdk5455.md](docs/xdk5455.md)) |
| `docs/` | Plan and notes |
| `assets/` | Your game files (ignored by git) |
| `generated/`, `out/` | Recompiled code and build output (ignored by git) |

## Building

### 1. Game files
Put your disc image in `assets/` and extract the XBEs from it into `assets/xbe/`:
```
python tools/xiso.py xbes "assets/Medal of Honor Rising Sun.iso" assets/xbe
```
The build reads `assets/xbe/default.xbe`; the game itself runs from the `.iso` in `assets/`. Keep the XBEs out of
`assets/` itself, or the runtime will look for loose data files beside them.

Expected `default.xbe` SHA-1: `9f5197f8ded3ce1cd3e1c1cae14a756a17c39d56`.

### 2. Dependencies
Visual Studio 2022 with **Desktop development with C++**, **C++ Clang Compiler for Windows** and **C++ CMake tools
for Windows**. `build.bat` offers to install the Clang and CMake components if they're missing. Python 3 for the
tools.

### 3. Build
Run `build.bat`. It writes `config/game.toml` with `xbe_symbols` on the first build, recompiles the XBE and builds
`out\build\release\MOHRS.exe`.

The build extracts the original title logo for `MOHRS.exe` and the silver save star for `MOHRSCoop.exe` from your
XBEs and embeds them as Windows icons. Generated icon files stay in the build folder; no extra Python packages
are needed.

### 4. Release zip
Run `release.bat` (needs 7-Zip on `PATH`). It brings the release build up to date and writes `MOHRS-XBERecomp.zip`:
both executables, the `compiler` folder that builds the game code on the player's PC from their own disc image on
the first start, the Visual C++ runtime, `d3dcompiler_47.dll`, licenses and the player's
[README](docs/player-readme.txt). No game code or game data goes into it. The version is `project(... VERSION)` in
`CMakeLists.txt`; it is in each executable's file properties and on the first line of the log.

## License

This repository's own code is under the [MIT License](LICENSE). The vendored SDK in `sdk/` has its own MIT license
(`sdk/LICENSE`) and lists its third party code in `sdk/THIRD_PARTY.txt`. Medal of Honor: Rising Sun, its code and
its data belong to Electronic Arts and are not covered; this project is not affiliated with or endorsed by EA.

## Credits

- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) and [BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp)
  by TGP482, which this port is built on and modelled after. See `sdk/README.md` and `sdk/THIRD_PARTY.txt`.

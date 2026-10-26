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
Widescreen and 60 FPS are implemented. An experimental mode above 60 FPS uses elapsed-time updates;
see [frame timing and settings](docs/frame-timing.md). The game uses XDK 5455, supported by this repo's SDK.
Adjustable [field of view](docs/fov.md) is available through `fov` in `settings.toml` (70 is the original view).
See [docs/PLAN.md](docs/PLAN.md) for the milestones.

## Repository layout

| Path | What |
|---|---|
| `sdk/` | XboxXBE-Recomp, vendored and patched here (base commit in `sdk/UPSTREAM.txt`) |
| `config/game.toml` | Recompiler config for `default.xbe`: symbols, data, hooks |
| `src/common/` | Host code shared by every image (`game_info.cpp` describes the game to the runtime) |
| `src/game/` | Hooks for `default.xbe` (`X86_FUNC(sub_XXXXXXXX)` overrides) |
| `tools/` | `xiso.py` (list/extract XDVDFS images), `xbe_info.py` (XBE header, XDK versions, sections), `dev.bat` (toolchain environment), RE helpers `disasm.py`, `lib_inventory.py`, `symbols_compare.py`, `hle_audit.py`, `dsp_image_check.py` (see [docs/xdk5455.md](docs/xdk5455.md)) |
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

## Credits

- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) and [BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp)
  by TGP482, which this port is built on and modelled after. See `sdk/README.md` and `sdk/THIRD_PARTY.txt`.

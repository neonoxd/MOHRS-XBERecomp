# Work plan

The game's XBE is statically recompiled to C++ by the XboxXBE-Recomp SDK (vendored in `sdk/`), the Xbox kernel and
XDK libraries are implemented on the host, graphics go through the SDK's Direct3D 12 renderer and audio through
XAudio2. Goal: a faithful, stable 1:1 port first, enhancements after. Session status lives in `HANDOVER.md` (local,
untracked).

## The game

From `tools/xbe_info.py` and `tools/xiso.py`:

| | `default.xbe` | `moh3rdc.xbe` |
|---|---|---|
| Title | Medal of Honor: Rising Sun | Medal of Honor Rising Sun (Coop) |
| Title ID | `4541003F` (EA), region `0x7`, version 97281 | same |
| SHA-1 | `9f5197f8ded3ce1cd3e1c1cae14a756a17c39d56` | `a9d9a2b524a8bf37d28d8e7d0004ddbee847a78e` |
| Built | 2003-10-20 (timestamp `0x3F9367A3`) | 2003-10-20 |
| Entry | `0x1A685F` | `0x19FC1F` |
| `.text` | 1.7 MB (`0x11000`-`0x1BB7DC`) | 1.7 MB |
| XDK | 5455 for XAPILIB, D3D8, DSOUND, XGRAPHC, XBOXKRNL, LIBCMT | same |
| Library sections | D3D, DSOUND, XGRPH, XPP, DOLBY | same |

- Reference ISO: SHA-1 `4f26cfa5dc80c679d103e9bbc5099e053dd15338`, 4.49 GB, game partition only (XDVDFS at offset 0),
  739 entries.
- No Bink, XMV, WMA, XNet or XOnline: no movie library or network stack to supply.
- `.XTLID` lists 87 library exports (41 D3D, 45 DSOUND), usable by `xbe_symbols` as a fallback.
- Not LTCG (library names are `D3D8`, not `D3D8LTCG`), so `[abi]` overrides are rare.
- Data is in EA formats decoded by game code: `.viv` (BIG archives), `.xsh` (shapes/textures), `.mad` (EA MAD video,
  53 files), `.ast`/`.mus`/`.mpf`/`.abk`/`.bnk` (audio), `.loc`/`.stf` (localisation), `.rtc`, `.ubr`. Movies go
  through ordinary D3D texture uploads and DirectSound, not a host decoder.
- Between the front end and missions the game relaunches `default.xbe`, carrying state in `Z:\MohLoader.bin`. Two
  Player Campaign relaunches into `moh3rdc.xbe` (SDK image switching). Saves go to `UDATA`.
- The game renders 4:3 only; the host pillarboxes it, or widens it (Hor+) with `widescreen`.

The SDK base (0.1.0, commit `994fdd1`) supported XDK 4831 and 5849 only; 5455 support was added here, see
[xdk5455.md](xdk5455.md). [BLACK-XBERecomp](https://github.com/TGP482/BLACK-XBERecomp) is the reference port.

## Milestones

| | Milestone | Status |
|---|---|---|
| M0 | Project setup: repo, vendored SDK, ISO/XBE tools | Done |
| M1 | Toolchain (Clang/LLD in VS 2022, offered by `build.bat`) and SDK tools | Done |
| M2 | XDK 5455 symbols, layout, signatures, runtime review | Done, no runtime change needed |
| M3 | Recompile and link | Done: 12287 functions, 303 jump tables; needed an SDK fix for switch bounds held in a register and `data_ranges` for the library data tails |
| M4 | Boot to the main menu | Done: legal movie, Pearl Harbor intro, mission 1_1, front end, pause, saves |
| M5 | Rendering correctness | Done for 1_1, 1_11, 1_2, 1_3, 1_4; 1_5, 1_7, 1_8, 1_9 not toured; issues fixed as found |
| M6 | Audio: EA streams and banks, 3D sound, ADPCM, mix bins, the reverb effects image | Plays; no full-mission check for crackle, drift or missing voices yet |
| M7 | Complete single-player playthrough, save games across restarts, 30 FPS timing as on the Xbox | Open: campaign not played through |
| M8 | Co-op executable `moh3rdc.xbe` (`MOHRSCoop.exe`, `config/coop.toml`, `src/coop`) | Done; the return from co-op to `default.xbe` has not been confirmed in a log |
| M9 | Release packaging (`release.bat`) | Done since v0.1.0; not checked: a crash dump from the zip, a PC without Visual Studio |

Rendering test tools: `tools\start_level.ps1`, `tools\level_tour.ps1`, settings `skip_intros` / `skip_cutscenes`,
`XBE_CAPTURE_DIR` + F3 frame captures, `XBE_D3D12_DEBUG`.

The release zip holds no game code: both executables, the compiler folder (without its work folder), the VC++
runtime, `d3dcompiler_47.dll`, licenses and `docs/player-readme.txt` as `README.txt`. The first start compiles
`MOHRS.dll` and `MOHRSCoop.dll` from the player's own XBEs.

## Enhancements

| Done | Doc |
|---|---|
| Keyboard and mouse, controller routing for split screen | [input.md](input.md) |
| Widescreen (Hor+), render scale, display mode, anisotropic filtering, skip intros/cutscenes | [player-readme.txt](player-readme.txt) |
| 60 FPS and unlocked frame rate | [frame-timing.md](frame-timing.md) |
| Field of view | [fov.md](fov.md) |
| PC Options screen, pause Cheats screen and mouse in menus | [menus.md](menus.md) |
| Quick save and load (testing) | [quick-save.md](quick-save.md) |
| Renderer CPU overhead | [performance.md](performance.md) |

Not started: MSAA/SMAA, draw distance, pause on focus loss. Texture dumping and replacement are in the SDK.

## Risks

| Risk | Mitigation |
|---|---|
| 5455 behaviour the runtime assumes from 4831/5849 (CDevice layout, push buffer, DSound structures) | Every layout value proven from 5455 disassembly; 5455 changes keyed on the config's `xdk` value |
| Upstream SDK moves on while the copy here is patched | `sdk/UPSTREAM.txt` records the base commit; SDK changes go in separate commits |

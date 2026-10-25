# Work Plan: Medal of Honor: Rising Sun (Xbox) recompiled

The port follows the same route as BLACK-XBERecomp: the game's XBE is statically recompiled to C++ by the
XboxXBE-Recomp SDK (vendored in `sdk/`), the Xbox kernel and XDK libraries are implemented on the host, and the
game draws through the SDK's Direct3D 12 renderer and plays through XAudio2. The goal is a faithful, stable 1:1
port first; enhancements come afterwards in a separate phase.

## What we start from

### The game (from `tools/xbe_info.py` and `tools/xiso.py`)

| | `default.xbe` | `moh3rdc.xbe` |
|---|---|---|
| Title | Medal of Honor: Rising Sun | Medal of Honor Rising Sun (Coop) |
| Title ID | `4541003F` (EA), region `0x7`, version 97281 | same |
| SHA-1 | `9f5197f8ded3ce1cd3e1c1cae14a756a17c39d56` | `a9d9a2b524a8bf37d28d8e7d0004ddbee847a78e` |
| Built | 2003-10-20 (timestamp `0x3F9367A3`) | 2003-10-20 |
| Entry | `0x1A685F` | `0x19FC1F` |
| `.text` | 1.7 MB (`0x11000`-`0x1BB7DC`) | 1.7 MB |
| XDK | **5455** for XAPILIB, D3D8, DSOUND, XGRAPHC, XBOXKRNL, LIBCMT | same |
| Library sections | D3D, DSOUND, XGRPH, XPP, DOLBY | same |

- ISO SHA-1 `4f26cfa5dc80c679d103e9bbc5099e053dd15338`, 4.49 GB, game partition only (XDVDFS at offset 0), 739
  entries.
- No Bink, no XMV, no WMA, no XNet/XOnline libraries: no movie library or network stack to supply on the host.
- `.XTLID` lists 87 library exports (41 D3D, 45 DSOUND ids), which `xbe_symbols` can use as a fallback.
- Not LTCG (library names are plain `D3D8`, not `D3D8LTCG`), so `[abi]` overrides should be rare.
- The data is EA's own formats, all decoded by game code: `.viv` (BIG archives), `.xsh` (shapes/textures), `.mad`
  (EA MAD video, 53 files), `.ast`/`.mus`/`.mpf`/`.abk`/`.bnk` (EA audio, streamed and banked), `.loc`/`.stf`
  (localisation), `.rtc`, `.ubr`. Movies therefore go through ordinary D3D texture uploads and DirectSound streams,
  not through a host decoder.

### The SDK (XboxXBE-Recomp 0.1.0, commit `994fdd1`)

- Supports XDK builds **4831** (007 NightFire) and **5849** (Splinter Cell DA, Black, 007 FRWL) only:
  `sdk/xdk/<build>/signatures.toml` (function fingerprints, no library bytes) and `layout.toml` (CDevice and
  global offsets, each proven by an instruction address).
- `runtime/xdk/d3d8.cpp`, `dsound.cpp` and `gpu/state.h` state they implement 4831 and 5849. 5455 sits between them,
  so offsets and some entry points will differ from both.
- `dsp_image.cpp` recognises effects by their decrypted DSP code as seen in 5849 images; the 5455 DOLBY/effects image
  may differ.
- Image switching (relaunching into the exe of another XBE) is built in, which the co-op XBE will use.

### Reference port: BLACK-XBERecomp

Shows what a finished game repo looks like: `config/game.toml` with ~250 symbols, ~20 data addresses, `[abi]` entries
and ~40 `[[hook]]`s; game hooks in `src/game`; the SDK's D3D, DirectSound and renderer moved into the game repo once
they needed game-specific changes. Its history shows the order the work went in: boot, hooks, sound fixes, input,
frame-rate independence, then options.

## Milestones

Each milestone has an exit check. Status lives in `HANDOVER.md` (local, untracked).

### M0 - Project setup (done)
Repo, vendored SDK (`sdk/`, upstream recorded in `sdk/UPSTREAM.txt`), game template configured for MOHRS (`MOHRS.exe`,
`assets/xbe/default.xbe`), ISO and XBEs in `assets/`, `tools/xiso.py` and `tools/xbe_info.py`, this plan.
- Exit: `git log` has the initial commit; `python tools/xbe_info.py assets/xbe/default.xbe` prints XDK 5455.

### M1 - Toolchain and SDK build (done)
- Install Clang/LLD into VS 2022 (`build.bat` offers it on first run).
- Configure with the `release` preset and build the SDK tools: `xbe_recomp`, `xbe_symbols`.
- Exit: both tools build; `xbe_symbols` runs on `default.xbe` and stops with "no signatures for these library builds".

### M2 - XDK 5455 support in the SDK (done)
Symbols, layout, signatures and the runtime review are done, see [xdk5455.md](xdk5455.md). No runtime source change
was needed for MOHRS.
1. Seed `sdk/xdk/5455/signatures.toml` from 5849 (and 4831 for comparison) and run `xbe_symbols`: byte hashes will
   mostly miss, but constant (`k`) matching and `.XTLID` ids should find a good share.
2. Make a flat PE (`sdk/tools/symbols/xbe2pe.py`) and identify the misses in Ghidra, library by library: D3D8,
   DSOUND, XAPI/XPP (pads, files, threads), CRT math. Fill `config/game.toml` `[symbols]`/`[data]` by hand.
3. Write `sdk/xdk/5455/layout.toml`: every CDevice/global offset the runtime reads, each with the 5455 instruction
   address that proves it (same standard as 4831/5849).
4. Diff the D3D8/DSOUND entry points the runtime implements against 5455 behaviour (argument conventions,
   structure layouts, render state numbering in `gpu/state.h`), and patch the runtime where 5455 differs.
5. Generate the real signatures: `xbe_symbols --sign config/game.toml --out sdk/xdk/5455/signatures.toml`.
- Exit: `xbe_symbols` on a clean checkout writes a complete config for `default.xbe` (and `moh3rdc.xbe`) with only
  symbols that genuinely don't exist in the image left at 0.

### M3 - Recompile and link (done)
Status: `MOHRS.exe` and `MOHRS.dll` build (12287 functions, 303 jump tables; about 2 s per partition). Needed an SDK
fix for switch bounds held in a register and `data_ranges` for the library data tails; see the M3 log entry in
`HANDOVER.md`.
- `xbe_recomp` generates C++ for the whole image; fix analysis problems with `[[function]]`, `[[switch]]` and
  `data_ranges`; add any x86/x87/SSE instruction the recompiler lacks (in `sdk/tools/recomp`).
- Exit: `build.bat` produces `MOHRS.exe` and `MOHRS.dll` with no errors.

### M4 - Boot to the main menu (done)
Status 2026-10-25: boots through the EA legal movie and the long Pearl Harbor intro into mission 1_1 (the game's
`game.cfg` flow on a fresh profile). Played by the user with a controller: mission 1 completed, main menu and pause
menu work, audio plays. The game relaunches `default.xbe` between front end and missions (state in
`Z:\MohLoader.bin`) and saves to `UDATA`; both work. No errors in the release log, no crash.
- Kernel/XAPI calls up to the first frame: threads, events, file I/O from the ISO (`.viv` archives), the save
  partition, D3D device creation, the DOLBY/DSP image.
- Replace trapping stubs as they are hit; log unimplemented exports.
- Exit: EA/logo sequence plays (MAD video) and the main menu is navigable with a controller.

### M5 - Rendering correctness (done; issues fixed as they come up)
Status 2026-10-25: MOHRS draws 4:3 only and is now shown pillarboxed at 4:3 instead of stretched. Mission 1 played
through, levels 1_11, 1_2, 1_3, 1_4 toured: no rendering problems found (the 1_2 river checked by the user against
other versions). 1_5, 1_7, 1_8, 1_9 not toured; per the user, rendering issues get fixed when they show up.
Test tools: `tools\start_level.ps1`, `tools\level_tour.ps1`, settings `skip_intros` / `skip_cutscenes`.
- Vertex programs, pixel combiners and fixed function as MOHRS uses them; texture formats (swizzled, DXT, palettes);
  render targets and post effects; fog; video playback surfaces; immediate mode draws; gamma.
- Tools: `XBE_CAPTURE_DIR` + F3 frame captures, `XBE_D3D12_DEBUG`, the harness reference renderer.
- Exit: menus, loading screens and the first missions render like the Xbox (compared against xemu captures).

### M6 - Audio
- DirectSound buffers and streams for EA audio (music `.mus`/`.mpf`, speech/ambience streams, banked SFX), 3D
  positional sound, ADPCM, mix bins, and the 5455 effects image (reverb).
- Exit: music, dialogue and SFX play without crackle, drift or missing voices through a full mission.

### M7 - Complete single-player playthrough
- Controller, rumble, save/load (profiles and checkpoints on the emulated HDD), every mission start to finish,
  split-screen modes in `default.xbe`, the 30 FPS timing as on the Xbox.
- Fix crashes, hangs and logic differences found on the way (x87 precision, CRT math, timing).
- Exit: the campaign is completable start to finish with no crashes; save games survive a restart.

### M8 - Co-op executable (`moh3rdc.xbe`)
- `config/coop.toml` from `xbe_symbols --name coop`, a second `add_xbe_image(MOHRSCoop xbe/moh3rdc.xbe coop.toml ...)`,
  `src/coop` hooks, `kGame.images` entry, and image switching between the two executables in both directions.
- Exit: co-op mode launches from the menu, plays a mission, and returns to `default.xbe`.

### M9 - Release packaging
- `release.bat` zip with no game code (built on the player's PC from their own XBE), README for players, version
  resource, crash dumps checked.
- Exit: a fresh folder with only the zip + the user's ISO builds and runs the game.

### Phase 2 - Enhancements (after M9, scoped then)
Following BLACK as a reference: output resolution and widescreen/aspect, unlocked frame rate (needs every
30 FPS-tied system made time-based), field of view, keyboard and mouse, MSAA/SMAA/anisotropic filtering, draw
distance, skip intros, pause on focus loss, texture dumping/replacement (already in the SDK).

## Risks

| Risk | Mitigation |
|---|---|
| XDK 5455 differs from 4831/5849 in ways the runtime assumes (CDevice layout, push buffer, DSound structures) | Prove every layout value from 5455 disassembly; diff against both known builds; keep 5455 changes behind the config's `xdk` value so the SDK still serves other games |
| Manual symbol hunting takes long | Constant matching + `.XTLID` first; group by library; use BLACK/5849 names as a map |
| EA engine specifics (MAD video, streamed audio, frame pacing) | Capture xemu reference behaviour; hook at game level as BLACK does |
| C: has ~23 GB free; ISO 4.5 GB, generated code and objects several GB | Watch `out/` and `generated/` sizes; move the build dir if needed |
| Upstream SDK moves on while we patch our copy | `sdk/UPSTREAM.txt` records the base commit; keep SDK changes in separate commits so they can be offered upstream or re-applied |

# Menus: PC Options, Cheats and mouse control

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

## Cheats

The pause screen's **Preferences** gets a **Cheats** entry under PC Options (`Preferences.fng` only, so single
player and co-op; not the multiplayer `MPPreferences.fng`). The screen is made from the pause archive's `HUD.fng`
like PC Options, with four On/Off rows. Left/right switch a row at once, confirm keeps the rows, back puts them as
they were on entry. The switches last for the session and are not written to `settings.toml` or the profile.

| Row | What it does |
|---|---|
| Give All Weapons | Every player weapon of the level, with full reserve ammo, given once per level once the player holds a weapon. In single player the other campaign levels' weapons too |
| Infinite Ammo | The game's unlimited ammo cheat: firing leaves the clip as it is |
| God Mode | The game's invincibility cheat; health is refilled when it comes on |
| One Shot Kills | The game's one-shot cheat: every hit on an AI character does 1000 damage |

Infinite Ammo, God Mode and One Shot Kills are the console's own unlockable cheats, each a byte the game reads where
it applies. The cheat getter (`0x127600` / `0x121C20`, index 0 to 11) and setter (`0x1276B0` / `0x121CD0`) list
all twelve. The game sets the bytes from the profile at a level's start (`0x9F710` / `0x92090`, not in co-op or
multiplayer) and clears them on a profile load or reset (`0x133CC0`, `0xA0E70`). `src/common/cheats.cpp` sets each
byte every frame while its row is on, at the main gameplay frame's entry (`0xA4A90` / `0x98A40`, once a frame in
a level and in the shell, paused too), and puts back the game's value when the row goes off. In a multiplayer match the bytes are left alone.

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Invincibility byte (cheat 10), read by the invulnerable test | `0x20EDBC` | `0x206F9C` |
| Invulnerable test (fastcall player: `+0xD4E` bit 4 or the cheat) | `0xD6210` | `0xCAD20` |
| Full health (fastcall player), called when invincibility comes on | `0xD7620` | `0xCC300` |
| Unlimited ammo byte (cheat 1), read by the weapon fire | `0x20EDBD` | `0x206F9D` |
| Weapon fire (fastcall weapon): `clip--` unless the byte is set | `0x1506C0` | `0x149A90` |
| One-shot byte (cheat 2), read by the AI hit handler | `0x20EDC7` | `0x206FA7` |
| AI hit handler: damage = 1000 (`0x1E0E78`) when set | `0xE9E30` | `0xE1E30` |
| Multiplayer match byte | `0x20EDB1` | `0x206F91` |
| Player list, player count | `0x270108`, `0x1FC658` | `0x265278`, `0x1F54B8` |

The one-shot cheat sets the damage of every hit on an AI character, whoever fired it, so allies die in one hit too.

### Give All Weapons

A player's weapon ring holds a holder for every player weapon in the level's object database (class `0x21`
entries, looked up by weapon hash in the template table at `0x270460`, 0x60 bytes each, built by `0xCB5E0`). The
player's setup adds each with the holder insert (`0xDF320` / `0xD3400`, thiscall player, weapon hash), which sorts
the ring by WeaponType. A holder the player does not own has flag `+0x3C0` bit 2 clear. The level's give-weapon
script native (`0x39E60` / `0x39A00`) finds the holder by hash, adds ammo and sets the bit.

Give All Weapons does the same for every holder of WeaponType 1 to 8 (not tank turrets, mounted guns or melee):
ammo up to the definition's most (`+0x12`) through the give-ammo call, the bit set, and the clip filled through the
reload call for a weapon newly owned. It runs once per player when the row comes on and once after each rebuild of
the ring (the holder insert is hooked), and waits until the player holds a weapon, so a level's unarmed start or
scene is not armed early.

A level's archive (`level.viv`) carries only its own weapons' files and its object database only its own holders;
the shared definitions (`weapbul_*.dat`, all 65 weapons) are not enough, as the holder insert returns no holder
for a hash the level has no template for. Weapons from other levels are added below.

### Weapons from other levels

In single player every campaign level (`data\1\1_N\level.viv`) is read from the disc image with the other
campaign levels' player weapons added, whether the cheat is on or not, so they are in the ring like the level's own:
unowned, as a level's weapons are until its scripts give them, until Give All Weapons gives them. Switching the row on
in a level gives them at once. `MOHRS_LEVEL_WEAPONS=0` reads the levels as they are on the disc. The switches are kept
in the `MOHRS_CHEATS` environment variable, which the instance a level or the front end relaunches into inherits.

The archive is built on the host (`src/common/level_archive.cpp`, `tests/level_archive_test.cpp`; sounds `src/common/level_sound.cpp`,
`tests/level_sound_test.cpp`) and served through
the SDK's disc file provider (`kernel::SetDiscFileProvider`): open, size queries and reads of that path see it. The
game loads, enumerates and frees it as its own, which matters: file data outside an archive's buffer is freed by
the game's heap (`0x112F20`).

- Each weapon comes from the first of 1_2, 1_3, 1_5, 1_7, 1_9, 1_1, 1_4, 1_8 holding it (the multiplayer maps have
  low detail meshes and no weapon scripts). The fire extinguisher is left out: its animations are only in 1_1's
  bank.
- Database (`levelfile.lfc`): a header (version, ?, count, table offset), a table of {class, field count, field
  array}, fields {type, count, values}; types 0 (file) and 4 (name) hold string offsets. All offsets are from the
  file's start, which the loader `0x114370` adds in place. The new database keeps the old bytes and puts a new
  table (the old entries, then the new) and the new entries' fields after them. Added per weapon: its class `0x21`
  entry, a class `0x9` entry for its script, `0x8` for the script's instance (`.sin`), `0xA` for its animation
  states (`.aso`). The script is the `.cbs` whose base (header `+0x24`) is `PlayerWeapon_Base` and which holds the
  weapon hash; the bazooka has none.
- Files: every file the weapon entry names (skeleton, first person mesh, crosshair and overlay, textures, `.aso`)
  and the script and instance, when the level lacks them. All are fetched by name (`0x114120` → archive lookup
  `0x1132E0`, a linear scan); `.xsh` files are also registered by walking the archive (`0x10D570`, `0x10CFD0`).
- World models are not added. A `.msh` is a static mesh (loader `0x10D6F0`, from `0xEB2E0` in the object step):
  the files it names, then EAGL blocks whose texture context comes from the level (`0x113360`). Another level's
  breaks the load (the EAGL loader `0x172660` reads a section table past the block), and the loader does not check
  for a missing one (null at `0x10D6F0+0xA7`). An added weapon's entry names the level's first weapon's world model
  instead, so a dropped or third person added weapon shows as that one. The weapon definitions (`weapbul_*.dat`)
  name world models a level lacks too; they are skipped there, as long as the file is not in the archive.
- Holder pool: `0xCB5E0` sizes the object pools from the settings entry (class `0x1B`): `0xD1E90(0x408, field 57)`
  is the weapon holders' (6 when unset, as in every campaign level; the multiplayer maps set 16). The holder
  insert fails past it, so the merged database points field 57 at a new record of the level's weapons plus the
  added plus 5.
- Archive: `C0FB`, directory size less 4 and count (16-bit big endian), then a 3-byte offset, 3-byte size and name
  per file; files start on 0x80. Offsets are 3 bytes, so an archive stays under 16 MB (1_2's grows to 15.6 MB).
- Animations: first person animations are in the level's `anim.bnk`, an EAGL ELF whose `AnimationBank` names them
  `S_0x<CRC-32 of the lower case name>`. The archive gets multiplayer map 2_1's bank (every player weapon's, 0.9 MB)
  as `mohrs_weapons.bnk`. After the level's bank loads (`0x68BD0`, a level load step in the table at `0x1FF54C`),
  the hook runs `0x68BD0` again with the bank globals pointed at the added bank and the hash table emptied, keeps
  the EAGL object and its hash → index table, and puts the level's back. A lookup by hash the level's bank misses
  (`0x68CE0` with bank 0) is made again with the added bank and the index (`0x68CE0`'s explicit bank path,
  `0x689B0`). `0x68BA0` releases it with the level's.
- Sounds: a weapon's sounds are events named `WpnFire_<name>` (`0x100CA0`), `WpnSlct_<name>` and `WpnRld_<name>`,
  each also with `_Zone<n>`, the name from the table at `0x202740` by the definition's `+0x60` (Colt_45,
  Springfield, Model_11, Sten_Silenced, ...), hashed with CRC-32 and played by `0x108370`; its animation states
  (`.aso`) name its foley events by hash. A level's events are in `sound\<m>_<l>.ubr` ("UBER": a header with the
  column count `+0x10`, record count `+0x14` and size `+0x18`, columns {name hash, type, offset}, records sorted by
  the type 6 hash column, which `0x107C20` binary searches, then a trailer). Column `0x7FFF5F24` is a bit mask of
  the banks the sample is in (`0xFE190`: bit n-1 bank n, 31 the script bank `<m><l>scr.abk`, which stays loaded)
  and `0xBC1069FF` the sample's index in it. A level has only its own weapons' events, all in its script bank. The
  provider also serves the level's `.ubr` and script bank with the added weapons' events from the level each came
  from: the records converted to the level's columns by name (1_7 and 1_8 have other column sets), their samples
  appended to the bank. A bank (`BNKl`, version 5) is a header (count `+6`, header size `+8`, data size `+0xC`), an
  offset to each sample's `PT` header from its own place, the headers (tags: byte, length, big endian value; `0x88`
  and `0x89` the absolute offsets of a sample's data and second channel), then the data; codec `0x04` samples keep
  their data in the header part, which moves with the headers. About 50-90 events and 1.1-1.5 MB per level.
- Memory: the main heap is one 48 MB `XPhysicalAlloc` (`0x1A60FA`) from `0x10FFD0`, given to the EA memory manager
  over its `XPhysicalSize`. The game's later physical allocations (D3D, sound) need about what is left of a 64 MB
  console, so the SDK gives the guest a development kit's 128 MB and the block is made larger at boot by up to
  12 MB, keeping 8 MB free (`MOHRS_HEAP_EXTRA_MB` sets it, 0 off); the larger block is tried with the SDK's
  allocator first, as the game stops on a failed one. The XDK's D3D masks physical addresses to 28 bits. An archive grows by at most that less 3 MB, a
  weapon at a time. Co-op has no heap hook, so it gets no weapons from other levels.

| | default.xbe |
|---|---:|
| Level load steps (`{1, function}`), the bank load among them | `0x1FF54C` |
| Animation bank load, lookup (cdecl out, hash or index, bank), release | `0x68BD0`, `0x68CE0`, `0x68BA0` |
| Bank in memory (data, size), its EAGL object, its `AnimationBank` | `0x390B3C`, `0x390B40`, `0x269CC8`, `0x269CC0` |
| Hash → index table (8 bytes each, 0x400 room), count | `0x267CC0`, `0x267CB8` |
| Level archive file (cdecl name, size out) | `0x114120` |
| Database loader, template table build | `0x114370`, `0xCB5E0` |
| XPhysicalAlloc (stdcall size, address, alignment, protection), main heap setup | `0x1A60FA`, `0x10FFD0` |

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Holder insert (thiscall player, hash; ret 4) | `0xDF320` | `0xD3400` |
| Give-weapon script native | `0x39E60` | `0x39A00` |
| Give ammo (thiscall weapon, amount; ret 4) | `0x150170` | `0x149540` |
| Reload (fastcall holder) | `0xE2520` | `0xDA640` |
| Current weapon (fastcall player, 0 for none) | `0xD6560` | `0xCB2A0` |
| Player: ring head | `+0xE70` | `+0xF50` |
| Weapon: definition, WeaponType | `+0x2F0`, `+0x310` | `+0x300`, `+0x320` |
| Holder: weapon `+0x2BC`, next `+0x2B4`, flags `+0x3C0` | same | same |

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

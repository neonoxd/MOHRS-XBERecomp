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
screen in the game's menu art laid out like HUD Options. Its first row, **Category**, picks what the rows under it
show (left and right go round the seven):

| Category | Rows | `settings.toml` |
|---|---|---|
| Display | Display Mode (Fullscreen, Borderless, Windowed; applied on confirm) | `display_mode` |
| | Render Scale (50% to 200% of the output height) | `render_percent` |
| | Frame Rate (30 FPS, 60 FPS, Unlocked) | `fps60`, `unlocked_fps` |
| | FPS Limit (Monitor, 30 to 480, Off) | `fps_cap` (1 Monitor, 0 Off; Off and higher stop at 2400) |
| | VSync (On, Off) | `vsync` |
| | Field of View (45 to 140 in steps of 5, 70 original) | `fov` |
| | Widescreen (On, Off) | `widescreen` |
| | Keep Bodies (On, Off; [keep-bodies.md](keep-bodies.md)) | `keep_bodies` |
| Mouse | Sensitivity X, Sensitivity Y (0.1 to 5.0) | `mouse_sensitivity`, `mouse_vertical_sensitivity` |
| | Invert X, Invert Y (On, Off) | `invert_mouse_x`, `invert_mouse` |
| Move Keys | Move Forward, Move Back, Strafe Left, Strafe Right, Jump, Crouch, Walk | `[keys]` |
| | Walk Speed (10% to 100%) | `walk_speed` |
| Combat Keys | Fire, Aim, Lean, Reload, Grenade, Melee, Action | `[keys]` |
| Weapon Keys | Change Weapon, Primary Weapon, Pistol, Special Weapon, Next Weapon, Previous Weapon, Sniper Zoom In, Sniper Zoom Out | `[keys]` |
| Look Keys | Look Up, Look Down, Look Left, Look Right, Center View | `[keys]` |
| Other Keys | Objectives, Pause, D-Pad Left, D-Pad Right, Quick Save, Quick Load | `[keys]` |

The key rows are `[keys]`' actions (`move_forward`, ..., [input.md](input.md#keyboard-keys)), showing the keys
bound, "None" for none. Left/right change a value row at once (display mode only on confirm). A press (A, Enter,
a click) on a key row shows "Press a key"; the next key or mouse button (the wheel included) pressed after every
held key is let go becomes the action's only key, and leaves every other action bound to it, but for the wheel,
which a weapon step and a sniper zoom may share. Escape or a controller's B cancels, Backspace leaves the action
with no key. Enter cannot be bound. Confirm (A, Enter on a value row) writes `settings.toml`; back (B, Backspace)
restores every row and key as it was when the screen opened. Settings without a row stay file-only. The category
shown is kept while the game runs.

Display Mode is applied on the window thread (`app::ApplyNow`, `sdk/runtime/app/main.cpp`), at startup and when
the confirmed mode differs from the one the screen opened with. A display mode change from Fullscreen at another
resolution is undone first, and the monitor's rects are read again at the desktop mode. Borderless and Fullscreen
give the window `WS_POPUP` over the whole monitor (Fullscreen first switches the monitor to `width` x `height`
when they differ from the desktop's), Windowed gives `WS_OVERLAPPEDWINDOW` with an output-sized client area
centred in the work area. The taskbar only checks whether a window covers its monitor when that window is
activated, and the window is already active when the mode changes in game, so each apply also tells the taskbar
directly (`ITaskbarList2::MarkFullscreenWindow`, on for Borderless and Fullscreen, off for Windowed). The window
thread is a single-threaded COM apartment for this.

- `src/common/options_package.{h,cpp}` builds `PCOptions.fng` from the archive's `HUD.fng`: the Crosshairs row is
  the pattern for ten value rows (name, value, a button group with flashing arrows) and, at the nine places under
  the first, nine key rows on the same lines (`PC Key <n> text`, `value`, `button`). A key row's button answers
  `Button_Pressed` with a copy of its `Button_Highlight` answer (run its Flash script) and a second response, type 2
  with `Button_Pressed` as its value, which sends the message to the game and so to the handler, as the package's
  own answers to `Pad_Left`/`Pad_Right`/`Pad_Accept`/`Pad_Back` do. The controller path (`0x94330`) sends a press
  to a focused object that answers it and `Pad_Accept` to the package otherwise, so a press on a key row reaches
  the handler instead of the package's accept and exit; the mouse sends it to the object the same way
  (`src/common/menu_mouse.cpp`). The HUD rows are removed, the title becomes PC OPTIONS and the focus/highlight
  targets point at the new buttons. It adds the entry to `Option_overlay.fng` (between Audio and Difficulty, moving
  the entries below down a row) and to `Preferences.fng` / `MPPreferences.fng` (under HUD Options) by copying the
  Audio entry with its path pointing at `PCOptions.fng`.
- `src/common/options_menu.{h,cpp}` holds the pages. The factory gives `PCOptions.fng` the HUD screen's handler
  class (vtable `0x1ED40C`), whose event function hands that package's events over: `HUD Setup` fills the rows,
  `Pad_Left`/`Pad_Right` step the focused row (FEPackage `+0x80`, the object's name hash at `+0x10`) or the
  category, `Button_Pressed` starts a key row's wait, `HUD Config Confirm` saves, `HUD Config Revert` restores. Up
  and down are the game's own navigation: the directional search (`0x88630`) passes over buttons with object flag
  `0x04000000` (`+0x1C`), which the game's button disable sets and its enable clears. For each place, the page's
  value row or key row is enabled and the other disabled, its name and value emptied; a place with neither has
  both disabled. The menu mouse finds no disabled object.
- The wait (`src/common/mouse_keyboard.h`): every pad's report reads as released, the keyboard's and the
  controllers' (`game::Pad`), the menu mouse takes no clicks and the quick save keys do nothing. The FEng update
  (`0x95220` / `0x87B80`) asks for the result before it runs; the key is bound at once (`LoadBindings`) and the
  keys held then act only once let go.

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Archive file lookup (cdecl: archive, path, flags) | `0x1132E0` | `0x10BDA0` |
| Screen handler factory (cdecl: package name) | `0x120F00` | `0x11B1B0` |
| HUD screen handler's event (thiscall, ret 0xC) | `0x12A620` | `0x124C20` |
| SetText (stdcall: package, object, ASCII text; grows the text's buffer) | `0x138A40` | `0x131D10` |
| RunScript (stdcall: package, object, script) | `0x138330` | `0x131600` |
| Button enable, disable (stdcall: package, object; flag `0x04000000` cleared, set) | `0x138B30`, `0x138B50` | `0x131E00`, `0x131E20` |
| Directional button search (thiscall button array: from, direction, interface, mask) | `0x88630` | `0x7AFB0` |
| Per-package controller handling | `0x94330` | `0x86C90` |
| Object's message answer lookup (thiscall object: message; list `+0x3C`, hash at `+0xC`) | `0x958D0` | `0x88270` |
| Menu sound (cdecl) | `0x123C10` | `0x11DC90` |

The screen is English in every language.

## Cheats

The pause screen's **Preferences** gets a **Cheats** entry under PC Options (`Preferences.fng` only, so single
player and co-op; not the multiplayer `MPPreferences.fng`). The screen is made from the pause archive's `HUD.fng`
like PC Options, with eight On/Off rows, Quick Reload (Off, 2x, 3x, 4x) and Custom Movement (Off,
Quake). Left/right change a row at once, confirm keeps the rows, back puts them as
they were on entry. The switches last for the session and are not written to `settings.toml` or the profile.

| Row | What it does |
|---|---|
| Give All Weapons | Every player weapon of the level, with full reserve ammo, given once per level once the player holds a weapon. In single player the other campaign levels' weapons too |
| Infinite Ammo | The game's unlimited ammo cheat: firing leaves the clip as it is |
| God Mode | The game's invincibility cheat; health is refilled when it comes on |
| One Shot Kills | The game's Silver Bullet cheat: every hit on an AI character does 1000 damage |
| Bullet Shield | The game's Bullet Shield cheat: bullets that reach the player spark and do no harm |
| Achilles Head | The game's Achilles Head cheat: hits on AI characters do no damage but to the head |
| Invisible Soldiers | The game's Invisible Soldiers cheat: the characters' bodies are not drawn |
| Rubber Grenades | The game's Rubber Grenades cheat: grenades bounce |
| Quick Reload | Every reload of the player's weapons takes a half, a third or a quarter of its time |
| Custom Movement | The players move on foot by Quake's movement rules, with a speed counter; their own grenades and rockets do them no damage and push them away (rocket jumps) |

All rows but Give All Weapons, Quick Reload and Custom Movement are the console's own unlockable cheats, each a byte the game reads where it applies,
every time it applies, so each takes effect from the next frame. The cheat getter (`0x127600` / `0x121C20`, index 0 to 11) and setter (`0x1276B0` / `0x121CD0`) list
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
| Silver Bullet byte (cheat 2), read by the AI hit handler | `0x20EDC7` | `0x206FA7` |
| AI hit handler: damage = 1000 (`0x1E0E78`) when cheat 2 is set, 0 when cheat 7 is set and the part hit is not 0 or 1 | `0xE9E30` | `0xE1E30` |
| Bullet Shield byte (cheat 3), read by the player's bullet contact | `0x20EDC4` | `0x206FA4` |
| Player's bullet contact: an impact spark and no hit when set | `0xD23E0` | `0xC6E90` |
| Rubber Grenades byte (cheat 4), read by the grenade update and its contact | `0x20EDC6` | `0x206FA6` |
| Grenade update, grenade contact | `0x14DE80`, `0x14EAB0` | `0x147250`, `0x147E80` |
| Achilles Head byte (cheat 7), read by the AI hit handler | `0x20EDC8` | `0x206FA8` |
| Invisible Soldiers byte (cheat 8), read by the character draw | `0x20EDCA` | `0x206FAA` |
| Character draw: the body skipped when set | `0xE9860` | `0xE18C0` |
| Multiplayer match byte | `0x20EDB1` | `0x206F91` |
| Player list, player count | `0x270108`, `0x1FC658` | `0x265278`, `0x1F54B8` |
| Player's current holder; its weapon | `+0xE6C`; holder `+0x2BC` | `+0xF4C`; holder `+0x2BC` |
| Weapon flags (bit 0: reload due or under way), reserve, clip | `+0x340`, `+0x2FC`, `+0x2FE` | `+0x350`, `+0x30C`, `+0x30E` |
| Reload start script native: the bit set (`0x150220` / `0x1495F0`) | `0x2E360` | `0x2D9F0` |
| Reload end script native: the clip filled from the reserve, the bit cleared (`0x150270` / `0x149640`) | `0x2E3E0` | `0x2DA70` |
| Animation graph update (thiscall graph, time; ret 4), the graph at actor `+0x54` | `0x69BD0` | `0x5C5A0` |

The Silver Bullet and Achilles Head cheats change every hit on an AI character, whoever fired it, so they apply to
allies too.

The other cheats are not rows. Unlock All Missions (cheat 0, `0x20EDCC` / `0x206FAC`, read by script native
`0x40940`) and All Replay Items belong to the front end. Men With Hats (cheat 5, `0x20EDC5` / `0x206FA5`) is read
only by the character template build at a level's load (`0xCB170`), which gives each character a random hat. Cheat
6 (`0x20EDBE` / `0x206F9E`) hides the first-person weapon (`0xD7810`, `0xE33B0`). Cheat 9 (`0x20EDC9` /
`0x206FA9`) makes every hit on the player do 1000 damage unless invincibility is on (`0xD7E90`, `0x14B020`). Cheat
11 (`0x20EDBF` / `0x206F9F`) is read once, in `0xDB530`.

### Quick Reload

A reload is the `Reload` animation played on the player's current holder, between two script natives: the start
sets bit 0 of the weapon's flags and the end fills the clip from the reserve and clears it (the weapon state update
`0x151320` / `0x14A6A0` also sets the bit when the clip runs dry). Each actor holds its animation graph at `+0x54`,
and the graph update takes the frame's time. With Quick Reload on, the update of a player's current holder's graph is
given its time multiplied by 2, 3 or 4 while the bit is set and the reserve is not empty, so the animation and its
events (the reload sounds) play that much faster. Other actors, the player's body and the
holder outside a reload keep their time. The factor is kept in the `MOHRS_RELOAD` environment variable (1 to 4) for
the instance a level or the front end relaunches into, like the other switches.

### Custom Movement

On foot, a player's velocity follows Quake's movement rules instead of the game's: `src/common/movement_rules.h` has
QuakeWorld's (`pmove.c`). Each frame: the jump, ground friction (stop speed 100), acceleration toward the wished
direction (10) on the ground or air acceleration with the wished speed capped at 30, and gravity, in Quake's units
(an inch, 0.0254 of the game's meter). Quake's friction of 4 is doubled to 8: at the game's scale its stop from a
run took 1.7 units and slid. A held jump hops again on every landing. The game keeps its collision, stepping,
crouching, fall damage and animation.

| | |
|---|---:|
| Run speed (forward and back, side) | 320 (400, 350) |
| Gravity | 800 |
| Jump speed, height | +270, 45.6 |
| Off the ground going up faster than | 180 |
| Crouched move | the game's 0.25 |
| Clamp on each velocity component | 2000 |

While the rules move the first player, the overlay shows their speed across, in Quake's units a second ("320 ups"),
under the middle of the picture (`src/common/game_info.cpp`). Only the first player's speed is shown, in co-op too; it
goes when no update has set it for 250 ms (the pause screens, the shell, off foot).

The game's player update (`0xDB530` / `0xD4BF0`, player vtable `0x1E9100` / `0x1E1D18` slot `+0x10`, thiscall
(ticks), ret 4) sets the local velocity (`+0x240` strafe, `+0x244` forward, `+0x248` up) from move actions 6 and 7:
on the ground straight from the sticks (4 units/s, `0x201E88`; crouched a quarter, `0x1E1008`), in the air added
to (`0x1E8EF0`), the jump adding 0.0649 a tick (`0x201E84`, about 3.9 units/s) when its ground test passed. The ground
test is a ray down from the position by the collision shape's length (`+0x180`, its method `+0x44`) and 5 cm
(`0x1F2E9C`), or a floor touched in the last collision. The world
velocity (`+0x230`) is the local one in the orientation's rows (`+0x50` right, `+0x60` forward, `+0x70` up). The move
step (`0xE0E50` / `0xD8DD0`, slot `+0x1C`) takes gravity, 0.002728 a tick squared (9.8 units/s^2, `0x1E84C4` /
`0x1E1098`), off the world z velocity unless the last collision touched a floor (`+0xD44` bit 1, set by the contact
handler `0xDEFA0` / `0xD3040` for a normal z above 0.642), clamps it to +-0.3 a tick and moves by it. The game's
ticks are 1/59.94 s.

`src/common/custom_movement.cpp` keeps a velocity per player across frames. Before the update it takes from the
last move what the collision allowed: when the player moved less than asked across the ground, the part of the
velocity along what was lost (into the wall) is taken away, as Quake's clip against a plane leaves the part
along it, and a ceiling stops the rise. A move that rose more than asked (10 inches/s), a stair step or a slope,
keeps the velocity. A push the update adds (an explosion's, `+0x250` with `+0xD45` bit 5) is added to it. At the update's
ground test (`0xDB7C4` / `0xD4EB9`, bl the result, esi the player) it reads actions 6, 7 and 10 and runs the rules;
the jump action the update then reads is the rules' jump, so the game's jump sound and animation go with it. After
the update the world and local velocities are written.

The actions are read from the input manager (`0x271188` / `0x266618`): an entry of `0x8F8` bytes per player index
(none when its port byte is `0xFF`), 12 bytes per action from `+0x7C`. A type byte comes first; for an edge type
(bit 1), such as the jump, `+4` holds the held value and `+8` is 1 only on the frame of the press, while the others
have their value at `+8` (input update `0xB8820`). The query (`0xB83E0` / `0xACC80`) reads `+8`. The rules take the
jump as held, so holding it hops on every landing, and a press keeps for 0.15 s, so a tap in the air (between two
steps going down) jumps on the next ground. On the ground the written move also goes down as fast as it goes across,
to 300 inches/s, so the player follows a slope or stairs down instead of leaving them, the floor pushing back the
part into it; for 0.06 s after walking off a step edge (not after a jump) the player is still on the ground. In the air the move step's gravity is given back before it
takes it, the rules having taken their own; on the ground it stays, settling the player onto the floor.

Up a slope the written move instead rises along the floor: pushed down into it, the collision resolve pushes the
player back out along the floor's normal, and on a 45 degree slope (1_2, normal z 0.70, which the game still counts
as floor) that undid the whole move, so the clip took the speed away every frame and the player could not climb it,
at high frame rates not at all. The floors come from the player's contact handler (`0xDEFA0` / `0xD3040`, player
vtable `+0x24`, thiscall player, pair): its contact points are a list at pair `+0x1C` (next `+0x2C`), each with a
normal at `+0`, `+4`, `+8` pointing away from the pair's first object (negated when that is the player), touching when
its depth `+0x20` is below 0; a normal z above 0.642 (`0x1E8ED0`) is floor and sets `+0xD44` bit 1. A stair's edge
is a contact that is not floor (a 1_2 stair's: normal z 0.53), and pushed down into it the player was put back where
it stood each step; at about 900 FPS, where a frame's acceleration is 3 inches/s, it never got over.

The faces kept are the touching contacts whose normal z is above 0.01, from the steps since the last update, or for
0.1 s after the last (a move along a slope lifts the player just clear of it in some steps). Of those the move goes
into (`n.x * x + n.y * y` below 0 for a move across of (x, y)):

- normal z 0.45 or more (slopes, stair edges): the move rises along the steepest, `-(n.x * x + n.y * y) / n.z`, at
  most twice the move across;
- less (down to 0.01): the move stays level and the collision lifts the player out, as the game's own walking does
  (1_2 has a 72 degree face, normal z 0.31, beside the top of that stair that a rise along it would climb).

When the move goes into none, it goes down as above.

The rules are off for a player on a ladder (`+0xD45` bit 0), on a vehicle, mounting or in a scripted move (`+0xD46`
bits 0, 4, 6), on a mounted gun (`+0xD44` bit 4), frozen (`+0xD4C` bit 2) or stunned (`+0xDC0` / `+0xEA4` above 0),
in co-op while `+0xE44`, `+0xE45` or `+0xD4E` bits 6-7 are set, in multiplayer and in the shell; the game's
velocity is then its own, and the rules take over from it again. Input comes only while `+0xD4C` bit 0 is set. The
game's collision resolve (`0xD8260` / `0xCD360`) limits its push out of the geometry to 0.3 units a tick. Quake's
edge friction (more friction over a drop ahead) is not done. The choice is kept in `MOHRS_MOVEMENT` for the
next instance; `MOHRS_MOVEMENT_TRACE` logs each update's move, the rules' frame and each blast's push.

#### Rocket jumps

While the rules are on, a player's own grenades and rockets do them no damage and push them as Quake's do. A
projectile's explosion (`0x14F770` / `0x148B40`, grenades and the bazooka) builds an explosion object
(`0x14A8B0` / `0x143BF0`): `+0x20` its centre, `+0x34` its damage, `+0x38` the damage type, `+0x3C` who fired it
(the projectile's `+0xE0`: in default.xbe the firing weapon's owner at weapon `+0x360`, set by the weapon's setup
`0x150460`, which for the player's weapons is the player object). The projectile's vtable (`0x1F2A80` /
`0x1EB8D0`) gives them: damage `+0x16C` / `+0x164`, type `+0x170` / `+0x168`, owner `+0x174` / `+0x16C`. The game applies it to each object
in reach with the explosion's vtable slot `+4` (`0x14B020` / `0x144360`, thiscall (target), ret 4): a
line-of-sight test from the centre (`0x14ABE0` / `0x143F20`, true when blocked), a falloff from 1 to 0.5 across the
radius, then for a player (its vtable `+0xB8` returns itself) zero damage under God Mode or 1000 under cheat 9, the
damage message (`0x28`), the controller rumble and the camera shake. The game pushes no player with an explosion
(the player's impulse, `+0x250` with `+0xD45` bit 5 through vtable `+0x114` `0xD6450`, comes only from script
natives).

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Explosion's apply to one target (explosion vtable slot `+4`) | `0x14B020` (`0x1F2880`) | `0x144360` (`0x1EB6D8`) |
| Its line-of-sight test | `0x14ABE0` | `0x143F20` |
| View shake start (thiscall player: amplitude, ramp up, ramp down, hold) | `0xD6670` | `0xCB3B0` |
| Its shake slot, the slot's start | `+0xD58`, `0xD5F70` | `+0xD70`, `0xCAA30` |
| View shake and recoil update | `0xD6700` | `0xCB440` |

The apply starts the player's view shake with (falloff, 0, 1, 10): amplitude 2 × the falloff, up in a tick, held 10
ticks, down in a tick. Each step of the update (once a console frame, `docs/frame-timing.md`) adds a random jolt of
up to ±0.25 × the amplitude, in radians, to the view offsets `+0x284`/`+0x288` (added to the pitch and yaw), so
point blank the view jumps by up to 0.5 radians a step.

When the target is the one who fired it and the rules own its velocity, the apply runs with the damage at 0 (the
message and rumble as with God Mode) and the shake's amplitude at 0.15 of the game's, and unless the line of sight is blocked the rules' velocity gets
Quake's push (`T_RadiusDamage` of 120 halved for one's own, `T_Damage`'s 8 a point): `4 * (120 - d / 2)` inches a
second away from the centre, `d` the distance in inches from the centre to the player's position (`+0x80`), none
past 240. A blast 28 inches below the position pushes 424 up; with a jump that reaches 301 inches (7.6 m). Players the rules do not
move (on a ladder, a vehicle, a gun) take no damage and the lighter shake from their own blasts, and are not pushed.

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
  the EAGL object and the `AnimationBank`, and puts the level's back. The two are kept in a guest block allocated
  when the main heap is (`0x10FFD0`, at the same point of every boot), so a quick load puts them back too: a run
  that loads a save from the shell never loaded the added bank, and its lookups came back empty (a null animation
  read in `0x68A60`). The hash → index table is made from the `AnimationBank` (count `+4`, names at `[+0x14]`, as
  `0x68BD0` reads them) whenever it changes. A lookup by hash the level's bank misses (`0x68CE0` with bank 0) is
  made again with the added bank and the index (`0x68CE0`'s explicit bank path, `0x689B0`). `0x68BA0` releases it
  with the level's.
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

## Mission time

The end-of-level stats (`EndOfLevelSummary.fng` in the pause archive) get a **Mission time** row under Hits, shown
as `m:ss.mmm` (`h:mm:ss.mmm` from an hour). The game already counts the time and never shows it: the game state's
`+0x31760` is zeroed at level start and each unpaused gameplay frame adds its whole 1/60 s ticks, except while player
1's `+0xD4C` has bit `0x2` set, so the milliseconds step by 1/60 s. Its unused getter returns seconds. A quick save
restores it with the rest of memory.

`src/common/mission_timer.{h,cpp}` reads and formats it. With `mission_timer` (HUD Options' Mission Timer, below) the
overlay shows it in the picture's top right corner, the FPS counter's size and style, right-aligned on the
width of the widest text of its length so it does not shift as the digits change. The gameplay frame (`0xA4A90` /
`0x98A40`, paused too) publishes the time at its entry unless the front-end byte (`0x2463B1` / `0x23E5B1`) is set;
the overlay draws it only while a publish is under a quarter second old, so it is not shown while loading, in
movies, on the end-of-level screens (their modal loop does not run the gameplay frame) or in the front end. It
shows the paused time in the pause screen.

HUD Options (`HUD.fng`, the shell's and the pause screen's) gets a **Mission Timer** row (On, Off) under HUD Type,
and under it an **FPS Counter** row (On, Off; `fps_counter`, frames a second in the picture's top left corner):

- `menu::AddHudRows` (`src/common/options_package.cpp`) copies the Crosshairs row two row pitches down (the pitch is
  HUD text's Y minus Crosshairs text's: 32 in the pause screen, 37 in the shell) as `Mission Timer text` (text
  `Mission Timer`, label `MISSION_TIMER`, not in the string tables), `Mission Timer On` / `Off` (the game's `ON` /
  `OFF` texts, so localised) and `Mission Timer button` with its arrows, and adds the button to Crosshairs button's
  highlight target lists; the same three pitches down as `FPS Counter ...` (label `FPS_COUNTER`), after the
  Mission Timer button. The archive hook gives each folder its own copy.
- The screen's handler (`0x12A620` / `0x124C20`) runs Crosshairs and HUD Type by the focused button's name hash and
  logs any other button as an unknown option. `MenuEvent` takes Pad_Left/Pad_Right while `Mission Timer button` or
  `FPS Counter button` has the focus: the switch flips, the handler's toggle sound plays (1 on, 0 off) and RunScript Show/Hide picks the On or
  Off text. Every other event goes to the handler first; after `HUD Setup` the rows are shown and remembered, after
  `HUD Config Confirm` a change is written to `settings.toml`, after `HUD Config Revert` the rows are put back.
- The menu mouse (`src/common/menu_mouse.cpp`) maps the rows' text, On and Off to their buttons, like the game's rows.

The end-of-level stats:

- `menu::AddMissionTime` (`src/common/options_package.cpp`) copies the Hits label and value as `Mission Time label` /
  `Mission Time value` one row under Hits (the row pitch is Head's Y minus Hits'), with the label text `Mission time`
  (label `MISSION_TIME`, not in the game's string tables, so the text stays). Every top-level object on screen below
  Hits (the seven body-part rows and the Medal Awarded text) moves down one row: Right arm ends at y 130 and the
  medal text at 158, above the outro's button bar (187). The copies keep Hits' Init/Show/Hide scripts, so they
  appear with the other rows.
- `MenuStats` (`src/common/options_menu.cpp`) runs the stats fill and, for the level's stats (cumulative byte 0),
  sets `Mission Time value` with SetText. The package's `Score choreo` Init sends `Update Player Stats Level
  Complete`, whose handler calls the fill with the medals byte set. The fill is also called for `Update Player
  Stats` (cumulative and medals 0) and for the profile's `Get Cumulative Stats` (cumulative 1, the shell's
  `StatsScreen.fng`, left alone). SetText skips a package without the object.

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Stats fill (thiscall handler: package, cumulative, medals, 2 more; ret 0x14) | `0x136D00` | `0x12FFD0` |
| Stats screen event (Get Cumulative Stats, Update Player Stats [Level Complete]) | `0x1373B0` | `0x130680` |
| Game state pointer | `0x2447D8` | `0x23C9D0` |
| Mission time add (thiscall: player, ticks), from the gameplay frame | `0x9F310` | `0x91C90` |
| Mission time zeroed at level start (`0x120F0` -> `0xA1570`) | `0xA17F2` | `0x941B2` |
| Mission time getter, seconds (unused) | `0x9D500` | `0x8FE80` |
| Level stats (13 values from game state `+0x31744`), cumulative stats | `0x9D520`, `0x9D600` | `0x8FEA0`, `0x8FF80` |

## Controller Options

Controller Options (`ControllerOptions.fng`, the shell's and the pause screen's) gets a **Modern Aiming** row (On,
Off) under Invert Y Axis, which switches [Modern Aiming](input.md#modern-aiming):

- `menu::AddAimRow` (`src/common/options_package.cpp`) copies the Invert Y Axis row one row pitch down (the pitch is
  Invert Y Axis text's Y minus Vibrate text's, over three rows: 38 in the shell, 32 in the pause screen) as
  `Modern Aiming text` (label `MODERN_AIMING`, not in the string tables), `Modern Aiming On` / `Off` and
  `Modern Aiming button` with its arrows, after Invert Y Axis button, and adds the button to Invert Y Axis button's
  highlight target lists. It shares the copy with the Mission Timer row.
- The screen's handler (`0x127FD0` / `0x1225D0`, vtable `0x1ED4B4` / `0x1E6384`) keeps its rows in the handler
  (`+0x50` crosshairs, `+0x51` vibrate, `+0x52` grenade cook, `+0x53` invert, `+0x54` sensitivity, `+0x58` turn
  speed), reads them from the profile on `Controller Setup` and writes them back on `Controller Options Confirm`;
  any other button logs an unknown option. `ControllerMenuEvent` (`src/common/options_menu.cpp`) does the same for
  its row: `Controller Setup` reads the setting and shows the row, left and right on `Modern Aiming button` flip it
  (toggle sounds 1 on, 0 off; RunScript Show/Hide on the On and Off texts) without passing the event on, and
  `Controller Options Confirm` sets the setting and writes `settings.toml` if it changed.
- The menu mouse maps the row's text, On and Off to its button.

## Mouse in menus

- Hover focuses an entry, left click selects it, the wheel moves through entries. A stationary pointer leaves
  focus to keyboard/controller navigation.
- On settings screens (PC Options, stock Audio, HUD, controller options) clicking a label or value focuses its row,
  even when its arrows are hidden; the arrows or the wheel change the value. A click on a PC Options key row waits
  for its key. The displayed confirm hint saves, the
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
- Controller and native mouse paths send `Button_Pressed` to a focused object that handles it, else `Pad_Accept`
  to the package, and send a copy with target `0xFFFFFFFB`.
- Queue targets (`0x93FD0`): `0xFFFFFFFB` goes to interface slot `+0x20`, a broadcast to listeners such as the menu
  sounds, which does not reach a screen handler; `0xFFFFFFFF` goes to slot `+0x1C`, which reaches the package's
  screen handler. An answer's response type 2 (`0x93370`) queues its value with target `0xFFFFFFFF`, type 3 with
  `0xFFFFFFFB`, type 0 runs the script named by its value.

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

`BlitFrame` (`sdk/runtime/gpu/d3d12.cpp`) fits a centred picture of `app::FrameAspect()` into the backbuffer (4:3,
or in widescreen the output's shape from 16:10 to 32:9, see [widescreen.md](widescreen.md)), and in widescreen
`Hud::ScreenX` (`src/common/widescreen.cpp`) compresses menu/HUD x around the centre by `1/widen`
(`widen = FrameAspect / (4/3)`). A client point is mapped to the displayed picture, then
`x = centre + (x - centre) * widen`, then into the object's space through the recorded render transforms. Points
outside the picture hit nothing.

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

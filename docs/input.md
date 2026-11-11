# Input: controller routing and mouse look

## Routing

- Singleplayer and ordinary front-end menus: keyboard/mouse (KBM) and the first connected controller both drive
  player 1.
- Two Player Campaign and split-screen multiplayer setup separate them automatically. At the join prompt, Enter
  joins KBM and A joins a controller. Joining KBM first makes it player 1; mouse aim follows KBM in either order.
- Gameplay, loading and pause keep the multiplayer assignment. Returning to the singleplayer/main menu shares input
  again. No setting or restart is involved.

Ports: KBM owns Xbox port 0 (the game's controller 1), leaving ports 1-3 for up to three physical controllers. The
first physical controller shares port 0 in singleplayer, while its own port 1 stays connected and sends a neutral
report; in multiplayer it sends only on port 1. The others keep ports 2 and 3. SDL slots keep their identity until
unplugged.

Port 1 stays connected in both modes so opening the join screen does not wait for XAPI's 250 ms device-discovery
poll. A held button or pushed stick must return to neutral before it acts on the first controller's new port, so a
mode change produces no duplicate actions; fresh buttons work immediately. Rumble follows the same assignment, and
the previous owner's motors are stopped on a mode change. Another player's input never moves KBM's cursor ownership.

### Implementation

`src/common/controller_routing.{h,cpp}` owns routing and the transition filter. The SDK's optional
`GameInfo::controller_port(port, rumble)` maps guest ports to SDL slots before reading input or setting motors (null
keeps the SDK mapping). The pad callback adds KBM to port 0.

The controller-update overrides publish the game's own mode flags on the guest thread, before that frame's input
update. Background XAPI reads use the host copy under a mutex and never read guest memory.

| Role | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Native controller update | `0x1616C0` | `0x15AA90` |
| In front-end shell (level 1_16) | `0x2463B1` | `0x23E5B1` |
| Shell co-op setup | `0x242435` | `0x23A615` |
| Shell multiplayer setup | `0x242436` | `0x23A616` |
| Gameplay co-op | `0x20EDB0` | `0x206F90` |
| Gameplay multiplayer | `0x20EDB1` | `0x206F91` |
| Player list | `0x270108` | `0x265278` |
| Input manager | `0x271188` | `0x266618` |

Inside the front end the shell flags win; otherwise the gameplay flags apply, including while paused. This needs no
heuristics (mouse activity, pad count, package names) and restores multiplayer routing from the co-op image's own
state after an executable switch.

In default.xbe, `0x1285C0` sets co-op setup before creating the two `MPJoin.fng` packages, `0x136640` sets
multiplayer setup for PvP and `0x12B5E0` clears both in `Setup First Menu`. Initialisation at `0x120F0` (co-op
`0x120B0`) derives the gameplay mode from the loader's saved game-settings flags.

Input manager entries are `0x8F8` bytes, each starting with its guest port; `+0x23E0` is the entry count. In
default.xbe `0xB8200` assigns the port, `0xB82D0` reads it and `0xB8280` reads the controller with it. Player
`+0x10DC` is a player index and `+0x10D8` a viewport index; neither identifies a physical controller.

## Keyboard movement

On foot the game sets velocity from move actions 6 (forward) and 7 (strafe) with no length clamp (`0xDD070`), so
speed is linear in each action. Each axis is mapped on its own: `|f| < 0.25` is zeroed (`0x1616C0`), the byte
`trunc((f + 1) * 127) - 128` loses a deadzone of 38 and the rest is stretched to 127 (`0xB8A16`, enabled by
`0x1FF855`; action table `0x1FF858`, co-op `0x1F7978`, same entries for 6 and 7). `MoveAxis` in
`src/common/mouse_keyboard.cpp` inverts that, so a held diagonal (each axis at 1/√2) and `walk_speed` give the
speed asked for. A plain scale of the stick value lands lower: 0.707 per axis moved diagonally at 79%, and a 50%
walk at 27%.

## Keyboard keys

Escape (the `pause` binding) presses Start in play and on pause pages, which pauses and resumes. In the front-end
shell (byte `0x2463B1`, co-op `0x23E5B1`, published by the controller-update override) it presses B instead, so it
goes back like Backspace rather than accepting. Which button a press sends is decided when the key goes down.

### Weapon keys

`primary_weapon` (1), `secondary_weapon` (2) and `special_weapon` (3) pick the KBM player's weapons by group
(`src/common/weapon_select.{h,cpp}`), using the weapon definition's WeaponType (definition `+0x68`, copied to the
weapon at `0x1504C3`):

| Key | WeaponType | Player weapons |
|---|---|---|
| Primary | 2 bolt rifle, 3 semi-automatic rifle or shotgun, 4 sniper, 5 automatic | M1 Garand, Springfield, shotgun, Thompson, Type 100, BAR |
| Secondary | 1 pistol | Colt .45, Welrod |
| Special | anything else: 6 launcher, 7 grenade, 8 thrown | Bazooka |

The game calls types 1-5 firearms (`0x150090`) and keeps the ring sorted by type as weapons are added (`0xDF320`),
which is why the switch steps pistol, rifle, automatic. A key picks its group's first usable weapon, or, when one of
the group is out, the next one after it, wrapping; a group with nothing else usable does nothing. The switch stays the
game's own, holster and draw included:

| Role | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| Action query (thiscall (player index, action), ret 8; 0 when not pressed) | `0xB83E0` | `0xACC80` |
| Switch action 0x12 read, then message 0x12 to the player's script | `0xDDCB0` | `0xD78E3`, `0xD7AC6` |
| Script native NextWeapon(direction), return address from the select | `0x33CD0`, `0x33D09` | `0x332C0`, `0x332F9` |
| Weapon select (thiscall (direction, id), ret 8) | `0xD8720` | `0xCD820` |
| Usable test (fastcall holder) | `0xE27E0` | `0xDA900` |
| Player: current holder, ring head | `+0xE6C`, `+0xE70` | `+0xF4C`, `+0xF50` |
| Weapon: id, WeaponType, counts | `+0x2F8`, `+0x310`, `+0x2FC/+0x2FE` | `+0x308`, `+0x320`, `+0x30C/+0x30E` |

Holders link through `+0x2B4` (next) and `+0x2B0` (previous), hold their weapon at `+0x2BC` and flags at `+0x3C0`
(bit 2: picked up). A weapon is usable when it is picked up, or for type 7 when its two counts are not both zero. The
select walks the ring in the given direction to the next usable holder whose weapon id matches (any, for id 0); with
a current holder and no match the walk never ends, so a match must exist before an id is passed.

A key press is held for 250 ms. The action query override answers the switch action as pressed for the player on
port 0 when the key's group has another usable weapon and the player is not on a gun (`+0xD46` bit 0; `0x10` is
only the way onto one). A level starts with no current holder (`0xE0020`); the game's switch then equips from the
ring's head, and a key equips its group's first. The override remembers that weapon's id for two seconds. The select override gives that id to the next select NextWeapon makes for the same
player, if the weapon is still usable; any other select is left alone. A key pressed again before that select moves
the remembered weapon on in its group without starting a second switch (the script would step past it afterwards),
and never to the weapon out: the select given that id puts it away and leaves the player unarmed. Weapon definitions
come from `weapbul_<easy|medium|hard|mp>.dat` in each level archive (loader `0x151EC0`, names hashed with CRC-32,
`0x10EB60`). Number keys used to be zoom (1, 2) and dpad left/right (3, 4); a settings file with neither
`primary_weapon` nor the first build's `weapon_1` that still has those defaults gets the new ones (wheel only, 4
and 5).

## Mouse look

Raw mouse counts are added to the KBM player's angles before the native stick integration; the game then applies
its own limits, wrapping, camera and gun transforms. The hook looks up which player the input manager assigned to
port 0 before consuming counts, so it works with KBM as player 2. `src/common/mouse_look.{h,cpp}`.

Both images share the on-foot yaw/pitch fields (`+0x274/+0x278`); later player fields differ:

| Field or hook | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| On-foot look hook | `0xE0AEE` | `0xD8A1E` |
| Mounted look hook | `0xDC911` | `0xD6151` |
| Gun yaw / pitch | `+0xE1C/+0xE20` | `+0xEFC/+0xF00` |
| Gun yaw / pitch limits | `+0xE14/+0xE18` | `+0xEF4/+0xEF8` |
| Zoom (half FoV in degrees) | `+0xE30` | `+0xF10` |

The mounted hooks sit after the call to the player's recoil update (`0xD6700` / `0xCB440`) and before the angle
loads and integration. In co-op, `0xD614C` calls `0xCB440`, `0xD6151` starts integration, `0xD6162` addresses gun yaw
and `0xD6174` adds gun pitch; the following branches enforce the limits, including two players on one shared gun.
The zoom fields are updated in the player update (`0xDB530` / `0xD4BF0`).

On a mounted gun (player `+0xD46` bit 0) the normal look function skips its mouse hook, so each image needs its own
mounted hook. Without it, aim is lost and `TakeMouse` is not called; its timestamp keeps `input::Playing()` true
(250 ms) and enables mouse button bindings.

## Tests

```powershell
cmd /c "tools\dev.bat cmake --build out\build\release --target MOHRS MOHRS_code MOHRSCoop MOHRSCoop_code -- -j 12"
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/controller_routing_test.cpp src/common/controller_routing.cpp src/common/mouse_look.cpp -o re/controller_routing_test.exe"
& re/controller_routing_test.exe
```

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keyboard_mapper_test.cpp -luser32 -o re/keyboard_mapper_test.exe"
& re/keyboard_mapper_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/weapon_select_test.cpp src/common/weapon_select.cpp -o re/weapon_select_test.exe"
& re/weapon_select_test.exe
```

The mapper test drives the real `Keyboard()` with faked keys: Escape as Start in play and pause, as B in the front
end and latched while held, weapon key presses once per press and only in play, the moved dpad keys. The weapon test
runs both overrides on a ring sorted like the game's: each group, cycling and wrapping within it, weapons not picked
up and empty grenades skipped, a second press before the select, another player's port, mounted and unarmed players,
selects from elsewhere, a weapon lost before the select, no current holder, and bit `0x10`. With `XBE_LOG_INFO`
set the log says why each key press switched or did not.

The routing test covers shared singleplayer input, joining in either order, both setup flags, co-op/PvP gameplay and pause,
return to the main menu with stale gameplay flags, held-input suppression, unplug/reconnect, extra pads, rumble
ownership, both mounted-gun layouts and zoom scaling. Not covered by automated tests: play with physical hardware
(swapping devices mid-mission, joining in co-op and PvP, pausing from each player, KBM on the 5_4 howitzer in co-op).

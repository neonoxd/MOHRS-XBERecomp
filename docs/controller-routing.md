# Controller and KBM routing

Singleplayer and ordinary front-end menus let keyboard/mouse and the first connected controller operate player 1.
Entering Two Player Campaign or split-screen multiplayer setup separates them automatically. At the join prompt,
press Enter for KBM and A on each controller. Join KBM first to make it player 1; mouse aim also follows KBM when
joined in a different order. Gameplay, loading and pause retain the multiplayer assignments. Returning to the
singleplayer/main menu restores shared input. No setting or restart is needed.

KBM reserves Xbox port 0 (shown as controller 1 by the game), leaving ports 1-3 for three physical controllers.
The first physical controller shares port 0 in singleplayer; its dedicated port 1 remains connected but sends a
neutral report. In multiplayer it sends input only on port 1. The other two controllers keep ports 2 and 3.
Physical SDL slots keep their identity until unplugged. This setup supports KBM plus up to three controllers.

Keeping port 1 connected in both modes avoids a delay waiting for the XAPI's 250 ms device-discovery poll when
opening the join screen. The pad report filter prevents duplicate actions. A held button or pushed stick must
return to neutral before it can act on the first controller's new port; unrelated fresh buttons still work.
Rumble follows the same assignment, with the old owner's motors stopped on mode changes. Another player's
controller input does not switch KBM's cursor ownership.

## Implementation and evidence

`src/common/controller_routing.{h,cpp}` owns the routing and transition filter. The SDK's optional
`GameInfo::controller_port(port, rumble)` callback maps guest ports to SDL slots before reading input or setting
motors. A null callback retains the SDK's original mapping. The existing pad callback adds KBM to port 0.

Ghidra analysis of both local XBEs identified the native mode flags below. The controller-update overrides publish
these flags on the guest thread, before that frame's original input update. Background XAPI reads use the host
routing state under a mutex and never read guest mode variables.

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

The shell flags take priority inside the front end; otherwise the gameplay flags apply, including while paused.
This avoids relying on mouse activity, a gameplay timeout, the number of connected pads or visible package names.
It also restores multiplayer routing from the co-op image's own native state after an executable handover.

In default.xbe, `0x1285C0` sets co-op setup before creating the two `MPJoin.fng` packages. `0x136640` sets
multiplayer setup for PvP. `0x12B5E0` clears both during `Setup First Menu`. Initialisation at `0x120F0`
(`0x120B0` in co-op) derives gameplay mode from the loader's saved game-settings flags.

The input manager's entries are `0x8F8` bytes; each starts with its assigned guest port and `+0x23E0` holds the
entry count. Default.xbe `0xB8200` assigns the port, `0xB82D0` reads it, and `0xB8280` uses it to read the native
controller. Mouse look checks this table to find the player assigned to KBM's port 0, including on mounted guns.
Player object `+0x10DC` is a player index, and `+0x10D8` is a viewport index; neither is a physical-controller ID.

## Validation

Both executables and game-code DLLs build with:

```powershell
cmd /c "tools\dev.bat cmake --build out\build\release --target MOHRS MOHRS_code MOHRSCoop MOHRSCoop_code -- -j 12"
```

The host integration test needs no game launch, input injection, physical pad or player save access:

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/controller_routing_test.cpp src/common/controller_routing.cpp src/common/mouse_look.cpp -o re/controller_routing_test.exe"
& re/controller_routing_test.exe
```

It exercises shared singleplayer input, independent KBM/controller joining, both multiplayer setup flags,
co-op/PvP gameplay and pause routing, return to main menu with stale gameplay flags, held-button/stick suppression,
fresh buttons during suppression, unchanged-mode polling, unplug/reconnect, extra pads, rumble ownership and mouse
aim in either join order. The existing menu pointer/adapter tests also pass.

Interactive verification remains: swap between KBM and a controller during a mission; join KBM and a controller in
both co-op and PvP setup; move/fire/aim independently in the match, pause from each player, then return to
singleplayer. The automated checks do not constitute an end-to-end play test with physical hardware.

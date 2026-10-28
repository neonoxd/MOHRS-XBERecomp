# Mouse look and mounted guns

Raw mouse counts go directly into the KBM player's angles before the native stick integration. The game then
applies its usual angle limits, wrapping, camera and gun transforms. The input manager assigns KBM to guest
port 0; the hook checks that assignment before consuming counts, including when KBM joins as player 2.

The two XBEs share the on-foot yaw/pitch fields (`+0x274/+0x278`), but their later player fields differ:

| Field or hook | default.xbe | moh3rdc.xbe (co-op) |
|---|---:|---:|
| On-foot look hook | `0xE0AEE` | `0xD8A1E` |
| Mounted look hook | `0xDC911` | `0xD6151` |
| Gun yaw / pitch | `+0xE1C/+0xE20` | `+0xEFC/+0xF00` |
| Gun yaw / pitch limits | `+0xE14/+0xE18` | `+0xEF4/+0xEF8` |
| Zoom (half FoV in degrees) | `+0xE30` | `+0xF10` |

The mounted hooks follow calls to the player's recoil update (`0xD6700` / `0xCB440`) and precede the angle loads
and integration. In co-op, `0xD614C` calls `0xCB440`, `0xD6151` starts integration, `0xD6162` addresses gun yaw,
and `0xD6174` adds gun pitch. The following branches enforce the native limits, including the special case when
both players mount a shared gun. The zoom fields are updated in the same player update functions (`0xDB530` /
`0xD4BF0`). These addresses and offsets were checked against the local XBEs and their Ghidra disassembly.

The level 5_4 howitzer exposed a missing co-op mounted hook: the normal look function skips its mouse hook when
player `+0xD46` bit 0 indicates a mounted gun. Only default.xbe had a `TurretLook` hook. Besides losing aim, co-op
stopped calling `TakeMouse`, whose activity timestamp keeps `Playing()` true and enables mouse button bindings.
The co-op hook now calls the same adapter with the co-op offsets. Normal co-op mouse zoom scaling also uses the
correct FoV field.

Validation: `tests/controller_routing_test.cpp` covers both layouts, zoom scaling, both KBM join orders, controller
ownership, and polling with no mouse motion while mounted. Build both executables and game-code DLLs with the
command in [controller-routing.md](controller-routing.md). In-game validation remains: mount the 5_4 howitzer,
aim and fire with KBM, dismount, and check a controller still works independently.

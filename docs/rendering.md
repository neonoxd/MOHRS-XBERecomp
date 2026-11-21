# Rendering fixes

## Clear texels write no depth

The game sets its render states from state blocks. The block apply (thiscall block) sends each field that differs from
its cache, and it is the only place the game sets alpha test. Model draws reach it from the render command stream
(`0x184240`, a command that applies a block from the table at `[0x39D17C]`).

| | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| State block apply | `0x17D010` | `0x1763E0` |

| Block field | Render state |
|---|---|
| `+0x10` | Z func |
| `+0x18` (byte) | alpha test enable |
| `+0x1C` | alpha ref |
| `+0x20` | alpha func |
| `+0x24` (byte) | textures on (off unbinds all four stages) |
| `+0x28` | alpha blend enable: 0 off, 1 on, other values leave it |
| `+0x30`, `+0x34`, `+0x38` | blend op, source blend, destination blend |
| `+0x48` | Z write enable; -1 takes the device setting's `+0x85` (on in game) |

Character meshes run blocks from their materials with blend op add, source alpha / inverse source alpha, Z write -1
and alpha test off. A texel of alpha 0 then adds no colour but still writes depth. A sleeve patch (for example the
sergeant's chevrons, a 64x64 P8 texture whose palette alpha is 0 around the insignia) is a mesh of its own, drawn before
the uniform under it: the uniform failed the depth test inside the patch's square, and what was behind the soldier
showed there. The console does the same.

`ApplyStateBlock` (`src/common/transparent_depth.cpp`) overrides the block apply. A block with alpha test off, Z write
1 or -1, blending on with op add, source alpha and destination inverse source alpha or one is applied with alpha test
on, func greater and ref 0; the block's fields are put back after the call, and the apply's caches hold what the device
was sent. With those blend factors a texel of alpha 0 leaves the target as it was, so only its depth (and stencil)
write is dropped. Partly clear texels (hair edges) still write depth, as before.

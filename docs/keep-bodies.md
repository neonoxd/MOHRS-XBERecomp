# Keep Bodies

`keep_bodies` in `settings.toml` (default off) or PC Options > Display > Keep Bodies: dead AI soldiers stay where
they fell until the level needs their slot for a new soldier. Off is the game's behaviour: a body goes a few seconds
after death (3.6 to 5.3 s were measured). Both executables. The level's soldier limit, the pools and the scripts'
spawn checks are the game's own; the setting only changes when a body is removed.

## How the game removes a body

Each AI soldier has a record, made by the spawn. It owns the soldier's brain (script, `+8`) and model (`+0xF8`);
`+0x14` is its hit points. The records are a list (next at `+0xC`) with a live count, and the spawn refuses a soldier
when the count is at the level's limit (game data: table `0x1B`, field `+0xB8`; 15 when unset). The same value sizes
the soldier model pool, so the limit also keeps the models in their pool.

| | default.xbe | moh3rdc.xbe |
|---|---|---|
| Spawn (cdecl) | `0x50450` | `0x537B0` |
| Record vtable | `0x1E21A4` | `0x1DB4C4` |
| Record list, count, limit | `0x25E9B0`, `0x25E9A4`, `0x25E9A0` | `0x256BE0`, `0x256BD4`, `0x256BD0` |
| Records start for a level (limit read) | `0x4E690` | `0x519F0` |
| Release (record vtable `+0`, thiscall updates) | `0xF8860` | `0xF11C0` |
| Deferred-delete map insert (thiscall object, updates; only if not queued) | `0xF8770`, map at `[0x282688]` | `0xF10D0`, map at `[0x277B18]` |
| Deferred-delete pass (each game loop update) | `0xF8680` | `0xF0FE0` |
| Its destroy call (`mov eax, [ecx]; call [eax+8]`) | `0xF86AF` | `0xF100F` |
| Record destroy (vtable `+8`, fastcall) | `0x4F990` | `0x52CF0` |
| Cull for queued spawns (cdecl spawns wanted) | `0x4EB20` | `0x51E80` |

About a second after death the soldier's script clears the model's team (native `0x2E230`: model vtable `+0x74`,
team at model `+0x124` = -1) and starts its fade (native `0x3D5B0` with 2: model vtable `+0x1B0`, `0xE57C0`, co-op
`0xDD940`, thiscall model, seconds). The fade is a fader at model `+0xFB0`: state (2 fading), elapsed, duration,
fraction 0.5 (`0xAD1A0`). Past fraction × duration (`0xAD200`) the soldier draw (`0xE9860`, model vtable `+0x34`)
sets the model's alpha (`+0xFE4`) to `1 - (elapsed - f·d) / (f·d)` (`0xAD2A0`), so the body fades to nothing.

Several seconds after death the soldier's script removes the soldier with a native (`0x34570`), which calls
`0x4EA50` (co-op `0x51DB0`, cdecl record): it gets the record's model (record vtable `+0x10`) and releases it
(model vtable `+0`, `0xCE3F0`, with 2 updates), which queues the model in the deferred-delete map and takes it out of
the world at once; then it marks the brain's script ended (`+0x24` = -1) and sends `0x17C`. The record falls due in
the deferred-delete map in the same frame (the release tells the record's watchers and queues it there). Each update the pass counts every entry down and, at zero, calls the object's
destroy and erases the entry. The destroy lowers the count, unlinks the record, takes the model out of the world and
frees model, brain and record.

A spawn refused at the limit is queued and tried again; while the queue is not empty the scripts' can-spawn check
(native `0x2E770`, co-op `0x2DD00`: queue empty and count below the limit) answers no. The cull makes room for queued
spawns: it sends `0x2A` to the farthest soldiers, `wanted + count - limit` of them.

## Implementation

`src/common/keep_bodies.{h,cpp}`, hooks in `src/game/hooks.cpp` and `src/coop/hooks.cpp`:

- The fade start (`0xE57C0` / `0xDD940`) runs, then with the setting on and the model a soldier model (vtable
  `0x1E99A0` / `0x1E25C0`) whose team is -1, the fader's state is set to 0: the draw never lowers the alpha. A living
  soldier's fade (a script fading one out) is left alone.
- `[[hook]]` `KeepBodyModel` at the model release in `0x4EA50` (`0x4EA60`, past it `0x4EA6F`; co-op `0x51DC0`,
  `0x51DCF`; esi the record): with the setting on and the record a dead soldier's, the release is passed over, so the
  model stays in the world and is not queued, and the record is kept. The rest of the removal (script ended, `0x17C`)
  runs as before. The record's destroy takes the model out of the world and frees it when the body goes.
- `[[hook]]` `KeepBody` at the pass's destroy call: with the setting on and the object a soldier record (its
  vtable) with hit points not above 0, the call is passed over (to `0xF86B4` / `0xF1014`) and the record is kept,
  oldest first. The pass erases its entry as usual. Everything else is destroyed as before.
- The map insert returns without inserting for a kept record, so it is never queued again and only Keep Bodies
  destroys it.
- A kept record still counts toward the limit. The oldest kept body is destroyed (through the record destroy, with
  the registers put back):
  - at the start of each pass while the count is at the limit, so a body never holds the last free slot past an
    update;
  - at the start of a spawn when the count is at the limit;
  - at the start of the cull, while `wanted + count - limit` is above 0, so the cull's room comes from bodies before
    it removes living soldiers.
- Before a body is destroyed it must still be in the record list, have the record vtable and hit points not above
  0, and not be in the deferred-delete map (the game destroys a queued one itself); one that fails is dropped. The
  kept list is emptied when a level's records start, and the record destroy override drops a record destroyed any
  other way.
- The map is a tree: `[map]` its head node, `[map + 4]` its size; a node has its parent at `+4`, children at `+8`
  and `+0xC` (0 for none), the object at `+0x10` and the updates left at `+0x14`; the head's parent is the root.
- A quick load puts back records the host's list does not know. After a load the next pass takes every dead soldier
  record in the list that is not in the map as a kept body, oldest first. With the setting off they all go at that
  pass, which also removes the bodies of soldiers who died in the few seconds before the save a little early.
- With the setting turned off, the next pass destroys every kept body.

## Tests

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keep_bodies_test.cpp src/common/keep_bodies.cpp -o re/keep_bodies_test.exe"
& re/keep_bodies_test.exe
```

Covers the setting off, which records are kept, the pass, spawn and cull releases (oldest first, registers put
back), bodies that left the list, are alive again or are queued in the map, finding the bodies after a quick load,
the level start and turning the setting off.
`MOHRS_CORPSE_TRACE=1` ([debugging.md](debugging.md)) logs each record destroy with its caller.

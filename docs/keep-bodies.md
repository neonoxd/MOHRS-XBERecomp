# Keep Bodies

`keep_bodies` in `settings.toml` (default off) or PC Options > Display > Keep Bodies: dead AI soldiers stay where
they fell for the rest of the level instead of fading out a few seconds after death. Up to 128 bodies a level (64 in
co-op); past that the oldest one goes. Both executables. The soldiers' records, scripts and places in the level's
soldier limit go at the game's own time; only the model stays, out of the world: it is drawn in the pose it died in,
and nothing else reaches it (shots and explosions pass through it).

## How the game removes a body

Each AI soldier has a record, made by the spawn. It owns the soldier's brain (script, `+8`) and model (`+0xF8`); the
level's soldier limit counts records (game data: table `0x1B`, field `+0xB8`; 15 when unset). The soldier models come
from a pool the model factory makes when the level's objects start (`0xCB5E0`), with exactly that limit's number of
models (`0x22E0` bytes each).

| | default.xbe | moh3rdc.xbe |
|---|---|---|
| AI soldier record vtable (hit points at `+0x14`) | `0x1E21A4` | `0x1DB4C4` |
| Record destroy (record vtable `+8`, fastcall) | `0x4F990` | `0x52CF0` |
| Its model removal, deactivation and free | `0x4F9CD`-`0x4F9F8` | `0x52D2D`-`0x52D58` |
| Soldier model vtable | `0x1E99A0` | `0x1E25C0` |
| Model fade (vtable `+0x1B0`, thiscall model, seconds) | `0xE57C0` | `0xDD940` |
| Model release (vtable `+0`, thiscall model, updates) | `0xCE3F0` | `0xC2EB0` |
| Its world removal | `0xCE3FD` | `0xC2EBD` |
| Model destroy (vtable `+8`) | `0xE5A50` | |
| Model deactivate (vtable offset) | `+0x168` | `+0x160` |
| World (thiscall this; first object `+0`, next `+0x1C`) | `0x270630` | `0x2657B0` |
| World removal (thiscall world, object) | `0xFA3E0` | `0xF2EB0` |
| Model factory, its soldier pool (`+0x10`, number at `+0`) | `0x270460`, `0x270470` | `0x2655E0`, `0x2655F0` |
| Pool creation (thiscall pool, size, count, flags) | `0xD1E90` | `0xC6940` |
| Model free (thiscall factory, model) | `0xCA7C0` | `0xBF070` |
| Pool slot walk (the slots' destroy) | `0xD1DB0` | |
| Soldier model allocation from the pool | `0xCB9D0` | `0xC0260` |
| Level's soldier pools start (in the records start) | `0x4E690` | |
| Collision object pool (`0x40` × 50: model `+0x114`, `+0x11C`), its take | `0x25EB68`, `0x4E550` | |
| Shape pool (`0x50` × 450: model `+0x110`, `+0x118`), its take, its free (cdecl shape) | `0x25EB4C`, `0x4EAB0`, `0x4E570` | `0x256D7C`, , `0x518D0` |
| Soldier model start (single player) | `0xE8CE0` | |
| Model deactivate | `0xE4CB0` | `0xDCE40` |
| Soldier model think, its AI controller update (model `+0x14D0`, vtable `+0x14`) | `0xE7970`, `0xE7BB9` | `0xDFA80`, `0xDFCC9` |
| World's draw (thiscall world, view), past its objects' loop | `0xF9460`, `0xF9560` | `0xF1F30`, `0xF2030` |
| Soldier draw (model vtable `+0x34`, thiscall model, view) | `0xE9860` | |
| Message post (cdecl target, message, data, sender, flag) | `0x4ADB0` | `0x4DF00` |
| AI controller (model `+0x14D0`): its entry on the watch list | `+0xD30` | `+0xD20` |
| Watch list head node; its join (thiscall entry), removal (cdecl entry) | `0x1FC7C0`; `0x28220`, `0x27D50` | `0x1F56E8`; `0x278F0`, `0x27420` |
| Deferred-delete pass (each game loop update) | `0xF8680` | `0xF0FE0` |
| Level's records start | `0x4E690` | `0x519F0` |
| Part pool (150 objects, factory `+0x10`), its creation, its allocation | `0x270600` (`0x3C0` each), `0x152370`, `0x152480` | `0x265780` (`0x3D0` each), `0x14B710`, |
| Level's shutdown (`Gameloop_Shutdown`) | `0xA3380` | `0x965B0` |
| Part start (thiscall part, ...): its script at `+0x140` | `0x150460` | `0x149830` |
| Part's script destroy (fastcall part: `+0x140` destroyed and cleared), from the part's free | `0x1500D0`, `0x151900` | `0x1494A0`, `0x14AC80` |
| Script start (no test for a data block), script data take (null when its bucket is full) | `0x4B120`, `0x4C6F0` | `0x4E240`, |
| Script destroy | `0x4B2B0` | `0x4E3D0` |
| Script pools start (per level): object pool, data buckets | `0x4C350`: `0x25E890`, `0x25E8F0` | |
| Model start's use of its weapons' scripts (`+0xB54`..`+0xB60`) | `0xE4070` | `0xDC1D0` |

A soldier model starts with team -1 (the model reset `0xCFE20`); the spawn's script gives it a team, and a soldier
no script gives one keeps -1 while alive (the first level's opening: the soldiers who run out of the room, whose script
then removes them alive as below). Only the record's hit points (`+0x14`, a float) tell a dead soldier: 0 or less.

About a second after death the soldier's script clears the model's team (native `0x2E230`: model vtable `+0x74`, team
at model `+0x124` = -1) and starts its fade (native `0x3D5B0` with 2: model vtable `+0x1B0`). The fade is a fader at
model `+0xFB0`: state (2 fading), elapsed, duration, fraction 0.5 (`0xAD1A0`). Past fraction × duration (`0xAD200`)
the soldier draw (`0xE9860`, model vtable `+0x34`) sets the model's alpha (`+0xFE4`) to `1 - (elapsed - f·d) / (f·d)`
(`0xAD2A0`), so the body fades to nothing.

Several seconds after death the script removes the soldier with a native (`0x34570`), which calls `0x4EA50` (co-op
`0x51DB0`, cdecl record): it gets the record's model (record vtable `+0x10`) and releases it with 2 updates. The
release queues the model in the deferred-delete map and tells its watchers (`0xF8860`), then takes it out of the
world. The script is marked ended (brain `+0x24` = -1) and `0x17C` goes to the soldier's parent script. Two updates
later the map's pass destroys the model; the model destroy goes through its brain (`+0x120`) to the brain's owner
record (`+0x20`) and on to the record destroy. That lowers the live count, unlinks the record, takes the model out of
the world again (a no-op by then), deactivates and frees it, frees the brain, unlinks the record's watch on the model
(`+0xEC`/`+0xF0`, made by `0xF87F0`) and deletes the record.

A soldier model's start (`0xE4C90`) builds its AI controller (model `+0x2B0` holds it, `+0x14D0` points at it), and
the controller joins a list of AI controllers the projectile code walks when a projectile goes (`0x28080`). The list's
nodes are reserved at each level's start (`0x28170`, from `0xA3B30`): 15 of them, and the list is emptied at the
level's shutdown. A join with no node left reads a null node and crashes. The model's deactivation destroys the
controller, which leaves the list; the removal finds its entry by walking the list, and would unlink the head node for
an entry that is not on it.

The world removal (`0xFA3E0`, done at once by `0xFA0C0`, or queued while the world is walking its objects) unlinks
the object from the world's list (`+0x18` previous, `+0x1C` next), from the world's type lists and its collision
(`0xF98C0`), and drops its pending contacts. The world's draw walks the same list: each object it shows (vtable
`+0x68`, no arguments) and finds in the view (`+0x64`, the view) is drawn (`+0x34`, the view). The soldier draw uses
the model's own transform (`+0xD0`), its 8 parts with their poses (`+0xB70`, 0x60 bytes each), its attachments and
shadow (`+0xEC8`), and the fader for its alpha.

The level's records start (`0x4E690`) also makes the soldiers' fixed pools (take `0x4BF70`: number `+8`, taken `+0xC`,
free list `+0x18`; null when none is left). Among them, `0x25EB30` (`0x10C` × 25) holds the records, and the model's
start (`0xE8CE0`) takes two collision objects from `0x25EB68` (`0x40` × 50: `+0x114`, also at `+0x128`, and `+0x11C`)
and two shapes from `0x25EB4C` (`0x50` × 450: `+0x110`, `+0x118`). With no collision object left the start reads a
null pointer and crashes. The model's deactivation gives all four back (the collision objects by their delete,
`0x13A2C0` → `0x4E560`; the shapes by `0x4E570`). The model's view test (vtable `+0x64`, `0xCF460`) uses the shapes,
and the mesh's bounds (`+0xC0`) when there are none. The soldier draw ends with its shadow (`+0xFCC`, vtable `+4`,
`0xB5E20`), which copies the shape the model's vtable `+0x44` gives for 3 (`0xE4210`: `+0x118`; 0 gives `+0x11C`).

A soldier's parts are objects (vtable `0x1F2CE0`) from one pool of 150 for the whole level, made by `0x152370` (co-op
`0x14B710`) at factory `0x2705F0` / `0x265770` `+0x10`: its attached objects (`+0xB54`-`+0xB60`), its part meshes
(the parts' objects, `+0xB70` + 0x60 each) and the object at `+0x1040`. A living soldier holds about 4. The allocation
(`0x152480`) returns 0 when the pool is full, and a soldier spawned then has none: it stands in the bind pose and does
not act. The model's deactivation (`0xE4CB0`: `0xE40D0` for the attached objects and `+0x1040`, `0xEAA20` for the head
part) and the model's free give them back.

A soldier's weapons (its attached objects `+0xB54`-`+0xB60`) each have a script: the part's start (`0x150460`) makes
one at `+0x140` from game data table `0x14`, with no entity. Its data block comes from the level's fixed script data
pool (`0x4C350` at the level's start; buckets at `0x25E8F0`, 0x1C bytes each: words, words + 1, number, taken,
total words, base, free list): 0x44 bytes × 50, 0x80 × 50, 0x140 × 100 and 0x258 × 28. The take (`0x4C6F0`) tries
each bucket big enough and returns 0 when none has a block left; the script start (`0x4B120`) writes through it
without a test and crashes (`0x49F4A`, a write to guest `0x14`). A rifle's script has 0x108 bytes of data, so the
0x140 bucket holds about 100 of them. The part's free (`0x151900`) destroys the script (`0x1500D0` → `0x4B2B0`), which
gives the block back (`0x4C170`). Only the model's start (`0xE4070`) uses its weapons' scripts. The world's objects
handle a part with no script (its release, message posts and the base updates test for it), except the collision
handler `0xEF6B0`, which reads the script's entity through it.

A model pool (`0xD1E90`) holds its number at `+0`, the number in use at `+4`, the model size rounded to 16 at `+0xC`,
its slots at `+0x10` and the last allocation's number at `+0x14`. Each slot is a 0x10-byte header and then the model
(the soldier allocation `0xCB9D0`, co-op `0xC0260`): the header's `+4` is the model's `+0x3C` (its second interface),
`+8` the allocation's number while in use (0 when free), and the model's `+0x40` is its slot.

While the model is in the world, its think (each update, from the entity step `0xFAB00`) updates its AI controller,
which reaches the soldier's brain and its owner record. A hit on the model (model vtable `+0x80`, `0xE9E30`) allocates
a message and posts `0x28` to the brain (`+0x120`); the post frees the message after the brain's handlers, and skips
the handlers of a brain whose script has ended (`+0x24` = -1).

## Implementation

`src/common/keep_bodies.{h,cpp}`, hooks in `src/game/hooks.cpp` and `src/coop/hooks.cpp`. A dead soldier's model is a
model with the soldier model vtable and team -1 whose record (the brain's owner, `+0x120` then `+0x20`, with the record
vtable) has no hit points left; a model with no brain, a body brain or an owner that is not a soldier record goes by
the team. At the record destroy the record itself is checked too. A living soldier a script removes is faded, released
and freed as in the game.

- The pool creation override adds 128 (co-op 64) to the soldier pool's count and 4 for each of those to the part
  pool's, whatever the setting, so it can be turned on in a level. Should the larger block not be had, the pool is made again with the game's count, and Keep
  Bodies keeps nothing in that level. The spare models are the pool's number less the level's limit, read from guest
  memory each time.
- The fade start runs, then with the setting on the fader of a dead soldier's model is switched off (`+0xFB0` = 0):
  the alpha stays 1.
- `[[hook]]` `KeepBodyInWorld` at the release's world removal: with the setting on and spare models, a dead soldier's
  model stays in the world until its record goes, so it is drawn by the world. It is still queued and its watchers
  are still told.
- `[[hook]]` `KeepBodyModel` at the record destroy's model block (past it `0x4F9FD`, co-op `0x52D5D`): with the
  setting on, spare models and a dead soldier's model, the model is kept as a body and its brain pointer is set to a
  body brain: a 0x40-byte block of host-kept guest memory (`src/common/guest_block.h`) with no class (`+0`), no owner
  (`+0x20`) and its script ended (`+0x24` = -1). A hit on a body is then posted to an ended script and its message
  freed; the model destroy finds no owner record. The model is taken out of the world, as the record destroy does at
  that point. Everything else of the destroy runs: the count, the record, the soldier's own brain and the watch go as
  in the game. Out of the world the body has no think, collision or hits, and the AI and scripts that walk the world
  do not find it.
- Then, as the deactivation would, the body's collision objects (`+0x114`, `+0x11C`) and its first shape (`+0x110`)
  go back to their pools and the model's pointers to them are cleared, so the level's soldiers find the collision
  objects as in the game. The shape at `+0x118` stays for the shadow: one of the 450 for each body.
- `[[hook]]` `DrawBodies` at the world's draw past its objects' loop (esi the world, ebp the view): each body is drawn
  with the world's own calls (`+0x68`, `+0x64` view, `+0x34` view). Nothing updates a body, so it keeps the pose it
  died in.
- On being kept, the body's AI controller leaves the watch list, so the level's soldiers find its nodes as in the game.
  The watch list removal override passes over an entry that is not on the list: the controller's destructor at the
  body's free.
- The body's weapons' scripts are destroyed with the game's call (`0x1500D0`, co-op `0x1494A0`) for each attached
  object that is a part in use with a script and is not in the world, so the bodies don't fill the level's script
  data pool. A weapon still in the world keeps its script. After a quick load the bodies found again have theirs
  destroyed too (a save from before this held them).
- A body keeps its parts, so it is still drawn whole. Its parts are the part pool's slots in use that the model's
  words point into, each counted once. The parts all bodies hold never number more than the slots added to the part
  pool (the pool's number less 150), so the level's soldiers find the game's 150. A body with more parts than that is
  not kept.
- The bodies are kept oldest first. Before one more is kept when there are as many as spare models, or when its parts
  would pass the added slots, the oldest is deactivated and freed, as the record destroy would have. Live soldiers never number more
  than the limit, so the pool always has a model for a spawn.
- The level's shutdown frees every body before the game's own shutdown runs, and keeps none until it is done. A level's
  records start empties the list.
- With the setting turned off, the next pass frees every body.
- After a quick load the next pass finds the bodies again: the soldier pool's models in use (at the slot's `+0x10`,
  the header pointing at its `+0x3C`) that are dead with a body brain (recognised by its bytes, so one an earlier
  instance made still counts), with their parts counted again. Past the spares or the added part slots (a save whose
  part pool has the game's count) the oldest are freed. The count is logged.

## Tests

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keep_bodies_test.cpp src/common/keep_bodies.cpp src/common/guest_block.cpp -o re/keep_bodies_test.exe"
& re/keep_bodies_test.exe
```

Covers the pool count, the setting off, a living soldier with no team left to the game, the fade, the model staying in the world until its record goes and then
outliving it out of the world with the body brain, the body's AI controller off the watch list, its collision objects
and first shape freed and the shadow's shape kept, its weapons' scripts destroyed (not a weapon in the world, not a
living soldier's, and again after a quick load), the bodies drawn from
the world's draw only, the oldest body freed past the spares (registers put back), no spares, the shutdown, turning
the setting off, finding the bodies in the pool after a quick load, the bodies' parts kept within the part pool's added
slots, and the bodies of a save whose part pool has the game's count freed after its load. `MOHRS_CORPSE_TRACE=1` ([debugging.md](debugging.md)) logs soldier record destroys and
model removals with their callers.

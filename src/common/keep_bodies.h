#pragma once
// Keep Bodies (PC Options > Display, setting keep_bodies; docs/keep-bodies.md): dead AI soldiers stay where they fell
// until the level needs their slot for a new soldier, instead of going a few seconds after death.
//
// Each AI soldier has a record (the spawn makes it: list, live count and the level's limit below) owning its brain
// (+8) and model (+0xF8); +0x14 is its hit points. A few seconds after death the soldier's script releases the record
// (record vtable +0, default.xbe 0xF8860): that queues it in the game's deferred-delete map (0xF8770) and the game
// loop's pass over the map (0xF8680) calls its destroy (vtable +8), which takes the model out of the world and frees
// model, brain and record.
//
// The body leaves the world before that: the script native that removes the soldier (default.xbe 0x34570, through
// 0x4EA50) releases the record's model (model vtable +0, 0xCE3F0), which queues it and takes it out of the world at
// once.
//
// With the setting on, that model release is passed over for a dead soldier and the pass leaves a dead soldier's
// record alone; the record is kept here, oldest first. The record
// still counts toward the level's limit, as it does in the game's own few seconds, so the pools and the limit are the
// game's. The oldest kept body is destroyed when the level needs room:
// - each pass, while the live count is at the limit;
// - before a spawn, when the count is at the limit;
// - before the cull that makes room for queued spawns (default.xbe 0x4EB20) by removing far soldiers, as many as it
//   would need, so it takes bodies instead of living soldiers.
// A kept record is never queued in the map again, so nothing else destroys it later. The list is emptied when a
// level's AI starts, and each body is checked against the soldier list and the map before it is destroyed. A quick
// load puts back records the list here knows nothing of: the next pass takes every dead soldier record that is not in
// the map as a kept body, oldest first.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct BodyLayout {
    uint32_t record_vtable;  // the AI soldier record's vtable
    uint32_t list;           // the first record; next at +0xC
    uint32_t count;          // records alive (the destroy lowers it)
    uint32_t limit;          // the level's most records (game data)
    uint32_t destroy;        // record vtable +8 (fastcall record)
    // the deferred-delete map: [map] its tree's head node, [map + 4] its size; a node's parent +4, left +8, right
    // +0xC, object +0x10 and updates left +0x14; the head's parent is the root, an empty child 0
    uint32_t delete_map;
    // the world the models are in: its first and last object at +0 and +4, an object's links at +0x18 and +0x1C
    uint32_t world;
    uint32_t model_vtable;  // the soldier model's
};

inline constexpr BodyLayout kDefaultBodies{0x1E21A4, 0x25E9B0, 0x25E9A4, 0x25E9A0, 0x4F990,
                                           0x282688, 0x270630, 0x1E99A0};
inline constexpr BodyLayout kCoopBodies{0x1DB4C4, 0x256BE0, 0x256BD4, 0x256BD0, 0x52CF0,
                                        0x277B18, 0x2657B0, 0x1E25C0};

// [[hook]] at the deferred-delete pass's destroy call (ecx the object); true skips the call: the body is kept
bool KeepBody(const X86Context& ctx, const BodyLayout& layout);
// [[hook]] in the script's soldier removal (default.xbe 0x4EA50, moh3rdc.xbe 0x51DB0; esi the record) at the model's
// release (model vtable +0 with 2 updates: queued, and out of the world at once); true skips it for a dead soldier,
// whose record is then kept: its destroy takes the model out of the world when the body goes
bool KeepBodyModel(const X86Context& ctx, const BodyLayout& layout);
// after the model's fade starts (model vtable +0x1B0, default.xbe 0xE57C0, moh3rdc.xbe 0xDD940: thiscall model,
// seconds): the fader at +0xFB0 off again for a dead soldier's model (team +0x124 -1)
void KeepBodyFade(uint32_t model, const BodyLayout& layout);
// at the pass's start: bodies kept with the setting off go now, and the oldest while the count is at the limit
void KeepBodiesPass(X86Context& ctx, const BodyLayout& layout);
// at a spawn's start: one body goes when the count is at the limit
void KeepBodiesSpawn(X86Context& ctx, const BodyLayout& layout);
// at the cull's start with the spawns it makes room for: bodies go first
void KeepBodiesCull(X86Context& ctx, const BodyLayout& layout, uint32_t wanted);
// the deferred-delete map's insert (thiscall object): true skips it for a kept body
bool KeptBody(uint32_t object);
// a record destroyed by anyone
void BodyDestroyed(uint32_t record);
// a level's AI starts: nothing kept is valid
void ForgetBodies();
// a quick load put back guest memory: the next pass finds the kept bodies again
void BodiesAfterLoad();

}  // namespace game

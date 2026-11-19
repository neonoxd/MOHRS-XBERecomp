#pragma once
// Keep Bodies (PC Options > Display, setting keep_bodies; docs/keep-bodies.md): dead AI soldiers stay where they fell
// for the rest of the level, up to a number of bodies, instead of fading out a few seconds after death.
//
// An AI soldier is a record (the level's soldier limit counts records) owning a brain (+8) and a model (+0xF8). About
// a second after death its script clears the model's team (+0x124 = -1) and starts the model's fade (model vtable
// +0x1B0, default.xbe 0xE57C0: a fader at +0xFB0 the draw turns into alpha); several seconds later the removal native
// releases the model (model vtable +0, 0xCE3F0: queued in the deferred-delete map and taken out of the world at once).
// The map's pass destroys the model, whose destroy goes on to the record destroy (0x4F990), which takes the model out
// of the world again, deactivates and frees it, frees the brain and the record.
//
// With the setting on, for a dead soldier's model (soldier model vtable, team -1, and its record, through the brain's
// owner +0x20, with no hit points left; team -1 is also every soldier model's default, so a living soldier a script
// removes, such as the soldiers who run out of the room at the first level's opening, has it too):
// - the fader is switched off after the fade starts;
// - the release leaves the model in the world until its record goes (it is still queued, and its watchers are told);
// - the record destroy skips the model's deactivation and free: the model is taken out of the world as the game does
//   there, and kept as a body. A body is out of the world for good: nothing thinks, collides or hits it, and the AI
//   and the scripts no longer find it. Only its draw is kept: after the world's draw loop each body is drawn with the
//   world's own calls (vtable +0x68, +0x64 view, +0x34 view), in the pose it died in;
// - the body's brain pointer (+0x120) is set to a body brain, a script that has ended with no class and no owner (the
//   record, its brain and the soldier's place in the limit go at the game's own time), and its AI controller's entry
//   leaves the projectile watch list, which the game sizes for the level's soldiers (15 nodes; a spawn with none left
//   crashes) and walks apart from the world; the entry's removal at the body's free passes over it;
// - the body's collision objects (+0x114, +0x11C) and one shape (+0x110) go back to their pools, as the model's
//   deactivation does after the same world removal: the level's pool holds the collision objects for 25 soldiers
//   (0x4E690), and a spawn with none left crashes. The shape at +0x118 stays for the draw's shadow.
// - the body keeps its parts (the attached objects, gun and part meshes from the level's object pool, 150 for the
//   whole level: default.xbe 0x152370, moh3rdc.xbe 0x14B710). A soldier spawned with that pool empty gets none and
//   stands in the bind pose with no AI, so the pool is made larger for the bodies too, and the parts the bodies hold
//   never number more than the slots added.
// The soldier model pool is made larger by a number of spare models when the level's objects start (0xCB5E0 makes it
// with the level's soldier limit), so live soldiers always find a model. The bodies are kept here, oldest first, never
// more than the spare models: the oldest is freed before one more is kept. All are freed when the level shuts down.
// After a quick load the next pass finds them again in the model pool.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct BodyLayout {
    uint32_t limit;         // the level's most soldier records (game data), which the model pool is made with
    uint32_t model_vtable;  // the soldier model's
    uint32_t world;         // the world (thiscall this): first object at +0, next +0x1C
    uint32_t leave_world;   // thiscall world, object
    uint32_t deactivate;    // the model's vtable offset the record destroy calls
    uint32_t factory;       // the model factory (thiscall this)
    uint32_t free_model;    // thiscall factory, model
    uint32_t model_pool;    // factory +0x10: the soldier models; number +0, model size +0xC, slots +0x10 (a slot is a
                            // 0x10-byte header, then the model: header +4 the model's +0x3C, +8 nonzero while in
                            // use; the model's +0x40 is its slot)
    uint32_t spare;         // models added to the pool for bodies
    uint32_t watch_list;    // the AI controllers' projectile watch list: its head node (+0 last, +4 first; a node is
                            // previous, next, entry), 15 nodes at a level's start
    uint32_t watch_leave;   // cdecl entry: takes an entry off the watch list
    uint32_t watch_entry;   // the AI controller's entry on it (controller +)
    uint32_t free_shape;    // cdecl shape: back to its pool (the model's +0x110 and +0x118)
    uint32_t record_vtable; // the AI soldier record's (vtable +8 the record destroy); hit points at record +0x14
    uint32_t part_pool;     // the level's object pool (factory +0x10) the soldiers' parts come from: same layout as
                            // the model pool, the object at slot +0x10
    uint32_t part_count;    // the game's number of objects in it
};
// the part pool's slots added for each spare model
inline constexpr uint32_t kPartsPerBody = 4;

inline constexpr BodyLayout kDefaultBodies{0x25E9A0, 0x1E99A0, 0x270630, 0xFA3E0,  0x168,  0x270460, 0xCA7C0,
                                           0x270470, 128,      0x1FC7C0, 0x27D50, 0xD30,   0x4E570, 0x1E21A4,
                                           0x270600, 150};
inline constexpr BodyLayout kCoopBodies{0x256BD0, 0x1E25C0, 0x2657B0, 0xF2EB0,  0x160,  0x2655E0, 0xBF070,
                                        0x2655F0, 64,       0x1F56E8, 0x27420, 0xD20,   0x518D0, 0x1DB4C4,
                                        0x265780, 150};

// after the model's fade starts (default.xbe 0xE57C0, moh3rdc.xbe 0xDD940: thiscall model, seconds): the fader at
// +0xFB0 off again for a dead soldier's model
void KeepBodyFade(uint32_t model, const BodyLayout& layout);
// [[hook]] in the model release (default.xbe 0xCE3F0, moh3rdc.xbe 0xC2EB0) at its world removal (esi the model):
// true leaves a dead soldier's model in the world
bool KeepBodyInWorld(const X86Context& ctx, const BodyLayout& layout);
// [[hook]] in the record destroy (default.xbe 0x4F990, moh3rdc.xbe 0x52CF0) at its model's removal (esi the record):
// true keeps the model as a body instead
bool KeepBodyModel(X86Context& ctx, const BodyLayout& layout);
// [[hook]] in the world's draw (default.xbe 0xF9460, moh3rdc.xbe 0xF1F30: thiscall world, view) after its objects'
// loop (esi the world, ebp the view): the bodies drawn
void DrawBodies(X86Context& ctx, const BodyLayout& layout);
// the watch list removal (default.xbe 0x27D50, moh3rdc.xbe 0x27420: cdecl entry): false for an entry not on the list,
// whose removal must be passed over (the game's would unlink the list's head node)
bool OnWatchList(uint32_t entry, const BodyLayout& layout);
// a game object pool's creation (thiscall pool, size, count, flags): the count to make it with (the soldier models
// and the part pool get room for the bodies)
uint32_t ModelPoolCount(uint32_t pool, uint32_t count, const BodyLayout& layout);
// at the deferred-delete pass's start: bodies found again after a quick load; all freed with the setting off
void KeepBodiesPass(X86Context& ctx, const BodyLayout& layout);
// at the level's shutdown, before the world and the pools go: every body freed, and none kept until it is over
void BodiesShutdown(X86Context& ctx, const BodyLayout& layout, bool starting);
// a level's records start: the bodies list is empty
void ForgetBodies();
// a quick load put back guest memory: the next pass finds the bodies again
void BodiesAfterLoad();

}  // namespace game

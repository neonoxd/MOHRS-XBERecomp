#include "common/keep_bodies.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <vector>

#include "common/guest_block.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

struct Kept {
    uint32_t model;
    uint32_t parts;  // the part pool's objects it holds
};
std::deque<Kept> g_bodies;      // kept models, oldest first; the game thread only
uint32_t g_parts = 0;           // the part pool's objects the bodies hold
bool g_loaded = false;          // a quick load since the last pass
bool g_shutdown = false;        // the level is shutting down

// an AI soldier record with hit points left (+0x14)
bool LivingRecord(uint32_t record, const BodyLayout& l) {
    return record && R32(record) == l.record_vtable && kernel::G<float>(record + 0x14) > 0.0f;
}

bool BodyBrain(uint32_t brain);

// A dead soldier's model: the soldier model's vtable, the team the death script cleared, and no living record. Team -1
// alone is not death: the model reset (0xCFE20) starts every soldier model with it, and soldiers a script never gives
// a team keep it (the first level's opening: the soldiers who run out of the room and are removed alive by their
// script).
bool DeadModel(uint32_t model, const BodyLayout& l) {
    if (!model || R32(model) != l.model_vtable || int32_t(R32(model + 0x124)) != -1) return false;
    uint32_t brain = R32(model + 0x120);
    return !brain || BodyBrain(brain) || !LivingRecord(R32(brain + 0x20), l);
}

// A body's brain: a script that has ended (+0x24 -1) with no class (+0) and no owner (+0x20). A message posted to it
// (a hit) runs no handler and is freed (0x4ADB0); the model destroy finds no owner record. Kept across quick loads
// (src/common/guest_block.h); one an older load left behind is still recognised by its bytes.
constexpr uint32_t kBrainSize = 0x40;
bool BodyBrain(uint32_t brain) {
    return brain && R32(brain) == 0 && R32(brain + 0x20) == 0 && R32(brain + 0x24) == 0xFFFFFFFFu;
}
uint32_t NewBodyBrain() {
    static GuestBlock block;
    bool fresh = false;
    uint32_t brain = block.Get(kBrainSize, &fresh);
    if (brain && fresh) {
        for (uint32_t at = 0; at < kBrainSize; at += 4) G<uint32_t>(brain + at) = 0;
        G<uint32_t>(brain + 0x24) = 0xFFFFFFFFu;
    }
    return brain;
}
// a kept body: a dead soldier's model with the body brain, or none if that could not be had
bool Body(uint32_t model, const BodyLayout& l) {
    if (!DeadModel(model, l)) return false;
    uint32_t brain = R32(model + 0x120);
    return brain == 0 || BodyBrain(brain);
}

// the spare models the pool was made with: its number less the level's limit
uint32_t Spare(const BodyLayout& l) {
    int32_t spare = int32_t(R32(l.model_pool)) - int32_t(R32(l.limit));
    return spare > 0 ? uint32_t(spare) : 0;
}

// the part pool's slots added for the bodies: its number less the game's
uint32_t SpareParts(const BodyLayout& l) {
    int32_t spare = int32_t(R32(l.part_pool)) - int32_t(l.part_count);
    return spare > 0 ? uint32_t(spare) : 0;
}

// The part pool's objects a model holds (its attached objects, gun and part meshes): the slots in use that the model's
// words point into, each once.
uint32_t Parts(uint32_t model, const BodyLayout& l) {
    uint32_t number = R32(l.part_pool), step = R32(l.part_pool + 0xC) + 0x10, slots = R32(l.part_pool + 0x10);
    uint32_t size = R32(l.model_pool + 0xC);
    if (!slots || !number || number > 0x10000 || step <= 0x10 || size > 0x10000) return 0;
    uint32_t span = number * step;
    std::vector<uint32_t> seen;
    for (uint32_t at = 0; at + 4 <= size; at += 4) {
        uint32_t offset = R32(model + at) - slots;
        if (offset >= span) continue;
        uint32_t slot = offset / step;
        if (R32(slots + slot * step + 8) && std::find(seen.begin(), seen.end(), slot) == seen.end())
            seen.push_back(slot);
    }
    return uint32_t(seen.size());
}

uint32_t ThisCall(X86Context& ctx, uint32_t fn, uint32_t self, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return result;
}

// A body's AI controller off the projectile watch list (the game's list holds 15 nodes, one for each soldier the level
// allows; the next spawn would find none). The controller's destructor at the body's free finds it gone.
void LeaveWatchList(X86Context& ctx, uint32_t model, const BodyLayout& l) {
    uint32_t controller = R32(model + 0x14D0);
    if (!controller) return;
    uint32_t entry = controller + l.watch_entry;
    if (OnWatchList(entry, l)) kernel::CallGuest(ctx, l.watch_leave, {entry});
}

// A body's collision objects back to their pools, as the model's deactivation does (default.xbe 0xE4CB0): both from a
// pool of 50 (+0x114, +0x11C, deleted), which a model's start (0xE8CE0) takes two from and crashes with none left, and
// one of its two shapes from a pool of 450 (+0x110). The other shape (+0x118) stays: the draw's shadow copies it
// (0xB5E20 through vtable +0x44 with 3), and the view test uses it. Out of the world nothing collides with a body.
void FreeCollision(X86Context& ctx, uint32_t model, const BodyLayout& l) {
    uint32_t first = R32(model + 0x114);
    for (uint32_t at : {0x114u, 0x11Cu}) {
        uint32_t object = R32(model + at);
        if (!object) continue;
        ThisCall(ctx, R32(R32(object)), object, {1});
        G<uint32_t>(model + at) = 0;
    }
    if (first && R32(model + 0x128) == first) G<uint32_t>(model + 0x128) = 0;  // a second pointer to +0x114's
    if (uint32_t shape = R32(model + 0x110)) {
        kernel::CallGuest(ctx, l.free_shape, {shape});
        G<uint32_t>(model + 0x110) = 0;
    }
}

// an object of the part pool's that is in use: at +0x10 of a slot whose header's +8 is set
bool PartInUse(uint32_t object, const BodyLayout& l) {
    uint32_t number = R32(l.part_pool), step = R32(l.part_pool + 0xC) + 0x10, slots = R32(l.part_pool + 0x10);
    if (!slots || !number || number > 0x10000 || step <= 0x10) return false;
    uint32_t offset = object - slots;
    return offset < number * step && offset % step == 0x10 && R32(slots + offset - 0x10 + 8);
}

bool InWorld(uint32_t object, const BodyLayout& l) {
    uint32_t n = 0;
    for (uint32_t o = R32(l.world); o && n < 0x10000; o = R32(o + 0x1C), n++)
        if (o == object) return true;
    return false;
}

// A body's weapons' scripts destroyed, as the part's free does (default.xbe 0x151900 -> 0x1500D0). The part's start
// (0x150460) makes one for each weapon at +0x140, its data from the level's fixed script pool (0x4C6F0: 0x140-byte
// blocks x100, 0x258 x28), and the script's start (0x4B120) does not test for none. Kept, the bodies' weapons filled
// it, and the next spawn's weapon crashed (0x49F4A). Only the model's start (0xE4070) uses the weapons' scripts, and a
// body never starts again; the part's free finds none. A weapon still in the world keeps its script: the world's
// collision handler (0xEF6B0) reads it without a test.
uint32_t ReleaseWeaponScripts(X86Context& ctx, uint32_t model, const BodyLayout& l) {
    uint32_t released = 0;
    for (uint32_t at = 0xB54; at <= 0xB60; at += 4) {
        uint32_t part = R32(model + at);
        if (!part || !PartInUse(part, l) || !R32(part + 0x140) || InWorld(part, l)) continue;
        ThisCall(ctx, l.part_script, part, {});
        released++;
    }
    return released;
}

// as the record destroy does: out of the world, deactivated, back to the pool
void FreeBody(X86Context& ctx, uint32_t model, const BodyLayout& l) {
    ThisCall(ctx, l.leave_world, l.world, {model});
    ThisCall(ctx, R32(R32(model) + l.deactivate), model, {});
    ThisCall(ctx, l.free_model, l.factory, {model});
}

void FreeOldest(X86Context& ctx, const BodyLayout& l) {
    Kept kept = g_bodies.front();
    g_bodies.pop_front();
    g_parts -= std::min(g_parts, kept.parts);
    if (Body(kept.model, l)) FreeBody(ctx, kept.model, l);
}

// the bodies within the spare models and the part pool's added slots, the oldest freed first
void Trim(X86Context& ctx, const BodyLayout& l, size_t models, uint32_t parts) {
    while (!g_bodies.empty() && (g_bodies.size() + models > Spare(l) || g_parts + parts > SpareParts(l)))
        FreeOldest(ctx, l);
}

// after a quick load: the bodies in the model pool, the dead soldier models in use with the body brain (a save from
// before the part pool was made larger has bodies holding its slots: they are freed)
void FindBodies(X86Context& ctx, const BodyLayout& l) {
    g_bodies.clear();
    g_parts = 0;
    uint32_t count = R32(l.model_pool), step = R32(l.model_pool + 0xC) + 0x10, slots = R32(l.model_pool + 0x10);
    for (uint32_t i = 0; slots && i < count && i < 4096; i++) {
        uint32_t slot = slots + i * step, model = slot + 0x10;  // the soldier allocation (0xCB9D0) puts it there
        if (R32(slot + 8) && R32(slot + 4) == model + 0x3C && Body(model, l)) {
            uint32_t parts = Parts(model, l);
            g_bodies.push_back({model, parts});
            g_parts += parts;
        }
    }
    size_t found = g_bodies.size();
    Trim(ctx, l, 0, 0);
    uint32_t scripts = 0;  // a save from before the weapons' scripts were released
    for (const Kept& kept : g_bodies) scripts += ReleaseWeaponScripts(ctx, kept.model, l);
    if (Opt().keep_bodies || found)
        LOGR("keep bodies: %zu bodies after the load (%zu freed), holding %u of %u added parts, %u weapon scripts "
             "released",
             g_bodies.size(), found - g_bodies.size(), g_parts, SpareParts(l), scripts);
}

}  // namespace

void KeepBodyFade(uint32_t model, const BodyLayout& l) {
    if (!Opt().keep_bodies || !DeadModel(model, l)) return;
    G<uint32_t>(model + 0xFB0) = 0;  // the fader's state: off, so the draw leaves the alpha (+0xFE4) at 1
}

bool KeepBodyInWorld(const X86Context& ctx, const BodyLayout& l) {
    return Opt().keep_bodies && !g_shutdown && Spare(l) > 0 && DeadModel(ctx.esi, l);
}

bool KeepBodyModel(X86Context& ctx, const BodyLayout& l) {
    uint32_t model = R32(ctx.esi + 0xF8);
    if (!Opt().keep_bodies || g_shutdown || LivingRecord(ctx.esi, l) || !DeadModel(model, l)) return false;
    uint32_t spare = Spare(l), parts = Parts(model, l), spare_parts = SpareParts(l);
    // a body may not hold the level's own parts: the next spawns would find none
    if (!spare || parts > spare_parts) return false;
    Trim(ctx, l, 1, parts);
    G<uint32_t>(model + 0x120) = NewBodyBrain();  // its own brain goes with the record
    LeaveWatchList(ctx, model, l);
    ThisCall(ctx, l.leave_world, l.world, {model});  // as the record destroy does; drawn by DrawBodies from now on
    FreeCollision(ctx, model, l);
    uint32_t scripts = ReleaseWeaponScripts(ctx, model, l);
    g_bodies.push_back({model, parts});
    g_parts += parts;
    static uint32_t logged = 0;
    if (logged++ < 8)
        LOGR("keep bodies: model %08X kept (%zu of %u bodies, %u parts, %u of %u added parts, %u weapon scripts "
             "released)",
             model, g_bodies.size(), spare, parts, g_parts, spare_parts, scripts);
    return true;
}

void DrawBodies(X86Context& ctx, const BodyLayout& l) {
    if (ctx.esi != l.world || g_bodies.empty()) return;
    uint32_t view = ctx.ebp;
    for (const Kept& kept : g_bodies) {
        uint32_t model = kept.model;
        if (!Body(model, l)) continue;
        uint32_t vtable = R32(model);
        if ((ThisCall(ctx, R32(vtable + 0x68), model, {}) & 0xFF) &&
            (ThisCall(ctx, R32(vtable + 0x64), model, {view}) & 0xFF))
            ThisCall(ctx, R32(vtable + 0x34), model, {view});
    }
}

bool OnWatchList(uint32_t entry, const BodyLayout& l) {
    uint32_t n = 0;
    for (uint32_t node = R32(l.watch_list + 4); node && node != l.watch_list && n < 4096; node = R32(node + 4), n++)
        if (R32(node + 8) == entry) return true;
    return false;
}

uint32_t ModelPoolCount(uint32_t pool, uint32_t count, const BodyLayout& l) {
    if (pool == l.model_pool) return count + l.spare;
    if (pool == l.part_pool) return count + l.spare * kPartsPerBody;
    return count;
}

void KeepBodiesPass(X86Context& ctx, const BodyLayout& l) {
    if (g_loaded) {
        g_loaded = false;
        FindBodies(ctx, l);
    }
    if (!Opt().keep_bodies)
        while (!g_bodies.empty()) FreeOldest(ctx, l);
}

void BodiesShutdown(X86Context& ctx, const BodyLayout& l, bool starting) {
    g_shutdown = starting;
    if (starting)
        while (!g_bodies.empty()) FreeOldest(ctx, l);
}

void ForgetBodies() {
    g_bodies.clear();
    g_parts = 0;
    g_shutdown = false;
}

void BodiesAfterLoad() { g_loaded = true; }

}  // namespace game

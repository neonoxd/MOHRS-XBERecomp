#include "common/keep_bodies.h"

#include <algorithm>
#include <cstdlib>
#include <deque>

#include "common/guest_block.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

std::deque<uint32_t> g_bodies;  // kept models, oldest first; the game thread only
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

// as the record destroy does: out of the world, deactivated, back to the pool
void FreeBody(X86Context& ctx, uint32_t model, const BodyLayout& l) {
    ThisCall(ctx, l.leave_world, l.world, {model});
    ThisCall(ctx, R32(R32(model) + l.deactivate), model, {});
    ThisCall(ctx, l.free_model, l.factory, {model});
}

void FreeOldest(X86Context& ctx, const BodyLayout& l) {
    uint32_t model = g_bodies.front();
    g_bodies.pop_front();
    if (Body(model, l)) FreeBody(ctx, model, l);
}

// after a quick load: the bodies in the model pool, the dead soldier models in use with the body brain
void FindBodies(const BodyLayout& l) {
    g_bodies.clear();
    uint32_t count = R32(l.model_pool), step = R32(l.model_pool + 0xC) + 0x10, slots = R32(l.model_pool + 0x10);
    for (uint32_t i = 0; slots && i < count && i < 4096; i++) {
        uint32_t slot = slots + i * step, model = slot + 0x10;  // the soldier allocation (0xCB9D0) puts it there
        if (R32(slot + 8) && R32(slot + 4) == model + 0x3C && Body(model, l)) g_bodies.push_back(model);
    }
    while (g_bodies.size() > Spare(l)) g_bodies.pop_front();  // never: the save had no more
    if (Opt().keep_bodies) LOGR("keep bodies: %zu bodies after the load", g_bodies.size());
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
    uint32_t spare = Spare(l);
    if (!spare) return false;
    while (g_bodies.size() >= spare) FreeOldest(ctx, l);
    G<uint32_t>(model + 0x120) = NewBodyBrain();  // its own brain goes with the record
    LeaveWatchList(ctx, model, l);
    ThisCall(ctx, l.leave_world, l.world, {model});  // as the record destroy does; drawn by DrawBodies from now on
    FreeCollision(ctx, model, l);
    g_bodies.push_back(model);
    static uint32_t logged = 0;
    if (logged++ < 8) LOGR("keep bodies: model %08X kept (%zu of %u bodies)", model, g_bodies.size(), spare);
    return true;
}

void DrawBodies(X86Context& ctx, const BodyLayout& l) {
    if (ctx.esi != l.world || g_bodies.empty()) return;
    uint32_t view = ctx.ebp;
    for (uint32_t model : g_bodies) {
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
    return pool == l.model_pool ? count + l.spare : count;
}

void KeepBodiesPass(X86Context& ctx, const BodyLayout& l) {
    if (g_loaded) {
        g_loaded = false;
        FindBodies(l);
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
    g_shutdown = false;
}

void BodiesAfterLoad() { g_loaded = true; }

}  // namespace game

#include "common/keep_bodies.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <deque>
#include <initializer_list>
#include <iterator>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

std::deque<uint32_t> g_kept;  // oldest first; the game thread only
bool g_loaded = false;        // a quick load since the last pass

int32_t Count(const BodyLayout& l) { return int32_t(R32(l.count)); }
int32_t Limit(const BodyLayout& l) { return int32_t(R32(l.limit)); }

// a soldier record whose hit points ran out, or whose model's team the death script cleared (some deaths leave the
// record's hit points above 0)
bool DeadSoldier(uint32_t record, const BodyLayout& l) {
    if (!record || R32(record) != l.record_vtable) return false;
    if (!(G<float>(record + 0x14) > 0.0f)) return true;
    uint32_t model = R32(record + 0xF8);
    return model && int32_t(R32(model + 0x124)) == -1;
}

bool Listed(uint32_t record, const BodyLayout& l) {
    uint32_t n = 0;
    for (uint32_t r = R32(l.list); r && n < 256; r = R32(r + 0xC), n++)
        if (r == record) return true;
    return false;
}

// waiting in the deferred-delete map: the game destroys it itself
bool Queued(uint32_t object, const BodyLayout& l) {
    uint32_t map = R32(l.delete_map);
    if (!map) return false;
    uint32_t head = R32(map);
    if (!head) return false;
    uint32_t stack[64], depth = 0, visited = 0;
    if (uint32_t root = R32(head + 4); root && root != head) stack[depth++] = root;
    while (depth && visited < 4096) {
        uint32_t node = stack[--depth];
        visited++;
        if (R32(node + 0x10) == object) return true;
        for (uint32_t child : {R32(node + 8), R32(node + 0xC)})
            if (child && child != head && depth < std::size(stack)) stack[depth++] = child;
    }
    return false;
}

// MOHRS_CORPSE_TRACE: each pass, a kept body's model joining or leaving the world's list, with its team
void TraceKept(const BodyLayout& l) {
    static const bool on = std::getenv("MOHRS_CORPSE_TRACE") != nullptr;
    if (!on) return;
    static std::map<uint32_t, int> was;  // record: bit 0 in the world, bit 1 the pose was evaluated (+0x2AC)
    static const auto start = std::chrono::steady_clock::now();
    for (uint32_t record : g_kept) {
        uint32_t model = R32(record + 0xF8);
        if (!model) continue;
        bool in = R32(l.world) == model || R32(l.world + 4) == model || R32(model + 0x18) || R32(model + 0x1C);
        bool posed = kernel::G<uint8_t>(model + 0x2AC) != 0;
        int now = int(in) | int(posed) << 1;
        auto it = was.find(record);
        if (it != was.end() && it->second == now) continue;
        was[record] = now;
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        LOGR("keep bodies: %.2f s kept record %08X model %08X %s the world, pose %s (team %d, brain %08X)", seconds,
             record, model, in ? "in" : "NOT in", posed ? "evaluated" : "NOT evaluated", int32_t(R32(model + 0x124)),
             R32(model + 0x120));
    }
}

// after a quick load: every dead soldier record not waiting in the map, oldest first (the list is newest first)
void FindKept(const BodyLayout& l) {
    g_kept.clear();
    uint32_t n = 0;
    for (uint32_t r = R32(l.list); r && n < 256; r = R32(r + 0xC), n++)
        if (DeadSoldier(r, l) && !Queued(r, l)) g_kept.push_front(r);
    if (!g_kept.empty()) LOGR("keep bodies: %zu bodies after the load", g_kept.size());
}

// destroys the oldest kept body that is still one; false when none is left
bool ReleaseOldest(X86Context& ctx, const BodyLayout& l) {
    while (!g_kept.empty()) {
        uint32_t record = g_kept.front();
        g_kept.pop_front();
        if (!Listed(record, l) || !DeadSoldier(record, l) || Queued(record, l)) continue;
        static uint32_t logged = 0;
        if (logged++ < 16)
            LOGR("keep bodies: released record %08X (%zu still kept, %d of %d records)", record, g_kept.size(), Count(l),
                 Limit(l));
        uint32_t ecx = ctx.ecx, edx = ctx.edx, eax = ctx.eax;
        ctx.ecx = record;
        kernel::CallGuest(ctx, l.destroy, {});
        ctx.ecx = ecx;
        ctx.edx = edx;
        ctx.eax = eax;
        return true;
    }
    return false;
}

}  // namespace

bool KeepBody(const X86Context& ctx, const BodyLayout& l) {
    uint32_t record = ctx.ecx;
    if (!Opt().keep_bodies || !DeadSoldier(record, l)) return false;
    if (std::find(g_kept.begin(), g_kept.end(), record) == g_kept.end()) g_kept.push_back(record);
    static uint32_t logged = 0;
    if (logged++ < 16)
        LOGR("keep bodies: kept record %08X (%zu kept, %d of %d records)", record, g_kept.size(), Count(l), Limit(l));
    return true;
}

bool KeepBodyModel(const X86Context& ctx, const BodyLayout& l) {
    uint32_t record = ctx.esi;
    if (!Opt().keep_bodies || !DeadSoldier(record, l)) return false;
    if (std::find(g_kept.begin(), g_kept.end(), record) == g_kept.end()) g_kept.push_back(record);
    static uint32_t logged = 0;
    if (logged++ < 16)
        LOGR("keep bodies: kept the model of record %08X (%zu kept, %d of %d records)", record, g_kept.size(),
             Count(l), Limit(l));
    return true;
}

void KeepBodyFade(uint32_t model, const BodyLayout& l) {
    if (!Opt().keep_bodies || !model || R32(model) != l.model_vtable || int32_t(R32(model + 0x124)) != -1) return;
    G<uint32_t>(model + 0xFB0) = 0;  // the fader's state: off, so the draw leaves the alpha (+0xFE4) at 1
    static uint32_t logged = 0;
    if (logged++ < 16) LOGR("keep bodies: no fade for model %08X", model);
}

void KeepBodiesPass(X86Context& ctx, const BodyLayout& l) {
    if (g_loaded) {
        g_loaded = false;
        FindKept(l);
    }
    if (g_kept.empty()) return;
    TraceKept(l);
    if (!Opt().keep_bodies) {
        while (ReleaseOldest(ctx, l)) {}
        return;
    }
    while (Count(l) >= Limit(l) && ReleaseOldest(ctx, l)) {}
}

void KeepBodiesSpawn(X86Context& ctx, const BodyLayout& l) {
    if (!g_kept.empty() && Count(l) >= Limit(l)) ReleaseOldest(ctx, l);
}

void KeepBodiesCull(X86Context& ctx, const BodyLayout& l, uint32_t wanted) {
    // the cull removes wanted + count - limit soldiers; bodies first
    while (int32_t(wanted) + Count(l) - Limit(l) > 0 && ReleaseOldest(ctx, l)) {}
}

bool KeptBody(uint32_t object) {
    return std::find(g_kept.begin(), g_kept.end(), object) != g_kept.end();
}

void BodyDestroyed(uint32_t record) {
    auto it = std::find(g_kept.begin(), g_kept.end(), record);
    if (it != g_kept.end()) g_kept.erase(it);
}

void ForgetBodies() {
    g_kept.clear();
    LOGR("keep bodies: level start, setting %s", Opt().keep_bodies ? "on" : "off");
}

void BodiesAfterLoad() { g_loaded = true; }

}  // namespace game

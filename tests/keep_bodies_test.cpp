// Host test: Keep Bodies (src/common/keep_bodies.h) on fake soldier models in a fake world, with the game's calls
// stubbed.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keep_bodies_test.cpp
//     src/common/keep_bodies.cpp src/common/guest_block.cpp -o re/keep_bodies_test.exe
#include "common/keep_bodies.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

std::vector<uint8_t> memory(0x600000);
std::vector<uint32_t> freed, deactivated, drawn, deleted, shapes;

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
using kernel::R32;
const game::BodyLayout& L = game::kDefaultBodies;
constexpr uint32_t kDeactivate = 0x123456;  // the model vtable's +0x168 entry
// the model vtable's draw entries: +0x68 shown, +0x64 in the view, +0x34 draw
constexpr uint32_t kShown = 0x123460, kInView = 0x123470, kDraw = 0x123480;
constexpr uint32_t kView = 0x5E0000;
constexpr uint32_t kCollision = 0x5D0000, kDelete = 0x123490;  // the collision objects' vtable, its delete

// models at 0x300000 + i * 0x2400, records at 0x10000 + i * 0x200, watch list nodes at 0x480000 + i * 0x10
uint32_t Model(int i) { return 0x300000 + uint32_t(i) * 0x2400; }
uint32_t Record(int i) { return 0x10000 + uint32_t(i) * 0x200; }
uint32_t Entry(int i) { return Model(i) + 0x1800 + L.watch_entry; }  // its AI controller at model +0x1800

// the watch list: the head node's +0 the last node, +4 the first; a node is previous, next, entry
void Unlink(uint32_t node) {
    uint32_t prev = R32(node), next = R32(node + 4);
    G<uint32_t>(prev + 4) = next;
    G<uint32_t>(next) = prev;
}

// the model pool's slot i: its 0x10-byte header just before model i (a model size of 0x23F0)
uint32_t Slot(int i) { return Model(i) - 0x10; }

// a soldier model in the world (newest first) and in pool slot i (header +4 the model's +0x3C, +8 in use), team 7, a
// brain and its AI controller on the watch list
void Spawn(int i) {
    uint32_t m = Model(i), r = Record(i);
    G<uint32_t>(Slot(i) + 4) = m + 0x3C;
    G<uint32_t>(Slot(i) + 8) = uint32_t(i) + 1;
    G<uint32_t>(m + 0x40) = Slot(i);
    uint32_t node = 0x480000 + uint32_t(i) * 0x10, first = R32(L.watch_list + 4);
    G<uint32_t>(m + 0x14D0) = m + 0x1800;
    G<uint32_t>(node) = L.watch_list;
    G<uint32_t>(node + 4) = first;
    G<uint32_t>(node + 8) = Entry(i);
    G<uint32_t>(first) = node;
    G<uint32_t>(L.watch_list + 4) = node;
    G<uint32_t>(m) = L.model_vtable;
    G<uint32_t>(m + 0x2000) = kCollision;  // the collision objects: +0x114 (also at +0x128) and +0x11C
    G<uint32_t>(m + 0x2100) = kCollision;
    G<uint32_t>(m + 0x114) = m + 0x2000;
    G<uint32_t>(m + 0x128) = m + 0x2000;
    G<uint32_t>(m + 0x11C) = m + 0x2100;
    G<uint32_t>(m + 0x110) = m + 0x2200;  // the shapes
    G<uint32_t>(m + 0x118) = m + 0x2300;
    G<int32_t>(m + 0x124) = 7;
    G<uint32_t>(m + 0x120) = 0x40000 + uint32_t(i) * 0x100;
    G<uint32_t>(m + 0xFB0) = 0;
    G<uint32_t>(m + 0x1C) = R32(L.world);
    G<uint32_t>(L.world) = m;
    G<uint32_t>(r + 0xF8) = m;
}
void Die(int i) {
    G<int32_t>(Model(i) + 0x124) = -1;
}
bool InWorld(uint32_t m) {
    for (uint32_t o = R32(L.world); o; o = R32(o + 0x1C))
        if (o == m) return true;
    return false;
}
void LeaveWorld(uint32_t m) {
    uint32_t* at = reinterpret_cast<uint32_t*>(memory.data() + L.world);
    while (*at && *at != m) at = reinterpret_cast<uint32_t*>(memory.data() + *at + 0x1C);
    if (*at == m) *at = R32(m + 0x1C);
}

void Level(int32_t limit) {
    std::fill(memory.begin(), memory.end(), uint8_t(0));
    G<uint32_t>(L.model_vtable + 0x168) = kDeactivate;
    G<uint32_t>(L.model_vtable + 0x68) = kShown;
    G<uint32_t>(L.model_vtable + 0x64) = kInView;
    G<uint32_t>(L.model_vtable + 0x34) = kDraw;
    G<uint32_t>(kCollision) = kDelete;
    G<uint32_t>(L.model_pool + 0xC) = 0x2400 - 0x10;
    G<uint32_t>(L.model_pool + 0x10) = Slot(0);
    G<int32_t>(L.limit) = limit;
    G<uint32_t>(L.watch_list) = L.watch_list;
    G<uint32_t>(L.watch_list + 4) = L.watch_list;
    G<uint32_t>(L.model_pool) = game::ModelPoolCount(L.model_pool, uint32_t(limit), L);
    game::ForgetBodies();
    freed.clear();
    deactivated.clear();
    drawn.clear();
    deleted.clear();
    shapes.clear();
}
// the world's draw reaching its objects' end
void DrawWorld(X86Context& ctx, uint32_t world = L.world) {
    drawn.clear();
    ctx.esi = world;
    ctx.ebp = kView;
    game::DrawBodies(ctx, L);
}

}  // namespace

namespace kernel {
uint8_t* g_base = memory.data();
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == L.leave_world) {
        Check(ctx.ecx == L.world && count == 1, "world removal on the world");
        LeaveWorld(args[0]);
    } else if (fn == L.watch_leave) {
        Check(count == 1, "the watch list removal takes the entry");
        uint32_t node = R32(L.watch_list + 4);
        while (node != L.watch_list && R32(node + 8) != args[0]) node = R32(node + 4);
        Check(node != L.watch_list, "the removal finds its entry");
        Unlink(node);
    } else if (fn == kShown) {
        Check(count == 0, "shown takes nothing");
        return 1;
    } else if (fn == kInView) {
        Check(count == 1 && args[0] == kView, "in the view takes the view");
        return 1;
    } else if (fn == kDraw) {
        Check(count == 1 && args[0] == kView, "the draw takes the view");
        drawn.push_back(ctx.ecx);
    } else if (fn == kDelete) {
        Check(count == 1 && args[0] == 1, "the collision object's delete");
        deleted.push_back(ctx.ecx);
    } else if (fn == L.free_shape) {
        Check(count == 1, "the shape's free takes the shape");
        shapes.push_back(args[0]);
    } else if (fn == kDeactivate) {
        deactivated.push_back(ctx.ecx);
    } else if (fn == L.free_model) {
        Check(ctx.ecx == L.factory && count == 1, "free on the factory");
        freed.push_back(args[0]);
    } else {
        Check(false, "an unknown guest call");
    }
    return 0;  // the real CallGuest puts eax, ecx and edx back as they were at the call
}
}  // namespace kernel
namespace mem {
// one block for the body brain, at 0x5F0000
uint32_t g_block = 0, g_block_size = 0;
uint32_t AllocSystem(uint32_t size, uint32_t) {
    g_block = 0x5F0000;
    g_block_size = size;
    return g_block;
}
uint32_t AllocationSize(uint32_t va) { return va == g_block ? g_block_size : 0; }
}  // namespace mem
namespace logx { void Write(Level, const char*, ...) {} }
namespace game {
Options& Opt() {
    static Options o;
    return o;
}
}  // namespace game

int main() {
    X86Context ctx{};
    game::Options& o = game::Opt();

    Check(game::ModelPoolCount(L.model_pool, 15, L) == 15 + L.spare, "the soldier model pool gets the spares");
    Check(game::ModelPoolCount(L.model_pool + 0x20, 5, L) == 5, "the other pools keep their count");

    // off: nothing changes
    Level(4);
    Spawn(0);
    Die(0);
    G<uint32_t>(Model(0) + 0xFB0) = 2;
    game::KeepBodyFade(Model(0), L);
    ctx.esi = Model(0);
    Check(R32(Model(0) + 0xFB0) == 2 && !game::KeepBodyInWorld(ctx, L), "off: the fade runs, the model leaves");
    ctx.esi = Record(0);
    Check(!game::KeepBodyModel(ctx, L), "off: the record destroy frees the model");

    // on: a dead soldier's model stays, a living one's goes
    o.keep_bodies = true;
    Level(4);
    Spawn(0);
    Spawn(1);
    Die(0);
    G<uint32_t>(Model(0) + 0xFB0) = 2;
    G<uint32_t>(Model(1) + 0xFB0) = 2;
    game::KeepBodyFade(Model(0), L);
    game::KeepBodyFade(Model(1), L);
    Check(R32(Model(0) + 0xFB0) == 0 && R32(Model(1) + 0xFB0) == 2, "a dead soldier's model does not fade");
    ctx.esi = Model(0);
    Check(game::KeepBodyInWorld(ctx, L), "a dead soldier's released model stays in the world");
    ctx.esi = Model(1);
    Check(!game::KeepBodyInWorld(ctx, L), "a living soldier's released model leaves");
    ctx.esi = Record(0);
    Check(game::KeepBodyModel(ctx, L) && freed.empty(), "the record goes, its model stays");
    Check(!InWorld(Model(0)) && InWorld(Model(1)), "the body leaves the world");
    {
        uint32_t m = Model(0);
        Check(deleted.size() == 2 && deleted[0] == m + 0x2000 && deleted[1] == m + 0x2100 && shapes.size() == 1 &&
                  shapes[0] == m + 0x2200,
              "the body's collision objects and its first shape go back to their pools");
        Check(!R32(m + 0x110) && !R32(m + 0x114) && !R32(m + 0x11C) && !R32(m + 0x128),
              "and the body points at none of them");
        Check(R32(m + 0x118) == m + 0x2300, "the shape the shadow reads stays");
    }
    uint32_t brain = R32(Model(0) + 0x120);
    Check(brain && R32(brain) == 0 && R32(brain + 0x20) == 0 && R32(brain + 0x24) == 0xFFFFFFFFu,
          "the body's brain: an ended script with no class and no owner");
    Check(!game::OnWatchList(Entry(0), L) && game::OnWatchList(Entry(1), L),
          "a body's AI controller leaves the watch list, a living soldier's stays");
    DrawWorld(ctx);
    Check(drawn.size() == 1 && drawn[0] == Model(0), "the world's draw draws the body");
    DrawWorld(ctx, L.world + 0x40);
    Check(drawn.empty(), "another world's draw does not");
    ctx.esi = Record(1);
    Check(!game::KeepBodyModel(ctx, L), "a living soldier's record destroy frees its model");

    // no more bodies than spare models: the oldest goes first, the registers put back
    Level(2);
    for (uint32_t i = 0; i <= L.spare; i++) {
        Spawn(int(i));
        Die(int(i));
        ctx.esi = Record(int(i));
        ctx.eax = 7;
        ctx.ecx = 0x55;
        Check(game::KeepBodyModel(ctx, L), "a body is kept");
    }
    Check(freed.size() == 1 && freed[0] == Model(0) && deactivated.size() == 1 && deactivated[0] == Model(0),
          "past the spares the oldest body is freed");
    Check(R32(L.world) == 0, "every body left the world");
    DrawWorld(ctx);
    Check(drawn.size() == L.spare && drawn[0] == Model(1), "the kept bodies are drawn, oldest first");
    Check(ctx.eax == 7 && ctx.ecx == 0x55, "registers are put back");
    Check(R32(L.watch_list + 4) == L.watch_list, "no body is on the watch list");

    // no spares (the larger pool could not be had): the game's way
    Level(4);
    G<uint32_t>(L.model_pool) = 4;
    Spawn(0);
    Die(0);
    ctx.esi = Model(0);
    Check(!game::KeepBodyInWorld(ctx, L), "no spares: the model leaves");
    ctx.esi = Record(0);
    Check(!game::KeepBodyModel(ctx, L), "no spares: the model is freed");

    // the level's shutdown frees the bodies and keeps none while it runs
    Level(4);
    for (int i = 0; i < 3; i++) {
        Spawn(i);
        Die(i);
        ctx.esi = Record(i);
        game::KeepBodyModel(ctx, L);
    }
    game::BodiesShutdown(ctx, L, true);
    Check(freed.size() == 3, "the shutdown frees every body");
    Spawn(3);
    Die(3);
    ctx.esi = Record(3);
    Check(!game::KeepBodyModel(ctx, L), "none kept while shutting down");
    game::BodiesShutdown(ctx, L, false);

    // turned off: the bodies go at the next pass
    Level(4);
    Spawn(0);
    Die(0);
    ctx.esi = Record(0);
    game::KeepBodyModel(ctx, L);
    o.keep_bodies = false;
    game::KeepBodiesPass(ctx, L);
    Check(freed.size() == 1 && !InWorld(Model(0)), "turned off, every body goes");
    o.keep_bodies = true;

    // a quick load: the bodies are the dead soldier models in the pool with the body brain
    Level(4);
    Spawn(0);
    Spawn(1);
    Spawn(2);
    Die(0);
    Die(1);
    ctx.esi = Record(0);
    game::KeepBodyModel(ctx, L);         // the body brain
    game::ForgetBodies();                // the host's list lost with the load
    G<uint32_t>(Model(2) + 0x120) = 0;  // alive (team 7): a model before its brain is set
    game::BodiesAfterLoad();
    game::KeepBodiesPass(ctx, L);
    DrawWorld(ctx);
    Check(drawn.size() == 1 && drawn[0] == Model(0), "after a load the body is drawn again");
    o.keep_bodies = false;
    game::KeepBodiesPass(ctx, L);
    Check(freed.size() == 1 && freed[0] == Model(0), "after a load only the dead model with the body brain is a body");

    std::puts("keep bodies: fade off, kept past its record out of the world and drawn, off the watch list, collision "
              "freed, spares never exceeded, shutdown, off and quick load");
}

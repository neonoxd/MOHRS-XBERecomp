// Host test: Keep Bodies (src/common/keep_bodies.h) on a fake soldier list, with the record destroy stubbed.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keep_bodies_test.cpp
//     src/common/keep_bodies.cpp -o re/keep_bodies_test.exe
#include "common/keep_bodies.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

std::array<uint8_t, 0x300000> memory{};
std::vector<uint32_t> destroyed;

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
using kernel::R32;
const game::BodyLayout& L = game::kDefaultBodies;

// records at 0x10000 + i * 0x200, linked newest first as the spawn does
uint32_t Record(int i) { return 0x10000 + uint32_t(i) * 0x200; }

void Spawn(int i, float hit_points) {
    uint32_t r = Record(i);
    G<uint32_t>(r) = L.record_vtable;
    G<float>(r + 0x14) = hit_points;
    uint32_t head = R32(L.list);
    G<uint32_t>(r + 0xC) = head;
    G<uint32_t>(r + 0x10) = 0;
    if (head) G<uint32_t>(head + 0x10) = r;
    G<uint32_t>(L.list) = r;
    G<int32_t>(L.count) += 1;
}

void Unlink(uint32_t r) {
    uint32_t next = R32(r + 0xC), prev = R32(r + 0x10);
    if (prev) G<uint32_t>(prev + 0xC) = next;
    if (next) G<uint32_t>(next + 0x10) = prev;
    if (R32(L.list) == r) G<uint32_t>(L.list) = next;
}

// the record destroy as the hooks have it: the override forgets the body, the game unlinks it and lowers the count
void Destroy(uint32_t r) {
    game::BodyDestroyed(r);
    Unlink(r);
    G<int32_t>(L.count) -= 1;
    destroyed.push_back(r);
}

// the deferred-delete pass reaching a record: the hook keeps it, or the game destroys it
void Expire(X86Context& ctx, uint32_t r) {
    ctx.ecx = r;
    if (!game::KeepBody(ctx, L)) Destroy(r);
}

void EmptyMap();

void Level(int32_t limit) {
    game::ForgetBodies();
    EmptyMap();
    G<uint32_t>(L.list) = 0;
    G<int32_t>(L.count) = 0;
    G<int32_t>(L.limit) = limit;
    destroyed.clear();
}

int32_t Count() { return G<int32_t>(L.count); }

// the deferred-delete map: object at 0x20000, head node 0x20100, nodes from 0x20200 hung in a right-leaning chain
// under a left child, so the walk has to follow both
constexpr uint32_t kMap = 0x20000, kHead = 0x20100;
uint32_t g_nodes = 0;
void EmptyMap() {
    G<uint32_t>(L.delete_map) = kMap;
    G<uint32_t>(kMap) = kHead;
    G<uint32_t>(kMap + 4) = 0;
    G<uint32_t>(kHead + 4) = 0;
    g_nodes = 0;
}
void Queue(uint32_t object) {
    uint32_t node = 0x20200 + g_nodes * 0x20;
    G<uint32_t>(node + 8) = 0;
    G<uint32_t>(node + 0xC) = 0;
    G<uint32_t>(node + 0x10) = object;
    if (g_nodes == 0) G<uint32_t>(kHead + 4) = node;
    else if (g_nodes == 1) G<uint32_t>(0x20200 + 8) = node;
    else G<uint32_t>(0x20200 + (g_nodes - 1) * 0x20 + 0xC) = node;
    g_nodes++;
    G<uint32_t>(kMap + 4) = g_nodes;
}

}  // namespace

namespace kernel {
uint8_t* g_base = memory.data();
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t*, size_t) {
    Check(fn == L.destroy, "only the record destroy is called");
    Destroy(ctx.ecx);
    ctx.eax = 0xDEAD;  // the hooks must put back what the call changed
    return 0;
}
}  // namespace kernel
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

    // off: the game's own removal
    Level(6);
    Spawn(0, 0);
    Expire(ctx, Record(0));
    Check(destroyed.size() == 1 && Count() == 0, "off: a dead soldier goes");

    // on: only dead soldier records are kept
    o.keep_bodies = true;
    Level(6);
    Spawn(0, 0);
    Spawn(1, 50);
    Spawn(2, 0);
    G<uint32_t>(Record(2)) = 0x1234;  // another kind of object
    Expire(ctx, Record(0));
    Expire(ctx, Record(1));
    Expire(ctx, Record(2));
    Check(destroyed.size() == 2 && destroyed[0] == Record(1) && destroyed[1] == Record(2), "on: only the dead soldier is kept");
    Check(game::KeptBody(Record(0)) && !game::KeptBody(Record(1)), "the kept body is known");
    Check(Count() == 1, "a kept body still counts");

    // a death that leaves the record's hit points: the model's team cleared (-1) marks it
    Level(6);
    Spawn(0, 16);
    Spawn(1, 16);
    G<uint32_t>(Record(0) + 0xF8) = 0x30000;
    G<int32_t>(0x30000 + 0x124) = -1;
    G<uint32_t>(Record(1) + 0xF8) = 0x30400;
    G<int32_t>(0x30400 + 0x124) = 7;
    ctx.esi = Record(0);
    Check(game::KeepBodyModel(ctx, L), "a cleared team marks a dead soldier");
    ctx.esi = Record(1);
    Check(!game::KeepBodyModel(ctx, L), "a soldier on a team with hit points left is alive");

    // the script's removal keeps a dead soldier's model in the world and the record with it
    Level(6);
    Spawn(0, 0);
    Spawn(1, 30);
    ctx.esi = Record(0);
    Check(game::KeepBodyModel(ctx, L) && game::KeptBody(Record(0)), "a dead soldier's model stays, its record kept");
    ctx.esi = Record(1);
    Check(!game::KeepBodyModel(ctx, L) && !game::KeptBody(Record(1)), "a living soldier's model goes as before");
    Expire(ctx, Record(0));
    Check(destroyed.empty() && Count() == 2, "the kept record is not destroyed when it falls due");
    // the death fade: off for a dead soldier's model (team -1), left for others
    constexpr uint32_t kDead = 0x50000, kLiving = 0x58000;
    G<uint32_t>(kDead) = L.model_vtable;
    G<int32_t>(kDead + 0x124) = -1;
    G<uint32_t>(kDead + 0xFB0) = 2;
    G<uint32_t>(kLiving) = L.model_vtable;
    G<int32_t>(kLiving + 0x124) = 7;
    G<uint32_t>(kLiving + 0xFB0) = 2;
    game::KeepBodyFade(kDead, L);
    game::KeepBodyFade(kLiving, L);
    Check(R32(kDead + 0xFB0) == 0 && R32(kLiving + 0xFB0) == 2, "a dead soldier's model does not fade, others do");
    o.keep_bodies = false;
    G<uint32_t>(kDead + 0xFB0) = 2;
    game::KeepBodyFade(kDead, L);
    Check(R32(kDead + 0xFB0) == 2, "off: the fade runs");
    o.keep_bodies = true;
    o.keep_bodies = false;
    ctx.esi = Record(0);
    Check(!game::KeepBodyModel(ctx, L), "off: the model goes as before");
    o.keep_bodies = true;

    // the pass leaves bodies alone below the limit, and frees the oldest ones at it
    Level(4);
    for (int i = 0; i < 3; i++) Spawn(i, 0);
    for (int i = 0; i < 3; i++) Expire(ctx, Record(i));
    ctx.eax = 7;
    ctx.ecx = 0x55;
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.empty() && Count() == 3, "below the limit nothing goes");
    Spawn(3, 100);
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.size() == 1 && destroyed[0] == Record(0) && Count() == 3, "at the limit the oldest body goes");
    Check(ctx.eax == 7 && ctx.ecx == 0x55, "registers are put back");

    // a spawn at the limit takes one body's slot
    Spawn(4, 100);
    Check(Count() == 4, "the limit reached again");
    game::KeepBodiesSpawn(ctx, L);
    Check(destroyed.size() == 2 && destroyed[1] == Record(1) && Count() == 3, "a spawn at the limit frees one body");
    game::KeepBodiesSpawn(ctx, L);
    Check(destroyed.size() == 2, "a spawn below the limit frees none");

    // the cull for queued spawns takes bodies before soldiers
    Level(5);
    for (int i = 0; i < 4; i++) Spawn(i, 0);
    for (int i = 0; i < 4; i++) Expire(ctx, Record(i));
    game::KeepBodiesCull(ctx, L, 3);  // 3 + 4 - 5 = 2 to remove
    Check(destroyed.size() == 2 && Count() == 2, "the cull's room comes from the oldest bodies");
    game::KeepBodiesCull(ctx, L, 1);
    Check(destroyed.size() == 2, "a cull with room takes none");

    // a body that left the list or came back alive is not destroyed again
    Level(3);
    Spawn(0, 0);
    Spawn(1, 0);
    Spawn(2, 0);
    for (int i = 0; i < 3; i++) Expire(ctx, Record(i));
    Unlink(Record(0));
    G<float>(Record(1) + 0x14) = 80;
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.size() == 1 && destroyed[0] == Record(2), "only a listed dead body is destroyed");

    // a level start forgets the bodies
    Level(3);
    Spawn(0, 0);
    Expire(ctx, Record(0));
    game::ForgetBodies();
    Check(!game::KeptBody(Record(0)), "forgotten at a level start");

    // a body the game queued again is left to the game
    Level(2);
    Spawn(0, 0);
    Spawn(1, 0);
    Expire(ctx, Record(0));
    Expire(ctx, Record(1));
    Queue(Record(0));
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.size() == 1 && destroyed[0] == Record(1), "a queued body is not destroyed twice");

    // a quick load: dead records not in the map are the kept bodies, oldest first
    Level(4);
    for (int i = 0; i < 4; i++) Spawn(i, i == 2 ? 60.0f : 0.0f);
    Queue(0x9999);
    Queue(0x8888);
    Queue(Record(3));  // released by its script, the game destroys it
    game::BodiesAfterLoad();
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.size() == 1 && destroyed[0] == Record(0), "after a load the oldest unqueued body goes first");
    Check(game::KeptBody(Record(1)) && !game::KeptBody(Record(2)) && !game::KeptBody(Record(3)),
          "after a load: the dead, unqueued records are kept");

    // turned off: the kept bodies go at the next pass
    Level(6);
    Spawn(0, 0);
    Spawn(1, 0);
    Expire(ctx, Record(0));
    Expire(ctx, Record(1));
    o.keep_bodies = false;
    game::KeepBodiesPass(ctx, L);
    Check(destroyed.size() == 2 && Count() == 0, "turned off, every kept body goes");

    std::puts("keep bodies: kept, released at the limit, at a spawn and before the cull, checked against the list and the "
              "map, found again after a load, forgotten");
}

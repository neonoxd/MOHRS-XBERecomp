// Host test: a checkpoint's weapon slots (src/common/checkpoint_weapons.h) as the checkpoint save (0xDB210) and the
// restore (0xDE660) call them, on a fake game state and player ring.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/checkpoint_weapons_test.cpp
//     src/common/checkpoint_weapons.cpp -o re/checkpoint_weapons_test.exe
#include "common/checkpoint_weapons.h"
#include "kernel/kernel.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
std::vector<uint8_t> memory(0x400000);

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
using kernel::R32;
const game::CheckpointLayout& L = game::kDefaultCheckpoint;
constexpr uint32_t kState = 0x100000, kStack = 0x80000, kRet = 0xDB2C2;
constexpr uint32_t kPlayer[2] = {0x20000, 0x28000};
constexpr uint32_t kHolders = 14, kGrenades = 0x500;  // the grenades (+0xE74): an id in no holder

// player p's holder i (id 0x100 + i) at 0x40000 + p * 0x10000 + i * 0x400, its weapon 0x200 after it
uint32_t Holder(int p, uint32_t i) { return 0x40000 + uint32_t(p) * 0x10000 + i * 0x400; }
uint32_t Weapon(int p, uint32_t i) { return Holder(p, i) + 0x200; }
uint32_t Id(uint32_t i) { return 0x100 + i; }

void Setup(uint32_t players) {
    std::fill(memory.begin(), memory.end(), uint8_t(0));
    G<uint32_t>(L.state) = kState;
    G<uint32_t>(L.player_count) = players;
    for (int p = 0; p < 2; p++) {
        G<uint32_t>(L.players + uint32_t(p) * 4) = kPlayer[p];
        G<uint32_t>(kPlayer[p] + L.index) = uint32_t(p);
        G<uint32_t>(kPlayer[p] + L.ring) = Holder(p, 0);
        for (uint32_t i = 0; i < kHolders; i++) {
            G<uint32_t>(Holder(p, i) + 0x2BC) = Weapon(p, i);
            G<uint32_t>(Holder(p, i) + 0x2B4) = Holder(p, (i + 1) % kHolders);
            G<uint32_t>(Weapon(p, i) + L.weapon_id) = Id(i);
        }
    }
}

void Own(int p, uint32_t i, uint16_t a, uint16_t b) {
    G<uint8_t>(Holder(p, i) + 0x3C0) |= 4;
    G<uint16_t>(Weapon(p, i) + 0x2FE) = a;
    G<uint16_t>(Weapon(p, i) + 0x2FC) = b;
}
void Drop(int p, uint32_t i) {
    G<uint8_t>(Holder(p, i) + 0x3C0) &= ~4;
    G<uint16_t>(Weapon(p, i) + 0x2FE) = 0;
    G<uint16_t>(Weapon(p, i) + 0x2FC) = 0;
}

// a thiscall on the game state's holder with these stack arguments; checks the arguments are popped
X86Context Call(void (*fn)(X86Context&, const game::CheckpointLayout&), std::initializer_list<uint32_t> args) {
    X86Context ctx{};
    ctx.ecx = L.state;
    ctx.esp = kStack;
    G<uint32_t>(kStack) = kRet;
    uint32_t at = kStack + 4;
    for (uint32_t a : args) G<uint32_t>(at) = a, at += 4;
    fn(ctx, L);
    Check(ctx.esp == kStack + 4 + 4 * uint32_t(args.size()), "the arguments popped");
    return ctx;
}

// the checkpoint save of player p as 0xDB210 makes it: each holder of the map, then the grenades
void Save(int p) {
    game::CheckpointSaveStart(kPlayer[p], L);
    for (uint32_t i = 0; i < kHolders; i++) {
        uint32_t w = Weapon(p, i);
        Call(game::CheckpointStore, {Id(i), G<uint16_t>(w + 0x2FE), G<uint16_t>(w + 0x2FC), uint32_t(p)});
        uint32_t usable = (G<uint8_t>(Holder(p, i) + 0x3C0) & 4) ? 1 : 0;
        Call(game::CheckpointSetUsable, {Id(i), usable, uint32_t(p)});
    }
    Call(game::CheckpointStore, {kGrenades, 3, 0, uint32_t(p)});
}

struct Loaded {
    uint32_t a, b;
    bool usable;
};
Loaded Load(int p, uint32_t id) {
    constexpr uint32_t kOutA = 0x90000, kOutB = 0x90004;
    G<uint32_t>(kOutA) = G<uint32_t>(kOutB) = 0xDEAD;
    Call(game::CheckpointCounts, {id, kOutA, kOutB, uint32_t(p)});
    X86Context ctx = Call(game::CheckpointUsable, {id, uint32_t(p)});
    return {R32(kOutA), R32(kOutB), (ctx.eax & 0xFF) != 0};
}
}  // namespace

namespace kernel { uint8_t* g_base = memory.data(); }

int main() {
    // single player: 14 weapons in the level, the player's past the first 8
    Setup(1);
    Check(game::CheckpointSlots(0, L) == 16, "single player: both players' slots");
    Own(0, 2, 30, 90);
    Own(0, 9, 8, 40);
    Own(0, 11, 1, 2);
    Own(0, 13, 0, 0);  // owned, no ammo left
    G<uint16_t>(Weapon(0, 12) + 0x2FE) = 20;  // not owned, but ammo for it
    Save(0);
    Loaded l = Load(0, Id(2));
    Check(l.a == 30 && l.b == 90 && l.usable, "an owned weapon comes back with its counts");
    Check(Load(0, Id(9)).usable && Load(0, Id(11)).usable, "owned weapons past the eighth come back");
    l = Load(0, Id(13));
    Check(l.usable && l.a == 0 && l.b == 0, "an owned weapon with no ammo comes back");
    l = Load(0, Id(12));
    Check(!l.usable && l.a == 20, "ammo for a weapon not owned is kept");
    l = Load(0, Id(5));
    Check(!l.usable && l.a == 0 && l.b == 0, "a weapon not owned loads as none, no ammo");
    Check(Load(0, kGrenades).a == 3, "the grenades are kept");
    uint32_t used = 0;
    for (uint32_t s = 0; s < 16; s++) used += R32(kState + L.slots + s * 12) != 0;
    Check(used == 6, "only the weapons with something to keep take slots");

    // the next checkpoint after the player lost one: not kept from the earlier one
    Drop(0, 9);
    Save(0);
    Check(!Load(0, Id(9)).usable && Load(0, Id(9)).a == 0, "a weapon gone since the last checkpoint is gone");
    Check(Load(0, Id(11)).usable, "the others stay");

    // every weapon owned: 15 to keep in 16 slots, nothing written past them
    Setup(1);
    for (uint32_t i = 0; i < kHolders; i++) Own(0, i, 1, 1);
    G<uint32_t>(kState + 0x3173C) = 0x12345678;  // the players' health after the slots
    Save(0);
    Check(Load(0, Id(13)).usable, "14 weapons and the grenades fit");
    Check(R32(kState + 0x3173C) == 0x12345678, "nothing past the second player's slots");

    // two players: 8 slots each, apart
    Setup(2);
    Check(game::CheckpointSlots(0, L) == 8 && game::CheckpointSlots(1, L) == 8, "two players: 8 slots each");
    Own(0, 1, 5, 5);
    Own(1, 3, 7, 7);
    Save(0);
    Save(1);
    Check(Load(0, Id(1)).usable && !Load(0, Id(3)).usable, "player 1's own weapons");
    Check(Load(1, Id(3)).usable && Load(1, Id(3)).a == 7 && !Load(1, Id(1)).usable, "player 2's own weapons");
    Save(0);
    Check(Load(1, Id(3)).usable, "a save of player 1 leaves player 2's slots");
    // more than 8 to keep: the first 8 by the map's order, as the game keeps them, and player 2's slots untouched
    for (uint32_t i = 0; i < kHolders; i++) Own(0, i, 1, 1);
    Save(0);
    Check(Load(0, Id(7)).usable && !Load(0, Id(8)).usable, "player 1: the first 8");
    Check(Load(1, Id(3)).usable && Load(1, Id(3)).a == 7 && !Load(1, Id(0)).usable, "player 2's slots untouched");

    std::printf("checkpoint_weapons_test: all passed\n");
    return 0;
}

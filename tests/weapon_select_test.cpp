// Host test: weapon keys through the switch query and the weapon select, on a fake player and weapon ring.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/weapon_select_test.cpp
//     src/common/weapon_select.cpp -o re/weapon_select_test.exe
#include "common/weapon_select.h"
#include "kernel/kernel.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
std::array<uint8_t, 0x300000> memory{};
int key = 0;

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
using kernel::R32;
const game::WeaponLayout& L = game::kDefaultWeapons;
constexpr uint32_t kPlayers = L.players, kManager = L.input_manager, kPlayer = 0x10000, kStack = 0x70000;
constexpr uint32_t kHolders[] = {0x20000, 0x21000, 0x22000, 0x23000};
constexpr uint32_t kWeapons[] = {0x30000, 0x31000, 0x32000, 0x33000};
constexpr uint32_t kIds[] = {0x400100, 0x400200, 0x400300, 0x400400};

void Setup() {
    memory.fill(0);
    G<uint32_t>(kPlayers) = kPlayer;
    G<uint8_t>(kManager + 0x23E0) = 2;
    G<uint8_t>(kManager) = 0;              // entry 0: KBM's port
    G<uint8_t>(kManager + 0x8F8) = 1;      // entry 1: a controller
    G<uint32_t>(kPlayers + 4) = kPlayer + 0x4000;
    for (int i = 0; i < 4; i++) {
        G<uint32_t>(kHolders[i] + 0x2BC) = kWeapons[i];
        G<uint32_t>(kHolders[i] + 0x2B4) = kHolders[(i + 1) % 4];
        G<uint32_t>(kHolders[i] + 0x2B0) = kHolders[(i + 3) % 4];
        G<uint8_t>(kHolders[i] + 0x3C0) = 4;
        G<uint32_t>(kWeapons[i] + L.id) = kIds[i];
    }
    G<uint8_t>(kHolders[1] + 0x3C0) = 0;  // not picked up: not counted
    G<uint32_t>(kPlayer + L.ring) = kHolders[0];
    G<uint32_t>(kPlayer + L.holder) = kHolders[0];
}

X86Context Query(uint32_t index, uint32_t action, uint32_t answer) {
    X86Context ctx{};
    ctx.eax = answer;
    game::WeaponSwitchQuery(ctx, index, action, L);
    return ctx;
}

// the select as NextWeapon calls it: returns the id it would be given
uint32_t Select(uint32_t player, uint32_t ret = 0x33D09, uint32_t id = 0) {
    X86Context ctx{};
    ctx.ecx = player;
    ctx.esp = kStack;
    G<uint32_t>(kStack) = ret;
    G<uint32_t>(kStack + 4) = 1;
    G<uint32_t>(kStack + 8) = id;
    game::WeaponSelect(ctx, L);
    return R32(kStack + 8);
}
}  // namespace

namespace kernel { uint8_t* g_base = memory.data(); }
namespace logx { void Write(Level, const char*, ...) {} }
namespace input { int TakeWeaponKey() { int k = key; key = 0; return k; } }

int main() {
    Setup();
    Check(Query(0, 0x12, 0).eax == 0 && Select(kPlayer) == 0, "no key: the game's switch as it was");

    key = 2;
    Check(Query(0, 0x12, 0).eax == 1, "key 2 reads as a switch press");
    Check(Select(kPlayer) == kIds[2], "places count usable weapons only: the second is the third holder");
    Check(Select(kPlayer) == 0, "the request is used once");

    key = 3;
    Check(Query(0, 0x11, 0).eax == 0 && key == 3, "other actions leave the key");
    Check(Query(0, 0x12, 0).eax == 1, "key 3");
    Check(Select(kPlayer, 0xE00AA) == 0, "a select from elsewhere is left alone");
    Check(Select(kPlayer) == kIds[3], "and NextWeapon's still goes to key 3: the fourth holder");

    key = 1;
    Check(Query(0, 0x12, 0).eax == 0, "the current weapon: no switch");
    key = 4;
    Check(Query(0, 0x12, 0).eax == 0, "no fourth usable weapon: no switch");
    key = 2;
    Check(Query(1, 0x12, 0).eax == 0 && key == 2, "a controller's player is not KBM's");
    Check(Query(0, 0x12, 1).eax == 1 && key == 2, "a real switch press stays as it is");

    G<uint8_t>(kPlayer + 0xD46) = 1;
    Check(Query(0, 0x12, 0).eax == 0, "on a mounted gun: nothing");
    G<uint8_t>(kPlayer + 0xD46) = 0;
    G<uint32_t>(kPlayer + L.holder) = 0;
    key = 2;
    Check(Query(0, 0x12, 0).eax == 0, "unarmed: nothing");
    G<uint32_t>(kPlayer + L.holder) = kHolders[0];

    key = 2;
    Query(0, 0x12, 0);
    G<uint8_t>(kHolders[2] + 0x3C0) = 0;
    Check(Select(kPlayer) == 0, "a weapon gone before the select: the game's next weapon");
    G<uint8_t>(kHolders[2] + 0x3C0) = 4;

    // type 7 counts by its ammo
    G<uint32_t>(kWeapons[2] + L.type) = 7;
    key = 2;
    Check(Query(0, 0x12, 0).eax == 1 && Select(kPlayer) == kIds[3], "an empty type 7 is skipped in the numbering");
    G<int16_t>(kWeapons[2] + L.count_a) = 3;
    key = 2;
    Check(Query(0, 0x12, 0).eax == 1 && Select(kPlayer) == kIds[2], "a type 7 with ammo is counted");

    key = 2;
    Query(0, 0x12, 0);
    Check(Select(kPlayer + 0x4000) == 0 && Select(kPlayer) == kIds[2], "another player's select keeps the request");

    std::puts("weapon select: all checks passed");
}

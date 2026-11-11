// Host test: weapon keys through the switch query and the weapon select, on a fake player and weapon ring.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/weapon_select_test.cpp
//     src/common/weapon_select.cpp -o re/weapon_select_test.exe
#include "common/weapon_select.h"
#include "common/mouse_keyboard.h"
#include "kernel/kernel.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
std::array<uint8_t, 0x300000> memory{};
int key = 0, steps = 0;
int scoped = -1;  // the last SetScoped, -1 none

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
using kernel::R32;
const game::WeaponLayout& L = game::kDefaultWeapons;
constexpr uint32_t kPlayers = L.players, kManager = L.input_manager, kPlayer = 0x10000, kStack = 0x70000;
constexpr int kPrimary = 1, kSecondary = 2, kSpecial = 3;

// the ring as the game keeps it, sorted by WeaponType (0xDF320)
enum Slot { Colt, Welrod, Garand, Thompson, BAR, Bazooka, Grenades, kSlots };
constexpr uint32_t kTypes[kSlots] = {1, 1, 3, 5, 5, 6, 7};
uint32_t Holder(int i) { return 0x20000 + i * 0x1000; }
uint32_t Weapon(int i) { return 0x40000 + i * 0x1000; }
uint32_t Id(int i) { return 0x400100 + i * 0x100; }

void Setup() {
    memory.fill(0);
    G<uint32_t>(kPlayers) = kPlayer;
    G<uint8_t>(kManager + 0x23E0) = 2;
    G<uint8_t>(kManager) = 0;              // entry 0: KBM's port
    G<uint8_t>(kManager + 0x8F8) = 1;      // entry 1: a controller
    G<uint32_t>(kPlayers + 4) = kPlayer + 0x4000;
    for (int i = 0; i < kSlots; i++) {
        G<uint32_t>(Holder(i) + 0x2BC) = Weapon(i);
        G<uint32_t>(Holder(i) + 0x2B4) = Holder((i + 1) % kSlots);
        G<uint32_t>(Holder(i) + 0x2B0) = Holder((i + kSlots - 1) % kSlots);
        G<uint8_t>(Holder(i) + 0x3C0) = 4;
        G<uint32_t>(Weapon(i) + L.id) = Id(i);
        G<uint32_t>(Weapon(i) + L.type) = kTypes[i];
    }
    G<uint8_t>(Holder(Welrod) + 0x3C0) = 0;  // not picked up
    G<int16_t>(Weapon(Grenades) + L.count_a) = 2;
    G<uint32_t>(kPlayer + L.ring) = Holder(Colt);
    G<uint32_t>(kPlayer + L.holder) = Holder(Garand);
}

void Holding(int i) { G<uint32_t>(kPlayer + L.holder) = Holder(i); }

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

// a key pressed in play: the id the switch it starts ends at, 0 for no switch
uint32_t Press(int k) {
    key = k;
    if (Query(0, 0x12, 0).eax != 1) return 0;
    return Select(kPlayer);
}

// wheel notches (next positive): the same
uint32_t Scroll(int n) {
    steps = n;
    if (Query(0, 0x12, 0).eax != 1) return 0;
    return Select(kPlayer);
}
}  // namespace

namespace kernel { uint8_t* g_base = memory.data(); }
namespace logx { void Write(Level, const char*, ...) {} }
namespace input {
WeaponRequest TakeWeaponRequest() {
    WeaponRequest r{key, steps};
    key = steps = 0;
    return r;
}
void SetScoped(bool s) { scoped = s; }
}  // namespace input

int main() {
    Setup();
    Check(Query(0, 0x12, 0).eax == 0 && Select(kPlayer) == 0, "no key: the game's switch as it was");

    // holding the Garand
    Check(Press(kSecondary) == Id(Colt), "secondary: the pistol, past one not picked up");
    Check(Press(kPrimary) == Id(Thompson), "primary from a primary: the next one");
    Holding(Thompson);
    Check(Press(kPrimary) == Id(BAR), "and the next");
    Holding(BAR);
    Check(Press(kPrimary) == Id(Garand), "wrapping to the first");
    Holding(Colt);
    Check(Press(kPrimary) == Id(Garand), "primary from a pistol: the first primary");
    Check(Press(kSecondary) == 0, "the only pistol, held: nothing");
    Check(Press(kSpecial) == Id(Bazooka), "special: the launcher first");
    Holding(Bazooka);
    Check(Press(kSpecial) == Id(Grenades), "then grenades with ammo");
    G<int16_t>(Weapon(Grenades) + L.count_a) = 0;
    Check(Press(kSpecial) == 0, "empty grenades are skipped: the only special, held");
    G<int16_t>(Weapon(Grenades) + L.count_a) = 2;

    // a second press before the script's select
    Holding(Garand);
    key = kPrimary;
    Check(Query(0, 0x12, 0).eax == 1, "first press starts the switch");
    key = kPrimary;
    Check(Query(0, 0x12, 0).eax == 0 && key == 0, "second press starts no second switch");
    Check(Select(kPlayer) == Id(BAR), "and the switch under way goes one further");
    key = kSecondary;
    Query(0, 0x12, 0);
    key = kPrimary;
    Check(Query(0, 0x12, 0).eax == 0, "back to the group held: no switch");
    Check(Select(kPlayer) == Id(Colt), "the switch under way keeps its weapon, never the one held");

    key = kSecondary;
    Query(0, 0x12, 0);
    Check(Select(kPlayer, 0xE00AA) == 0, "a select from elsewhere is left alone");
    Check(Select(kPlayer) == Id(Colt), "and NextWeapon's still gets the request");

    key = kSecondary;
    Check(Query(0, 0x11, 0).eax == 0 && key == kSecondary, "other actions leave the key");
    Check(Query(1, 0x12, 0).eax == 0 && key == kSecondary, "a controller's player is not KBM's");
    Check(Query(0, 0x12, 1).eax == 1 && key == kSecondary, "a real switch press stays as it is");

    G<uint8_t>(kPlayer + 0xD46) = 1;
    Check(Press(kSecondary) == 0, "on a mounted gun: nothing");
    G<uint8_t>(kPlayer + 0xD46) = 0x10;
    Check(Press(kSecondary) == Id(Colt), "getting onto a gun is still on foot");
    G<uint8_t>(kPlayer + 0xD46) = 0;
    G<uint32_t>(kPlayer + L.holder) = 0;
    Check(Press(kSecondary) == Id(Colt), "no current holder, as a level starts: the group's first");
    Check(Press(kPrimary) == Id(Garand), "for each group");
    Holding(Garand);

    // the wheel: every usable weapon, either way (Colt, Garand, Thompson, BAR, Bazooka, Grenades; no Welrod)
    Holding(Garand);
    Check(Scroll(1) == Id(Thompson), "next");
    Check(Scroll(-1) == Id(Colt), "previous");
    Holding(Colt);
    Check(Scroll(-1) == Id(Grenades), "previous wraps to the last");
    Holding(Grenades);
    Check(Scroll(1) == Id(Colt), "next wraps to the first");
    Check(Scroll(3) == Id(Thompson), "notches add up");
    Check(Scroll(6) == 0, "all the way round: nothing");
    steps = 1;
    Check(Query(0, 0x12, 0).eax == 1, "a notch starts the switch");
    steps = 1;
    Check(Query(0, 0x12, 0).eax == 0 && Select(kPlayer) == Id(Garand), "a second before the select goes one further");
    G<uint32_t>(kPlayer + L.holder) = 0;
    Check(Scroll(1) == Id(Colt) && Scroll(-1) == Id(Grenades), "no current holder: first or last");
    Holding(Garand);

    // the scope: a sniper out and zoomed in
    G<uint32_t>(Weapon(Garand) + L.type) = 4;
    G<float>(kPlayer + L.fov) = 35.0f;
    game::WeaponScope(kPlayer, L);
    Check(scoped == 0, "a sniper from the hip: not scoped");
    G<float>(kPlayer + L.fov) = 10.0f;
    game::WeaponScope(kPlayer, L);
    Check(scoped == 1, "a sniper zoomed in: scoped");
    G<uint32_t>(Weapon(Garand) + L.type) = 3;
    game::WeaponScope(kPlayer, L);
    Check(scoped == 0, "another weapon zoomed in: not scoped");
    scoped = -1;
    game::WeaponScope(kPlayer + 0x4000, L);
    Check(scoped == -1, "another player's look update says nothing");

    key = kSecondary;
    Query(0, 0x12, 0);
    G<uint8_t>(Holder(Colt) + 0x3C0) = 0;
    Check(Select(kPlayer) == 0, "a weapon gone before the select: the game's next weapon");
    Check(Press(kSecondary) == 0, "no pistol: nothing");

    std::puts("weapon select: all checks passed");
}

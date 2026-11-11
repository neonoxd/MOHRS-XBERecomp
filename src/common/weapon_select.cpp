#include "common/weapon_select.h"

#include <algorithm>
#include <chrono>

#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

constexpr uint32_t kSwitchAction = 0x12;
constexpr uint32_t kHeldWeapon = 0x2BC, kNextHolder = 0x2B4, kHolderFlags = 0x3C0;
constexpr uint8_t kHolderHas = 4;
constexpr uint32_t kCountedType = 7;
constexpr uint32_t kPistol = 1, kLastFirearm = 5;
// bit 0: on a gun, whose weapon is the current one (0xD6560 / 0xCB2A0); 0x10 is only the way onto it (set at
// 0xD9056, swapped for bit 0 at 0xDC3F8)
constexpr uint32_t kStateFlags = 0xD46;
constexpr uint8_t kMounted = 1;
constexpr uint32_t kMaxHolders = 32;
// The script holsters before it calls NextWeapon; a switch it never finishes leaves the request unused.
constexpr uint64_t kSwitchMs = 2000;

// guest thread only: the player update and the script run there
struct Pending {
    uint32_t player = 0, id = 0;
    uint64_t until = 0;
} g_pending;

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count());
}

bool Pointer(uint32_t address) { return address >= 0x10000; }

uint32_t Weapon(uint32_t holder) { return R32(holder + kHeldWeapon); }

// 0xE27E0 / 0xDA900
bool Usable(uint32_t holder, const WeaponLayout& l) {
    uint32_t weapon = Weapon(holder);
    if (!Pointer(weapon)) return false;
    if (R32(weapon + l.type) == kCountedType) return G<int16_t>(weapon + l.count_a) + G<int16_t>(weapon + l.count_b) != 0;
    return (G<uint8_t>(holder + kHolderFlags) & kHolderHas) != 0;
}

// a weapon key's group by WeaponType: 1 the firearms but pistols (2-5), 2 pistols (1), 3 the rest
bool InGroup(uint32_t type, int key) {
    bool firearm = type >= kPistol && type <= kLastFirearm;  // 0x150090
    if (key == 1) return firearm && type != kPistol;
    if (key == 2) return type == kPistol;
    return !firearm;
}

// the key's next usable weapon after `from` in the switch's order, or its first when `from` is not in the group; 0
// when the player has none
uint32_t Pick(uint32_t player, int key, uint32_t from, const WeaponLayout& l) {
    uint32_t group[kMaxHolders];
    uint32_t count = 0, after = kMaxHolders;
    uint32_t head = R32(player + l.ring), at = head;
    for (uint32_t i = 0; Pointer(at) && i < kMaxHolders; i++) {
        if (Usable(at, l) && InGroup(R32(Weapon(at) + l.type), key)) {
            if (at == from) after = count;
            group[count++] = at;
        }
        at = R32(at + kNextHolder);
        if (at == head) break;
    }
    if (!count) return 0;
    return after < count ? group[(after + 1) % count] : group[0];
}

// the usable holder with this weapon id, or 0: what the select's walk stops at
uint32_t WithId(uint32_t player, uint32_t id, const WeaponLayout& l) {
    uint32_t head = R32(player + l.ring), at = head;
    for (uint32_t i = 0; Pointer(at) && i < kMaxHolders; i++) {
        if (Usable(at, l) && R32(Weapon(at) + l.id) == id) return at;
        at = R32(at + kNextHolder);
        if (at == head) break;
    }
    return 0;
}

// the player KBM's port 0 is assigned to, by input manager entry (0xB7E80: 0x8F8 bytes each, starting with the port)
uint32_t KeyboardPlayer(uint32_t index, const WeaponLayout& l) {
    if (index >= std::min(uint32_t(G<uint8_t>(l.input_manager + 0x23E0)), 4u)) return 0;
    if (G<uint8_t>(l.input_manager + index * 0x8F8) != 0) return 0;
    return R32(l.players + index * 4);
}

}  // namespace

void WeaponSwitchQuery(X86Context& ctx, uint32_t index, uint32_t action, const WeaponLayout& l) {
    if (action != kSwitchAction || ctx.eax) return;
    uint32_t player = KeyboardPlayer(index & 0xFF, l);
    if (!Pointer(player)) return;
    int key = input::TakeWeaponKey();
    if (!key) return;
    // On a gun the switch is the gun's. No current holder is how a level starts (0xE0020 clears it): the game's own
    // switch then takes the first usable weapon (the select walks from the ring's head), so a key takes its group's.
    if (G<uint8_t>(player + kStateFlags) & kMounted) {
        LOGI("weapon key %d: on a gun", key);
        return;
    }
    uint32_t current = R32(player + l.holder);
    if (!Pointer(current)) current = 0;
    // Pressed again before the script's select, the key cycles on from the weapon already asked for; the switch
    // under way takes the new one, and no second switch is started (the script would step past it after).
    bool switching = g_pending.player == player && NowMs() <= g_pending.until;
    if (g_pending.player && !switching) LOGI("weapon key: the last switch asked for was not taken by the script");
    uint32_t asked = switching ? WithId(player, g_pending.id, l) : 0;
    uint32_t from = asked ? asked : current;
    uint32_t wanted = Pick(player, key, from, l);
    // the select given the current weapon's id would put it away and leave the player unarmed
    if (!wanted || wanted == from || wanted == current) {
        LOGI("weapon key %d: nothing else in the group (holding %08X)", key, current);
        return;
    }
    g_pending = {player, R32(Weapon(wanted) + l.id), NowMs() + kSwitchMs};
    if (!switching) ctx.eax = 1;
    LOGI("weapon key %d: switching to holder %08X", key, wanted);
}

void WeaponSelect(X86Context& ctx, const WeaponLayout& l) {
    if (!g_pending.player || R32(ctx.esp) != l.next_weapon_return || ctx.ecx != g_pending.player) return;
    Pending pending = g_pending;
    g_pending = {};
    // only a plain next/previous; the weapon must still be there, or the select's walk would not end
    if (R32(ctx.esp + 8) != 0 || NowMs() > pending.until || !WithId(pending.player, pending.id, l)) return;
    G<uint32_t>(ctx.esp + 8) = pending.id;
    LOGI("weapon key: the script's switch goes to weapon id %08X", pending.id);
}

}  // namespace game

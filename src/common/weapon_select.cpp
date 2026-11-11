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
constexpr uint32_t kStateFlags = 0xD46;  // bit 0 a fixed gun manned, 0x10 a vehicle's (both images)
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

// the holder of the player's place-th usable weapon (1-based) in the order the switch steps through them, or 0
uint32_t Place(uint32_t player, int place, const WeaponLayout& l) {
    uint32_t head = R32(player + l.ring), at = head;
    for (uint32_t i = 0; Pointer(at) && i < kMaxHolders; i++) {
        if (Usable(at, l) && --place == 0) return at;
        at = R32(at + kNextHolder);
        if (at == head) break;
    }
    return 0;
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
    int place = input::TakeWeaponKey();
    if (!place) return;
    // on a gun the switch is the gun's; with no weapon out a level has the player unarmed
    uint32_t current = R32(player + l.holder);
    if (G<uint8_t>(player + kStateFlags) & 0x11 || !Pointer(current)) return;
    uint32_t wanted = Place(player, place, l);
    if (!wanted || wanted == current) return;
    g_pending = {player, R32(Weapon(wanted) + l.id), NowMs() + kSwitchMs};
    ctx.eax = 1;
    LOGI("weapon key %d: switching to holder %08X", place, wanted);
}

void WeaponSelect(X86Context& ctx, const WeaponLayout& l) {
    if (!g_pending.player || R32(ctx.esp) != l.next_weapon_return || ctx.ecx != g_pending.player) return;
    Pending pending = g_pending;
    g_pending = {};
    // only a plain next/previous; the weapon must still be there, or the select's walk would not end
    if (R32(ctx.esp + 8) != 0 || NowMs() > pending.until || !WithId(pending.player, pending.id, l)) return;
    G<uint32_t>(ctx.esp + 8) = pending.id;
}

}  // namespace game

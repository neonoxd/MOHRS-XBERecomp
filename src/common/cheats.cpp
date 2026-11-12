#include "common/cheats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <iterator>

#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

constexpr uint32_t kHeldWeapon = 0x2BC, kNextHolder = 0x2B4, kHolderFlags = 0x3C0;
constexpr uint8_t kHolderHas = 4;
constexpr uint32_t kMaxHolders = 32, kMaxPlayers = 4;
constexpr uint32_t kMostAmmo = 0x12;
// WeaponType 1-5 firearms, 6 launchers, 7 grenades, 8 thrown; tank turrets, mounted guns and melee are left alone
constexpr uint32_t kFirstType = 1, kLastType = 8;

std::atomic<bool> g_on[size_t(Cheat::Count)];

// game thread only
struct Byte {
    bool forced = false;
    uint8_t was = 0;  // the game's value when forcing began
};
Byte g_bytes[3];
bool g_god_applied = false;
uint32_t g_ring_built[kMaxPlayers];  // players whose ring was (re)built and not given since
bool g_give_all = false;             // Give All Weapons switched on: every player gets them once

bool Pointer(uint32_t address) { return address >= 0x10000; }

uint32_t Call(X86Context& ctx, uint32_t fn, uint32_t ecx, std::initializer_list<uint32_t> args = {}) {
    uint32_t saved = ctx.ecx;
    ctx.ecx = ecx;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = saved;
    return result;
}

void Force(Byte& b, uint32_t address, bool on) {
    uint8_t& value = G<uint8_t>(address);
    if (on) {
        if (!b.forced) b = {true, value};
        value = 1;
    } else if (b.forced) {
        value = b.was;
        b = {};
    }
}

uint32_t Player(const CheatLayout& l, uint32_t i) { return R32(l.weapons.players + i * 4); }

uint32_t PlayerCount(const CheatLayout& l) { return std::min(R32(l.player_count), kMaxPlayers); }

// every player weapon in the ring owned, its reserve full, the clip of a newly owned one loaded
int GiveAll(X86Context& ctx, uint32_t player, const CheatLayout& l) {
    int given = 0;
    uint32_t head = R32(player + l.weapons.ring), at = head;
    for (uint32_t i = 0; Pointer(at) && i < kMaxHolders; i++) {
        uint32_t weapon = R32(at + kHeldWeapon);
        if (Pointer(weapon))
            LOGI("cheats: holder %08X weapon %08X id %08X type %u flags %02X", at, weapon, R32(weapon + l.weapons.id),
                 R32(weapon + l.weapons.type), G<uint8_t>(at + kHolderFlags));
        if (Pointer(weapon)) {
            uint32_t type = R32(weapon + l.weapons.type), def = R32(weapon + l.definition);
            if (type >= kFirstType && type <= kLastType && Pointer(def)) {
                bool had = (G<uint8_t>(at + kHolderFlags) & kHolderHas) != 0;
                int16_t most = G<int16_t>(def + kMostAmmo);
                Call(ctx, l.give_ammo, weapon, {uint32_t(uint16_t(most))});
                if (!had) {
                    G<uint8_t>(at + kHolderFlags) |= kHolderHas;
                    Call(ctx, l.reload, at);
                    given++;
                }
            }
        }
        at = R32(at + kNextHolder);
        if (at == head) break;
    }
    return given;
}

}  // namespace

// The switches outlive a relaunch (the front end and a level are each a process of their own): kept in the
// environment, which the next instance inherits
constexpr char kCheatsVariable[] = "MOHRS_CHEATS";

void LoadCheats() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char* v = std::getenv(kCheatsVariable);
        unsigned bits = v ? unsigned(std::strtoul(v, nullptr, 16)) : 0;
        for (size_t c = 0; c < size_t(Cheat::Count); c++) g_on[c] = (bits >> c) & 1;
        if (bits) LOGR("cheats: %x from the previous instance", bits);
    });
}

bool CheatOn(Cheat cheat) {
    LoadCheats();
    return g_on[size_t(cheat)].load(std::memory_order_relaxed);
}

void SetCheat(Cheat cheat, bool on) {
    LoadCheats();
    bool was = g_on[size_t(cheat)].exchange(on, std::memory_order_relaxed);
    if (on == was) return;
    LOGI("cheats: %d %s", int(cheat), on ? "on" : "off");
    unsigned bits = 0;
    for (size_t c = 0; c < size_t(Cheat::Count); c++) bits |= unsigned(g_on[c].load()) << c;
    char text[16];
    std::snprintf(text, sizeof(text), "%x", bits);
    _putenv_s(kCheatsVariable, text);
}

void CheatRingBuilt(uint32_t player) {
    for (uint32_t& p : g_ring_built) {
        if (p == player) return;
    }
    for (uint32_t& p : g_ring_built) {
        if (!p) {
            p = player;
            return;
        }
    }
}

void CheatFrame(X86Context& ctx, const CheatLayout& l) {
    // MOHRS_CHEATS_AT=<seconds>:<hex mask> (tests): the switches set that long into the run, as the menu would
    static const char* at = std::getenv("MOHRS_CHEATS_AT");
    if (at) {
        static const auto start = std::chrono::steady_clock::now();
        double seconds = std::atof(at);
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= seconds) {
            const char* colon = std::strchr(at, ':');
            unsigned bits = colon ? unsigned(std::strtoul(colon + 1, nullptr, 16)) : 0;
            for (size_t c = 0; c < size_t(Cheat::Count); c++) SetCheat(Cheat(c), (bits >> c) & 1);
            LOGR("cheats: %x at %.1f s (MOHRS_CHEATS_AT)", bits, seconds);
            at = nullptr;
        }
    }
    // a multiplayer match keeps the game's own; the shell has no players
    bool play = !G<uint8_t>(l.multiplayer);
    bool in_level = play && !G<uint8_t>(l.front_end);
    bool god = play && CheatOn(Cheat::GodMode);
    Force(g_bytes[0], l.god, god);
    Force(g_bytes[1], l.ammo, play && CheatOn(Cheat::InfiniteAmmo));
    Force(g_bytes[2], l.one_shot, play && CheatOn(Cheat::OneShotKills));

    uint32_t count = in_level ? PlayerCount(l) : 0;
    // full health as the game's own cheat gives when it is switched on
    if (god && !g_god_applied && count) {
        for (uint32_t i = 0; i < count; i++)
            if (Pointer(Player(l, i))) Call(ctx, l.refill_health, Player(l, i));
        g_god_applied = true;
    }
    if (!god) g_god_applied = false;

    bool give = CheatOn(Cheat::GiveWeapons);
    if (!give) {
        g_give_all = false;
        std::fill(std::begin(g_ring_built), std::end(g_ring_built), 0u);
        return;
    }
    if (!g_give_all) {
        // switched on: every player now, then each new ring once
        g_give_all = true;
        for (uint32_t i = 0; i < count; i++) CheatRingBuilt(Player(l, i));
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t player = Player(l, i);
        uint32_t* built = std::find(std::begin(g_ring_built), std::end(g_ring_built), player);
        if (!Pointer(player) || built == std::end(g_ring_built)) continue;
        // only once the level has armed the player: a level's start, or a scene that holds no weapon, is its own
        if (!Call(ctx, l.current_weapon, player)) continue;
        int given = GiveAll(ctx, player, l);
        *built = 0;
        LOGR("cheats: player %u given %d weapons", i, given);
    }
}

}  // namespace game

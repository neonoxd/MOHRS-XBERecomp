#pragma once
// Cheats, switched in the pause screen's Preferences > Cheats (src/common/options_menu.h, docs/menus.md).
//
// All but Give All Weapons are the game's own cheats (the console's unlockable bonus cheats), each a byte the game
// reads where it applies, so each takes effect from the next frame:
//   God Mode (Invincibility): the player's invulnerable test (0xD6210 / co-op 0xCAD20)
//   Infinite Ammo (Unlimited Ammo): the weapon fire leaving the clip alone (0x1506C0 / 0x149A90)
//   One Shot Kills (Silver Bullet): the AI hit handler setting the damage to 1000 (0xE9E30 / 0xE1E30)
//   Bullet Shield: a bullet reaching the player sparks and does not hit (0xD23E0 / 0xC6E90)
//   Achilles Head: the AI hit handler doing no damage but to the head (0xE9E30 / 0xE1E30)
//   Invisible Soldiers: the characters' bodies not drawn (0xE9860 / 0xE18C0)
//   Rubber Grenades: a grenade's update bouncing it (0x14DE80, 0x14EAB0 / 0x147250, 0x147E80)
// The game sets those bytes from the profile at a level's start and clears them on a profile load; here they are set
// again every frame while switched on, and put back as they were when switched off. The profile is never written.
//
// Give All Weapons hands out every weapon in the player's weapon ring: the ring holds a holder for every player
// weapon in the level's object database (class 0x21, 0xDF320 from the player's setup), given or not. Other levels'
// weapons are put in the database and archive of the levels loaded while it is on (src/common/level_weapons.h).
#include <cstddef>
#include <cstdint>

#include "common/weapon_select.h"
#include "cpu/x86_context.h"

namespace game {

// the order is the bits of MOHRS_CHEATS
enum class Cheat {
    GiveWeapons,
    InfiniteAmmo,
    GodMode,
    OneShotKills,
    BulletShield,
    AchillesHead,
    InvisibleSoldiers,
    RubberGrenades,
    Count
};

struct CheatLayout {
    uint32_t bytes[size_t(Cheat::Count)];  // the game's cheat byte for each, 0 for Give All Weapons
    uint32_t front_end, multiplayer;       // bytes: in the shell, in a multiplayer match
    uint32_t player_count;                 // the players in [weapons.players]
    uint32_t refill_health;                // fastcall (player): health to the difficulty's full
    uint32_t current_weapon;               // fastcall (player): the weapon out, 0 for none
    uint32_t give_ammo;                    // thiscall (weapon, amount), ret 4: ammo up to the definition's most
    uint32_t reload;                       // fastcall (holder): the clip filled from the reserve
    uint32_t definition;                   // weapon: its definition (clip +4, most ammo +0x12)
    const WeaponLayout& weapons;
};

inline constexpr CheatLayout kDefaultCheats{
    {0, 0x20EDBD, 0x20EDBC, 0x20EDC7, 0x20EDC4, 0x20EDC8, 0x20EDCA, 0x20EDC6},
    0x2463B1, 0x20EDB1, 0x1FC658, 0xD7620, 0xD6560, 0x150170, 0xE2520, 0x2F0, kDefaultWeapons};
inline constexpr CheatLayout kCoopCheats{
    {0, 0x206F9D, 0x206F9C, 0x206FA7, 0x206FA4, 0x206FA8, 0x206FAA, 0x206FA6},
    0x23E5B1, 0x206F91, 0x1F54B8, 0xCC300, 0xCB2A0, 0x149540, 0xDA640, 0x300, kCoopWeapons};

// the menu's switches (any thread; the frame applies them)
bool CheatOn(Cheat cheat);
void SetCheat(Cheat cheat, bool on);

// the player's holder insert (0xDF320 / co-op 0xD3400, thiscall player): a level's setup building the ring
void CheatRingBuilt(uint32_t player);
// at the main gameplay frame's entry (0xA4A90 / co-op 0x98A40: once a frame in a level and the shell, paused too,
// where the players are live): the cheat bytes set, health refilled when god mode comes on, and the weapons given
// once per player ring while Give All Weapons is on and the player holds a weapon
void CheatFrame(X86Context& ctx, const CheatLayout& layout);

}  // namespace game

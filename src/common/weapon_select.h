#pragma once
// Weapon keys: weapon_1..3 pick the player's first, second or third weapon (docs/input.md "Weapon keys").
//
// The switch stays the game's own: on the switch action (0x12) the player update posts message 0x12 to the player's
// script (default.xbe 0xDDCB0, co-op 0xD78E3 and 0xD7AC6), which holsters the weapon and calls its NextWeapon native
// (0x33CD0 / 0x332C0). That calls the weapon select (0xD8720 / 0xCD820, thiscall (direction, id), ret 8) with id 0,
// which walks the player's weapon ring from the current holder to the next usable one. A weapon key reads as the
// switch action for KBM's player, and the select it leads to is given the asked-for weapon's id, so the walk stops
// there instead.
//
// Ring layout: the player's current holder and the ring's first holder (+0xE6C/+0xE70, co-op +0xF4C/+0xF50); a holder
// has its weapon at +0x2BC, the next holder at +0x2B4 and flags at +0x3C0 (bit 2: the player has it). A weapon has its
// id, type and two counts; the usable test (0xE27E0 / 0xDA900) takes type 7 by its counts, the rest by bit 2.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct WeaponLayout {
    uint32_t players, input_manager;
    uint32_t holder, ring;           // player
    uint32_t id, type, count_a, count_b;  // weapon
    uint32_t next_weapon_return;     // the NextWeapon native's return address from the select
};
inline constexpr WeaponLayout kDefaultWeapons{0x270108, 0x271188, 0xE6C, 0xE70, 0x2F8, 0x310, 0x2FC, 0x2FE, 0x33D09};
inline constexpr WeaponLayout kCoopWeapons{0x265278, 0x266618, 0xF4C, 0xF50, 0x308, 0x320, 0x30C, 0x30E, 0x332F9};

// after the input manager's action query (thiscall (player index, action), ret 8; 0xB83E0 / 0xACC80), with its
// arguments: a weapon key reads as a switch press for KBM's player when it asks for another usable weapon
void WeaponSwitchQuery(X86Context& ctx, uint32_t index, uint32_t action, const WeaponLayout& layout);
// before the weapon select: the switch a weapon key started goes to that weapon
void WeaponSelect(X86Context& ctx, const WeaponLayout& layout);

}  // namespace game

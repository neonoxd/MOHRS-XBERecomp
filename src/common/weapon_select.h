#pragma once
// Weapon keys (docs/input.md "Weapon keys"): primary_weapon cycles the firearms but pistols, secondary_weapon the
// pistols, special_weapon the rest (launchers first); next_weapon and prev_weapon (the wheel) step through all of
// them. Groups go by the weapon definition's WeaponType (+0x68, copied to the
// weapon at 0x1504C3): 1 pistol, 2 bolt rifle, 3 semi-automatic rifle or shotgun, 4 sniper, 5 automatic, 6 launcher,
// 7 grenade, 8 thrown; 0x150090 calls 1-5 firearms. The ring is kept sorted by it (0xDF320), so a group is in order.
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
// id, WeaponType and two counts; the usable test (0xE27E0 / 0xDA900) takes type 7 by its counts, the rest by bit 2.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct WeaponLayout {
    uint32_t players, input_manager;
    uint32_t holder, ring;           // player
    uint32_t id, type, count_a, count_b;  // weapon
    uint32_t next_weapon_return;     // the NextWeapon native's return address from the select
    uint32_t fov;                    // player: the zoom field, half the view's FoV in degrees (docs/input.md)
    uint32_t script;                 // player: its script object, which the switch message is posted to
    uint32_t running_script;         // global: the script object whose code runs
    uint32_t switch_start, draw;     // natives the player's script calls first in a switch, and drawing a weapon
};
inline constexpr WeaponLayout kDefaultWeapons{0x270108, 0x271188, 0xE6C, 0xE70, 0x2F8,  0x310,   0x2FC,  0x2FE,
                                              0x33D09,  0xE30,    0x220, 0x2557C0, 0x2E6B0, 0x30590};
inline constexpr WeaponLayout kCoopWeapons{0x265278, 0x266618, 0xF4C, 0xF50, 0x308,   0x320,   0x30C,  0x30E,
                                           0x332F9,  0xF10,    0x220, 0x24D9E8, 0x2DC40, 0x2F620};

// after the input manager's action query (thiscall (player index, action), ret 8; 0xB83E0 / 0xACC80), with its
// arguments: a weapon key reads as a switch press for KBM's player when its group has another usable weapon
void WeaponSwitchQuery(X86Context& ctx, uint32_t index, uint32_t action, const WeaponLayout& layout);
// each on-foot look update (the mouse look hook, esi the player): for KBM's player, whether a sniper (WeaponType 4)
// is out and zoomed in, so the wheel zooms rather than steps the weapon
void WeaponScope(uint32_t player, const WeaponLayout& layout);
// before the weapon select: the switch a weapon key started goes to that weapon
void WeaponSelect(X86Context& ctx, const WeaponLayout& layout);
// after a message post to a script (0x4ADB0 / 0x4DF00, cdecl (script object, message, ...); true when a handler of
// the script's current state took it), with the target and message: the first switch message after a key press that
// no handler took (the player's script is in a state without one, as right after a level restart) means the switch
// was not started, and the next press starts one
void WeaponSwitchPosted(uint32_t target, uint32_t message, bool taken, const WeaponLayout& layout);
// at the native opcode (0x497D0 / 0x4C920), the native about to run: the player's script starting the switch
// (0x2E6B0 / 0x2DC40: the first native its switch handler calls, then the holster) or drawing a weapon (0x30590 /
// 0x2F620: after the select, and the level's first draw at its start or restart). A switch message a handler took
// without starting a switch (right after a restart the script is still drawing the first weapon and ignores it)
// does not keep the next press from starting one; a draw with no select ends the switch asked for.
void WeaponNative(uint32_t native, const WeaponLayout& layout);

}  // namespace game

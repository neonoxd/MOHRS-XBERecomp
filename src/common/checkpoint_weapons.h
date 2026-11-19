#pragma once
// Checkpoint weapons (docs/menus.md "Weapons from other levels"): a checkpoint keeps each player's weapons in the game state,
// 8 slots a player, and saves every holder of the player's weapon map, owned or not. With the weapons from other
// levels (src/common/level_weapons.h) a level has more than 8, and a player's own weapons past the first 8 by id were
// lost at the next death. Single player only (default.xbe); co-op adds no weapons.
//
// The slots: game state ([state]) + 0x3167C + player index * 0x60, 12 bytes each: weapon id, two counts (shorts),
// and a byte whose bit 0 is "usable" (0xE27E0). Room for 2 players: player 2's would be +0x3173C, the players'
// health. A player's current weapon id is at +0x31674 + index * 4.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct CheckpointLayout {
    uint32_t state;         // global: the game state's address
    uint32_t players;       // global: the players, 4 bytes each
    uint32_t player_count;  // global
    uint32_t slots;         // game state: the first player's first slot
    uint32_t index;         // player: its index
    uint32_t ring;          // player: the ring's first holder
    uint32_t weapon_id;     // weapon
};
inline constexpr CheckpointLayout kDefaultCheckpoint{0x2447D8, 0x270108, 0x1FC658, 0x3167C, 0x10DC, 0xE70, 0x2F8};

// slots a player has: in single player the first player has the second player's too
uint32_t CheckpointSlots(uint32_t index, const CheckpointLayout& l);
// the checkpoint save of a player (0xDB210, fastcall player), before it: its slots emptied, so a weapon it no longer
// has is not kept from an earlier checkpoint
void CheckpointSaveStart(uint32_t player, const CheckpointLayout& l);
// replacements of the slot calls (thiscall, the game state's holder as this):
// 0x9D310 (id, count, count, index; ret 0x10): stores a weapon, but not one the player neither has nor has ammo for
// (a missing one loads as that)
void CheckpointStore(X86Context& ctx, const CheckpointLayout& l);
// 0x9D3B0 (id, count out, count out, index; ret 0x10): a weapon's counts, 0 when missing
void CheckpointCounts(X86Context& ctx, const CheckpointLayout& l);
// 0x9D430 (id, usable, index; ret 0xC): a weapon's usable bit
void CheckpointSetUsable(X86Context& ctx, const CheckpointLayout& l);
// 0x9D490 (id, index; ret 8): a weapon's usable bit, 0 when missing
void CheckpointUsable(X86Context& ctx, const CheckpointLayout& l);

}  // namespace game

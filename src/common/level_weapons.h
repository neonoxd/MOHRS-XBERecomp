#pragma once
// Weapons from other levels for Give All Weapons (src/common/cheats.h, docs/menus.md). default.xbe only.
//
// - The game's main heap is a fixed 48 MB physical block (0x10FFD0: XPhysicalAlloc 0x1A60FA, then the EA memory
//   manager's pool over XPhysicalSize). It is made larger at boot by what the physical memory has spare, so a level
//   archive with more in it fits.
// - A campaign level's level.viv is read from the disc image as one with the other campaign levels' player weapons
//   added (src/common/level_archive.h), through the SDK's disc file provider, the cheat on or not: they wait unowned
//   in the ring until Give All Weapons gives them, so switching it on in a level needs no restart. The
//   game then loads them as its own: the weapon entries build holders in the player's ring, their files are found
//   by name in the archive, the .xsh crosshairs are registered with the rest.
// - The first person animations are looked up by name hash in the level's animation bank (0x68CE0, from the table
//   0x68BD0 builds). The archive's added bank is loaded the same way after the level's (0x68BD0 run once more with
//   the bank globals pointed at it, then put back), and a lookup the level's bank misses is made in it.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

using GuestFunction = void (*)(X86Context&, uint8_t*);

// the disc file provider, at the game's init
void InstallLevelWeapons();
// XPhysicalAlloc (stdcall size, address, alignment, protection; ret 0x10): the main heap's block made larger
void HeapAlloc(X86Context& ctx, uint8_t* base, GuestFunction original);
// the animation bank load (0x68BD0): the archive's added bank after the level's
void LoadWeaponBank(X86Context& ctx, uint8_t* base, GuestFunction original);
// the animation lookup (0x68CE0, cdecl out, hash or index, bank): a hash the level's bank lacks found in the added one
void FindAnimation(X86Context& ctx, uint8_t* base, GuestFunction original);
// the animation bank release (0x68BA0) at a level's end: the added bank released too
void ReleaseWeaponBank(X86Context& ctx, uint8_t* base, GuestFunction original);

}  // namespace game

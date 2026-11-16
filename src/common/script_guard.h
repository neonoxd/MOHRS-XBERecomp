#pragma once
// Script natives given a destroyed script object (docs/debugging.md "Script natives").
//
// A script object (constructor default.xbe 0x4B480, co-op 0x4E570) holds its class at +0, its entity at +0x20 and
// its id at +0x24, its vtable at +0xC. Destroyed (0x4B2B0 sets the id to -1; the base destructor 0xF8820 / 0xF1180
// leaves the base vtable 0x1E0B90 / 0x1D9F90 at +0xC) it is freed, but a script can still hold it: in 1_8 a
// BaseAction script ran on with an ArisakaNoBayonet soldier's object after it was destroyed, and natives read its
// entity (+0x20, null) without a check (0x30C70, then 0x3D0A0). A soldier's scripts also run while it has no entity
// (+0x20 null on a live object, the running script's own object without one too): 0x30C70 again.
//
// Every native runs through the script opcode 0x497D0 / 0x4C920: the instruction at the script pc (0x2557BC /
// 0x24D9E4) has the native's index at +2, its descriptor (16 bytes, table at 0x255524 / 0x24D744) the words it pops
// at +0xA and its arguments at +0xC, the script stack top at 0x25584C / 0x24DA74. NativeOnDestroyed is true, and
// the opcode done, when an argument is a destroyed script object, or when the native is one that reads its first
// argument's entity without a check (the lists in script_guard.cpp) and that entity is null: the native does not run, its
// arguments are popped as it would pop them, a result it would leave is 0, and the pc moves on.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct ScriptLayout {
    uint32_t pc, stack, index, table;  // globals
    uint32_t destroyed_vtable;         // the base vtable a destroyed script object keeps at +0xC
    bool coop;                         // which image's list of natives
};
inline constexpr ScriptLayout kDefaultScripts{0x2557BC, 0x25584C, 0x255520, 0x255524, 0x1E0B90, false};
inline constexpr ScriptLayout kCoopScripts{0x24D9E4, 0x24DA74, 0x24D740, 0x24D744, 0x1D9F90, true};

// at the native opcode's entry (cdecl, no arguments)
bool NativeOnDestroyed(X86Context& ctx, const ScriptLayout& layout);
// there too: the function of the native about to run
uint32_t RunningNative(const ScriptLayout& layout);

// A slot lookup (default.xbe 0xD2D60, co-op 0xC7810; thiscall (slot), ret 4: the owner's +0x990 / +0x9A0 array)
// called with no owner. The native 0x3B060 / 0x3AD30 (arguments: an object, a second object, the slot, one more)
// gets the owner from its second argument's entity (vf10, then vf98), leaves it null when that argument, its entity
// or the vf10 result is null, and calls the lookup anyway; on a null result it does nothing more. In 1_2 an
// Action_Attach level script called it with an NPC and 0. True, with the lookup done returning null, at the lookup's
// entry when its owner is null.
bool SlotLookupWithoutOwner(X86Context& ctx);

}  // namespace game

#pragma once
#include "cpu/x86_ops.h"

// Quick save and load for testing, single player (docs/quick-save.md): the bound keys (F5, F9) at the entry of the
// gameplay frame save the whole machine or put it back (the SDK's app/snapshot.h), kept in memory and in quicksave/MOHRS.qsv.
namespace game {

// the gameplay frame's entry, before the frame runs; site is the hook function that calls this
void QuickSaveFrame(X86Context& ctx, const void* site);
void QuickSaveUi();  // what the last key did, for a moment over the picture

}  // namespace game

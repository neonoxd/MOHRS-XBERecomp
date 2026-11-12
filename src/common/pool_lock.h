#pragma once
// The game's small-block pools (operator new and MEM_alloc below 0x40 bytes: 0x111E70 tries them before the EA heap)
// are bitmaps with no lock: the search (default.xbe 0x111BE0 -> 0x111B70, co-op 0x10A760 -> 0x10A6F0) tests a free
// bit and clears it, the free (0x111C30, co-op 0x10A7B0) sets it again. The EA heap behind them locks (0x16E750); the
// pools rely on no thread switch landing in that window, which on the single-core Xbox it all but never did. Here
// guest threads run at once: a level loading on the loader thread while the intro movie plays on the main thread
// was given a block another thread held, and crashed in a skeleton's bone map (0x72410 reading a heap header as a
// node's key, from 0x71B40 during 0xCB5E0). Both run under one host lock.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

using PoolFunction = void (*)(X86Context&, uint8_t*);
// the pool search (size in ebx; the block or 0 in eax) or free (block in eax; true in al when it was a pool's)
void LockedPool(X86Context& ctx, uint8_t* base, PoolFunction original);

}  // namespace game

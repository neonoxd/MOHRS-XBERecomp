#pragma once
// The game sets its render states from state blocks: 0x17D010 (co-op 0x1763E0, thiscall (block)) sends each field
// that differs from its cache and is the only place the game sets alpha test. Character meshes run blocks from their
// materials with blending by source alpha, depth writes on and alpha test off, so a texel of alpha 0 draws nothing but
// still writes depth. A sleeve patch is a mesh of its own drawn before the uniform under it, its square texture clear
// around the insignia: the uniform then failed the depth test inside the square and the water behind the soldier showed
// through. The console does the same. Such a block is applied with alpha test on, passing alpha above 0: a texel that
// blending would leave unchanged is not drawn, so no colour changes, only the depth it wrote.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

using StateBlockApply = void (*)(X86Context&, uint8_t*);
// ecx: the block; al 1 on return
void ApplyStateBlock(X86Context& ctx, uint8_t* base, StateBlockApply original);

}  // namespace game

#pragma once
// Mouse look, shared by both images; each image's [[hook]] passes where its player list is.
//
// The player object (the same layout as the GameCube build, see MouseInjector's games/mohrs.c): yaw at +0x274 in
// radians, wrapped to -pi..pi, pitch at +0x278, clamped to +-1.45 and positive down; the view's field of view at
// +0xE30, 35 from the hip and less zoomed in; on a mounted gun its yaw and pitch at +0xE1C/+0xE20 and their limits at
// +0xE14/+0xE18. The input manager maps each player to its guest controller port; KBM always owns port 0.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

// esi the player, before the look update adds the stick's turn rates and wraps and clamps the angles
void MouseLook(X86Context& ctx, uint32_t players, uint32_t input_manager);
// esi the player on a mounted gun, before the gun's update adds the stick's rates and clamps to the gun's limits
void TurretLook(X86Context& ctx, uint32_t players, uint32_t input_manager);

}  // namespace game

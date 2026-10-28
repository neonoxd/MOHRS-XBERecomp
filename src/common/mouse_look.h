#pragma once
// Mouse look, shared by both images; each image's [[hook]] passes where its player list is.
//
// The player object (the same layout as the GameCube build, see MouseInjector's games/mohrs.c): yaw at +0x274 in
// radians, wrapped to -pi..pi, pitch at +0x278, clamped to +-1.45 and positive down; the view's field of view at
// +0xE30 in default.xbe / +0xF10 in co-op, 35 from the hip and less zoomed in. Mounted gun angles and limits also
// differ between the images (docs/mouse-look.md). The input manager maps players to guest ports; KBM owns port 0.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct MouseLookOffsets {
    uint32_t fov;
    uint32_t gun_yaw;
    uint32_t gun_pitch;
};
inline constexpr MouseLookOffsets kDefaultMouseLook{0xE30, 0xE1C, 0xE20};
inline constexpr MouseLookOffsets kCoopMouseLook{0xF10, 0xEFC, 0xF00};

// esi the player, before the look update adds the stick's turn rates and wraps and clamps the angles
void MouseLook(X86Context& ctx, uint32_t players, uint32_t input_manager, const MouseLookOffsets& offsets);
// esi the player on a mounted gun, before the gun's update adds the stick's rates and clamps to the gun's limits
void TurretLook(X86Context& ctx, uint32_t players, uint32_t input_manager, const MouseLookOffsets& offsets);

}  // namespace game

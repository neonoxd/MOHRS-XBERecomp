#pragma once
// 16:9: MOHRS draws 4:3 only (it never reads the video flags). With app::Widescreen() the runtime shows its 4:3 frame
// stretched to 16:9; the 3D view is widened to match, keeping the vertical field of view (Hor+), and the HUD, text and
// menus are squeezed back to their shape in the middle 4:3 of the frame.
#include <cstdint>

namespace game {

// after the camera's SetFov(half width, half height) (default.xbe 0x82BA0): its half angles in radians go in as their
// tangents at +0x40 (across) and +0x44 (down), which the projection and the view frustum's planes are built from
void WidenView(uint32_t camera);

// the renderer extension that squeezes the 2D draws; before the renderer starts
void StartWidescreen();

}  // namespace game

#pragma once
// Widescreen: MOHRS draws 4:3 only (it never reads the video flags). With app::Widescreen() the runtime shows its 4:3
// frame stretched to app::FrameAspect(), the output's own shape from 16:10 to 32:9 (GameInfo::widest_aspect); the 3D
// view is widened to match, keeping the vertical field of view (Hor+), and the HUD, text and menus are squeezed back
// to their shape in the middle 4:3 of the frame. Movies are placed for the stretched frame and not squeezed.
#include <algorithm>
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

// how much wider than 4:3 the frame is shown: 1 without widescreen
float FrameWidening();

// after the camera's SetFov(half width, half height) (default.xbe 0x82BA0): its half angles in radians go in as their
// tangents at +0x40 (across) and +0x44 (down), which the projection and the view frustum's planes are built from
void WidenView(uint32_t camera);

// the renderer extension that squeezes the 2D draws; before the renderer starts
void StartWidescreen();

// A movie's scales on a screen_w x screen_h console screen shown widen times wider than 4:3: as large as fits, its
// shape kept on the stretched frame, so an x scale widen times smaller than the y scale. Widen 1 is the game's fit.
inline void MovieFit(float screen_w, float screen_h, float movie_w, float movie_h, float widen, float& scale_x,
                     float& scale_y) {
    scale_y = std::min(screen_h / movie_h, screen_w * widen / movie_w);
    scale_x = scale_y / widen;
}

// [[hook]] in the movie player (default 0xFC170, co-op 0xF4C80) at the FLD after its fit (0xFC372 / 0xF4E82): the
// scales for the widened frame, and the movie's size for the extension, whose draw then is not squeezed
void MovieScale(X86Context& ctx);
// after the movie player returns
void MovieEnded();

}  // namespace game

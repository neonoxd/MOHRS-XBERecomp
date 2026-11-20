// tools\dev.bat clang++ -std=c++20 -Isrc -Isdk/runtime tests/hud_place_test.cpp -o re/hud_place_test.exe
// game::HudCenter (src/common/widescreen.h): where HUD Position and HUD Offset put the gameplay HUD's pieces.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/widescreen.h"

static void Check(bool ok, const std::string& message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message.c_str());
        std::exit(1);
    }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// where console x lands on the shown frame, as a fraction of its width
static float Shown(float x, float center, float w, float widen) { return (center + (x - center) / widen) / w; }

int main() {
    const float w = 640, mid = 320;
    // a compass from x 70 to 145, an ammo counter from 360 to 510, a crosshair across the middle
    const float compass_lo = 70, compass_hi = 145, ammo_lo = 360, ammo_hi = 510;
    for (float aspect : {16.0f / 10.0f, 16.0f / 9.0f, 21.0f / 9.0f, 32.0f / 9.0f}) {
        float widen = aspect / (4.0f / 3.0f);
        std::string at = std::to_string(aspect);
        // centre, no offset: the middle, as the menus
        Check(Near(game::HudCenter(compass_lo, compass_hi, w, widen, false, 0), mid), "centre 0 is the middle " + at);
        Check(Near(game::HudCenter(ammo_lo, ammo_hi, w, widen, false, 0), mid), "centre 0 right " + at);
        // the middle stays for a draw across it, in every mode
        for (bool edge : {false, true})
            for (uint32_t o : {0u, 5u, 10u})
                Check(Near(game::HudCenter(300, 340, w, widen, edge, o), mid), "crosshair stays " + at);
        // edge, no offset: the 4:3 layout against the edges, its margins squeezed
        float left = game::HudCenter(compass_lo, compass_hi, w, widen, true, 0);
        float right = game::HudCenter(ammo_lo, ammo_hi, w, widen, true, 0);
        Check(Near(Shown(compass_lo, left, w, widen), compass_lo / widen / w), "edge 0 left " + at);
        Check(Near(1 - Shown(ammo_hi, right, w, widen), (w - ammo_hi) / widen / w), "edge 0 right " + at);
        // the pieces keep their width
        Check(Near(Shown(compass_hi, left, w, widen) - Shown(compass_lo, left, w, widen),
                   (compass_hi - compass_lo) / widen / w), "same width " + at);
        float gap = (Shown(compass_lo, mid, w, widen) - Shown(compass_lo, left, w, widen));
        float step = game::kHudOffsetStep;
        for (uint32_t o = 1; o <= game::kHudOffsetMax; o++) {
            // edge moves in a step each, centre out, neither past the other
            float in = Shown(compass_lo, game::HudCenter(compass_lo, compass_hi, w, widen, true, o), w, widen) -
                       Shown(compass_lo, left, w, widen);
            Check(Near(in, std::min(o * step, gap)), "edge offset " + std::to_string(o) + " " + at);
            float out = Shown(compass_lo, mid, w, widen) -
                        Shown(compass_lo, game::HudCenter(compass_lo, compass_hi, w, widen, false, o), w, widen);
            Check(Near(out, std::min(o * step, gap)), "centre offset " + std::to_string(o) + " " + at);
            // mirrored on the right
            float rin = Shown(ammo_hi, right, w, widen) -
                        Shown(ammo_hi, game::HudCenter(ammo_lo, ammo_hi, w, widen, true, o), w, widen);
            Check(Near(rin, std::min(o * step, gap)), "right edge offset " + at);
        }
        std::printf("%.2f: centre to edge %.1f%% of the width, %.1f steps\n", aspect, gap * 100, gap / step);
    }
    // 4:3: nothing moves
    Check(game::HudCenter(compass_lo, compass_hi, w, 1.0f, true, 5) == mid, "4:3 keeps the middle");
    std::printf("HUD place checks passed\n");
}

// tools\dev.bat clang++ -std=c++20 -Isrc tests/fov_test.cpp -o re/fov_test.exe
#include <cstdio>
#include <cstdlib>

#include "common/fov.h"

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

int main() {
    Check(game::FovTangentScale(70) == 1, "default must leave projection unchanged");
    Check(game::ClampFov(0) == 45 && game::ClampFov(1000) == 140, "invalid settings must be bounded");
    Check(game::FovTangentScale(0) == game::FovTangentScale(45), "lower bound must apply in camera math too");
    Check(game::FovTangentScale(1000) == game::FovTangentScale(140), "upper bound must apply in camera math too");
    const float native = std::tan(35 * game::kHalfDegrees * 2);
    for (uint32_t degrees : {45u, 70u, 90u, 110u, 140u}) {
        float scale = game::FovTangentScale(degrees);
        float hip = native * scale;
        Check(Near(std::atan(hip) / game::kHalfDegrees, float(degrees)), "hip FoV must match requested degrees");
        float down = std::tan(26.25f * game::kHalfDegrees * 2);
        Check(Near((down * scale) / hip, down / native), "projection aspect must remain unchanged");
        for (float half_zoom : {5.0f, 18.0f, 32.0f, 35.0f}) {
            float zoom = std::tan(half_zoom * game::kHalfDegrees * 2);
            Check(Near((zoom * scale) / hip, zoom / native), "aim and scope magnification ratios must be preserved");
        }
        float wide = std::atan(hip * 4 / 3) / game::kHalfDegrees;
        Check(wide > degrees && wide < 180, "widescreen must add a finite Hor+ view");
        std::printf("FoV %u: %.2f degrees at 16:9\n", degrees, wide);
    }
    // the near plane's half extents (near * tangent) never exceed the original 4:3 view's at 0.4
    const float near = 0.4f, down = std::tan(26.25f * game::kHalfDegrees * 2);
    Check(Near(game::kPlayerTanAcross, native) && Near(game::kPlayerTanDown, down), "reference view must be the game's");
    Check(game::NearPlaneScale(native, down) == 1, "the original view keeps its near plane");
    Check(game::NearPlaneScale(native * 0.4f, down * 0.4f) == 1, "aiming never moves the near plane out");
    for (uint32_t degrees : {45u, 70u, 85u, 90u, 140u}) {
        for (float widen : {1.0f, 4.0f / 3.0f}) {
            float k = game::FovTangentScale(degrees), across = native * k * widen, scaled = game::NearPlaneScale(across, down * k);
            Check(near * scaled * across <= near * native * 1.0001f, "near plane must be no wider than the original");
            Check(near * scaled * down * k <= near * down * 1.0001f, "near plane must be no taller than the original");
            Check(scaled <= 1, "the near plane is never pushed out");
            if (degrees == 85 && widen > 1)
                std::printf("FoV 85 at 16:9: near %.3f, half width %.3f (original %.3f)\n", near * scaled,
                            near * scaled * across, near * native);
        }
    }
    std::puts("FoV checks passed");
}

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
    std::puts("FoV checks passed");
}

// tools\dev.bat clang++ -std=c++20 -Isrc -Isdk/runtime tests/movie_fit_test.cpp -o re/movie_fit_test.exe
// game::MovieFit (src/common/widescreen.h): the movie player's scales on a widened 640x480 frame
#include "common/widescreen.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

struct Shown {
    float left, top, width, height;  // console pixels before the frame is stretched
    float aspect;                    // the picture's shape on the stretched frame
};

Shown Fit(float movie_w, float movie_h, float frame_aspect) {
    float widen = frame_aspect / (4.0f / 3.0f), sx, sy;
    game::MovieFit(640, 480, movie_w, movie_h, widen, sx, sy);
    Shown s{(640 - sx * movie_w) * 0.5f, (480 - sy * movie_h) * 0.5f, sx * movie_w, sy * movie_h, 0};
    s.aspect = s.width * widen / s.height * (640.0f / 480.0f) / (4.0f / 3.0f);
    return s;
}

}  // namespace

int main() {
    // 4:3: the game's own fit, one scale
    Shown s = Fit(320, 144, 4.0f / 3.0f);
    Check(Near(s.width, 640) && Near(s.height, 288) && Near(s.top, 96), "4:3 frame keeps the game's letterbox");
    s = Fit(320, 224, 4.0f / 3.0f);
    Check(Near(s.width, 640) && Near(s.height, 448), "4:3 frame, 4:3 movie");
    for (float aspect : {1.6f, 16.0f / 9.0f, 2560.0f / 1080.0f, 3440.0f / 1440.0f, 32.0f / 9.0f}) {
        for (auto [w, h] : {std::pair{320.0f, 144.0f}, {640.0f, 288.0f}, {320.0f, 224.0f}, {640.0f, 448.0f}}) {
            s = Fit(w, h, aspect);
            Check(Near(s.aspect, w / h), "shape kept on the stretched frame");
            Check(s.left > -0.01f && s.top > -0.01f && s.width < 640.01f && s.height < 480.01f, "inside the frame");
            Check(Near(s.width, 640) || Near(s.height, 480), "as large as fits");
        }
    }
    s = Fit(640, 288, 16.0f / 9.0f);
    Check(Near(s.width, 640) && Near(s.height, 384), "16:9: a 20:9 movie fills the width");
    s = Fit(640, 288, 3440.0f / 1440.0f);
    Check(Near(s.height, 480) && s.width < 640, "21:9: a 20:9 movie fills the height");
    std::puts("movie fit: game fit at 4:3, shape kept and largest fit from 16:10 to 32:9");
}

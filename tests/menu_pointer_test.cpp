// tools\dev.bat clang++ -std=c++20 -Isrc tests/menu_pointer_test.cpp -o re/menu_pointer_test.exe
#include "common/menu_pointer.h"
#include <cstdio>
#include <cstdlib>

void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    using namespace game::menu;
    float x, y;
    Check(MapPointer(640, 360, 1280, 720, 640, 480, false, x, y) && Near(x, 320) && Near(y, 240), "pillarbox centre");
    Check(!MapPointer(100, 360, 1280, 720, 640, 480, false, x, y), "pillarbox is not clickable");
    Check(MapPointer(160, 0, 1280, 720, 640, 480, false, x, y) && Near(x, 0) && Near(y, 0), "4:3 picture origin");
    Check(MapPointer(1000, 700, 2000, 1400, 640, 480, true, x, y) && Near(x, 320) && Near(y, 240), "letterboxed wide centre");
    Check(!MapPointer(1000, 50, 2000, 1400, 640, 480, true, x, y), "letterbox is not clickable");
    Check(MapPointer(280, 360, 1280, 720, 640, 480, true, x, y) && Near(x, 80), "inverse menu compression");
    Check(MapPointer(560, 720, 2560, 1440, 640, 480, true, x, y) && Near(x, 80), "output scaling");
    Check(!MapPointer(0, 0, 0, 0, 640, 480, true, x, y), "minimised window");
    Rect r; r.Include({10, 20, 30, 40}); r.Include({5, 25, 50, 35});
    Check(r.left == 5 && r.top == 20 && r.right == 50 && r.bottom == 40, "row union");
    Check(r.Contains(6, 21) && !r.Contains(50, 21) && !r.Contains(30, 41), "hit edges");
    PointerButtons b;
    Check(!b.Advance(1, 1, true), "opening while held swallows press");
    Check(!b.Advance(1, 0, false), "held press stays blocked");
    Check(!b.Advance(0, 0, false), "release clears block");
    Check(b.Advance(1, 1, false) == 1, "one physical press");
    Check(!b.Advance(1, 0, false), "no repeat while held");
    Check(!b.Advance(1, 0, true), "new package consumes held click");
    Check(!b.Advance(0, 0, false), "release after transition");
    Check(b.Advance(0, 1, false) == 1, "raw tap between polls");
    Check(b.Advance(2, 2, false) == 2, "right click has its own edge");
    std::puts("menu pointer: coordinate mapping, row bounds and transition edges passed");
}

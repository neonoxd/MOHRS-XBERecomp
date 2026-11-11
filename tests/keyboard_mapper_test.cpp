// Host test: the real keyboard mapper (src/common/mouse_keyboard.cpp) with faked key state and focus, no OS input.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/keyboard_mapper_test.cpp
//     -luser32 -o re/keyboard_mapper_test.exe
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {
int held_key;
HWND const kWindow = reinterpret_cast<HWND>(0x1234);
SHORT FakeKeyState(int vk) { return vk == held_key ? SHORT(0x8000) : 0; }
HWND FakeForeground() { return kWindow; }
UINT FakeMapKey(UINT, UINT, HKL) { return 0; }  // binds stay the US keys, whatever the layout here
}  // namespace
#define GetAsyncKeyState FakeKeyState
#define GetForegroundWindow FakeForeground
#define MapVirtualKeyExW FakeMapKey
#include "../src/common/mouse_keyboard.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef MapVirtualKeyExW

namespace game {
Options& Opt() {
    static Options options;
    return options;
}
}  // namespace game

namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

constexpr uint16_t kStartBit = 1u << 4;

// one poll of port 0 with this key held, in play (the game took mouse counts) or not
input::Pad Poll(int key, bool play) {
    if (play) {
        float dx, dy;
        input::TakeMouse(dx, dy);
    } else {
        input::g_take_ms = 0;
    }
    held_key = key;
    input::Pad pad;
    input::Keyboard(pad);
    return pad;
}

bool Start(const input::Pad& p) { return (p.buttons & kStartBit) != 0; }
bool Back(const input::Pad& p) { return p.analog[1] == 0xFF; }
}  // namespace

int main() {
    input::g_hwnd = kWindow;
    input::LoadBindings();

    // Escape: Start in play and the pause menu, B in the front end
    Check(Start(Poll(VK_ESCAPE, true)) && !Back(Poll(VK_ESCAPE, true)), "Escape pauses with Start");
    Check(Start(Poll(VK_ESCAPE, false)), "held across the pause it stays Start");
    Check(!Start(Poll(0, false)), "released");
    Check(Start(Poll(VK_ESCAPE, false)), "in the pause menu Escape resumes with Start");
    Poll(0, false);
    input::SetFrontEnd(true);
    auto front = Poll(VK_ESCAPE, false);
    Check(Back(front) && !Start(front), "in the front end Escape is B");
    input::SetFrontEnd(false);
    Check(Back(Poll(VK_ESCAPE, false)) && !Start(Poll(VK_ESCAPE, true)), "held out of the front end it stays B");
    Poll(0, true);
    Check(Start(Poll(VK_ESCAPE, true)), "a new press in play is Start again");
    Poll(0, true);
    Check(Back(Poll(VK_BACK, false)), "Backspace still backs out of menus");
    Poll(0, false);

    // weapon keys: a press in play, once
    Check(input::TakeWeaponKey() == 0, "no key");
    auto pad = Poll('2', true);
    Check(!(pad.buttons & 0xF), "2 no longer presses the dpad");
    Check(input::TakeWeaponKey() == 2 && input::TakeWeaponKey() == 0, "2 asks for the second weapon once");
    Poll('2', true);
    Check(input::TakeWeaponKey() == 0, "holding it asks nothing more");
    Poll(0, true);
    Poll('3', true);
    Check(input::TakeWeaponKey() == 3, "3 the third");
    Poll(0, true);
    Poll('1', false);
    Check(input::TakeWeaponKey() == 0, "nothing in menus");
    Poll(0, false);
    Check((Poll('4', true).buttons & 0xF) == 1u << 2, "4 is dpad left");
    Check((Poll('5', true).buttons & 0xF) == 1u << 3, "5 is dpad right");

    std::puts("keyboard mapper: all checks passed");
}

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
    input::SetSaveMenu(true);
    Check(Back(Poll(VK_ESCAPE, true)) && !Start(Poll(VK_ESCAPE, true)), "on a save screen Escape is B, even in play");
    Poll(0, false);
    Check(Back(Poll(VK_ESCAPE, false)), "and out of play");
    input::SetSaveMenu(false);
    Check(Back(Poll(VK_ESCAPE, false)), "held off the save screen it stays B");
    Poll(0, true);
    Check(Start(Poll(VK_ESCAPE, true)), "back in play Escape pauses again");
    Poll(0, true);

    // weapon keys: a press in play, once
    Check(input::TakeWeaponRequest().group == 0, "no key");
    auto pad = Poll('2', true);
    Check(!(pad.buttons & 0xF), "2 no longer presses the dpad");
    Check(input::TakeWeaponRequest().group == 2 && input::TakeWeaponRequest().group == 0, "2 asks for the second weapon once");
    Poll('2', true);
    Check(input::TakeWeaponRequest().group == 0, "holding it asks nothing more");
    Poll(0, true);
    Poll('3', true);
    Check(input::TakeWeaponRequest().group == 3, "3 the third");
    Poll(0, true);
    Poll('1', false);
    Check(input::TakeWeaponRequest().group == 0, "nothing in menus");
    Poll(0, false);
    // the wheel: weapon steps, zoom only with a scope up
    auto notches = [](int up, int down) {
        input::g_wheel_notches[0] = up;
        input::g_wheel_notches[1] = down;
        if (up) input::g_wheel_ms[0] = input::NowMs();
        if (down) input::g_wheel_ms[1] = input::NowMs();
    };
    notches(0, 2);
    pad = Poll(0, true);
    auto request = input::TakeWeaponRequest();
    Check(request.steps == 2 && request.group == 0, "two notches down: two steps on");
    Check(!(pad.buttons & 0xF), "and no zoom");
    input::g_wheel_ms[1] = 0;
    notches(1, 0);
    Poll(0, true);
    Check(input::TakeWeaponRequest().steps == -1, "a notch up: one step back");
    input::g_wheel_ms[0] = 0;
    input::SetScoped(true);
    notches(0, 1);
    pad = Poll(0, true);
    Check(input::TakeWeaponRequest().steps == 0 && (pad.buttons & 0xF) == 1u << 1, "scoped: the notch zooms out");
    input::g_wheel_ms[1] = 0;
    input::SetScoped(false);
    notches(0, 1);
    Poll(0, false);
    Check(input::TakeWeaponRequest().steps == 0, "in a menu: no step, and none left over");
    Poll(0, true);
    Check(input::TakeWeaponRequest().steps == 0, "none left over in play");
    input::g_wheel_ms[1] = 0;
    Poll('2', true);
    notches(0, 1);
    Poll('2', true);
    request = input::TakeWeaponRequest();
    Check(request.group == 0 && request.steps == 1, "a step after a group key: the step wins");
    input::g_wheel_ms[1] = 0;
    Poll(0, true);

    Check((Poll('4', true).buttons & 0xF) == 1u << 2, "4 is dpad left");
    Check((Poll('5', true).buttons & 0xF) == 1u << 3, "5 is dpad right");

    std::puts("keyboard mapper: all checks passed");
}

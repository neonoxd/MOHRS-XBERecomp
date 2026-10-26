// the keyboard and mouse as the controller on port 0
#include "common/mouse_keyboard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <mutex>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/options.h"

namespace input {

namespace {

HWND g_hwnd = nullptr;
std::atomic<int> g_dx{0};
std::atomic<int> g_dy{0};
std::atomic<uint64_t> g_take_ms{0};  // TakeMouse, asked every tick in play
std::atomic<bool> g_settle{false};
std::atomic<bool> g_keyboard{true};
constexpr uint64_t kPlayMs = 250;
constexpr uint64_t kTapMs = 80;
// The wheel binds as two keys, codes Windows leaves unassigned. A notch holds what is bound to it for one tap.
constexpr int kWheelUp = 0x0E;
constexpr int kWheelDown = 0x0F;
std::atomic<uint64_t> g_wheel_ms[2] = {};  // the last notch up, down

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

// what an action presses: 0-7 analog A B X Y black white LT RT, 8-15 the buttons bits (dpad up down left right,
// start, back, left thumb, right thumb), 16-19 the move keys, 20-23 the look keys
struct Target {
    Action action;
    int button;
};
constexpr int kWalk = -3;
constexpr int kFirstButtonBit = 8;
constexpr int kStart = 12;
constexpr int kFirstMoveKey = 16;
constexpr int kFirstLookKey = 20;
constexpr int kStickMax = 32767;
// named after the game's default controller layout, MOH Sharpshooter; another layout picked in the game's options
// changes what the buttons do, not which buttons the keys press
constexpr Target kActions[] = {
    {{"move_forward", "W"}, 16},
    {{"move_back", "S"}, 17},
    {{"strafe_left", "A"}, 18},
    {{"strafe_right", "D"}, 19},
    {{"walk", "Alt"}, kWalk},
    {{"fire", "LMB"}, 7},
    {{"aim_mode", "RMB"}, 6},
    {{"action", "E,F"}, 0},
    {{"change_weapon", "Q,Mouse4"}, 1},
    {{"grenade_throw", "G"}, 2},
    {{"jump", "Space"}, 3},
    {{"reload", "R"}, 4},
    {{"crouch", "C,Ctrl"}, 5},
    {{"center_view", "Z"}, 14},
    {{"melee_attack", "V,MMB"}, 15},
    {{"sniper_zoom_up", "Wheel Up,1"}, 8},  // the dpad
    {{"sniper_zoom_down", "Wheel Down,2"}, 9},
    {{"dpad_left", "3"}, 10},
    {{"dpad_right", "4"}, 11},
    {{"back", "Tab"}, 13},
    {{"pause", "Escape"}, kStart},
    {{"look_up", "Up"}, 20},
    {{"look_down", "Down"}, 21},
    {{"look_left", "Left"}, 22},
    {{"look_right", "Right"}, 23},
};
constexpr size_t kCount = std::size(kActions);
struct Name {
    int vk;
    const char* name;
};
constexpr Name kNames[] = {
    {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"}, {VK_MBUTTON, "MMB"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {kWheelUp, "Wheel Up"}, {kWheelDown, "Wheel Down"},
    {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"},
    {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"}, {VK_CAPITAL, "CapsLock"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
    {'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "I"},
    {'J', "J"}, {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
    {'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Y"}, {'Z', "Z"},
    {'0', "0"}, {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'5', "5"}, {'6', "6"}, {'7', "7"}, {'8', "8"},
    {'9', "9"},
    {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"},
    {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"}, {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"},
    {VK_NUMPAD4, "Numpad4"}, {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
    {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"}, {VK_ADD, "NumpadPlus"}, {VK_SUBTRACT, "NumpadMinus"},
    {VK_MULTIPLY, "NumpadStar"}, {VK_DIVIDE, "NumpadSlash"}, {VK_DECIMAL, "NumpadPeriod"},
    {VK_OEM_3, "Backtick"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"},
    {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"},
};

// not bindable: in the game's menus the arrows are the dpad, Enter accepts, Backspace backs out
constexpr int kFixed[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK};

// US layout scan codes of the keys that move with the layout: a bind names the US key at that position
struct Position {
    int vk;
    UINT scan;
};
constexpr Position kPositions[] = {
    {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4', 0x05}, {'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8', 0x09},
    {'9', 0x0A}, {'0', 0x0B}, {VK_OEM_MINUS, 0x0C}, {VK_OEM_PLUS, 0x0D},
    {'Q', 0x10}, {'W', 0x11}, {'E', 0x12}, {'R', 0x13}, {'T', 0x14}, {'Y', 0x15}, {'U', 0x16}, {'I', 0x17},
    {'O', 0x18}, {'P', 0x19}, {VK_OEM_4, 0x1A}, {VK_OEM_6, 0x1B},
    {'A', 0x1E}, {'S', 0x1F}, {'D', 0x20}, {'F', 0x21}, {'G', 0x22}, {'H', 0x23}, {'J', 0x24}, {'K', 0x25},
    {'L', 0x26}, {VK_OEM_1, 0x27}, {VK_OEM_7, 0x28}, {VK_OEM_3, 0x29}, {VK_OEM_5, 0x2B},
    {'Z', 0x2C}, {'X', 0x2D}, {'C', 0x2E}, {'V', 0x2F}, {'B', 0x30}, {'N', 0x31}, {'M', 0x32},
    {VK_OEM_COMMA, 0x33}, {VK_OEM_PERIOD, 0x34}, {VK_OEM_2, 0x35},
};

using Binds = std::array<std::vector<int>, kCount>;
std::mutex g_bind_lock;
Binds g_binds;

int Vk(const std::string& name) {
    for (const Name& n : kNames)
        if (_stricmp(n.name, name.c_str()) == 0) return n.vk;
    return 0;
}

UINT ScanOf(int vk) {
    for (const Position& p : kPositions)
        if (p.vk == vk) return p.scan;
    return 0;
}

// the keyboard layout the player types with, switched per window thread
HKL Layout() { return GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, nullptr)); }

// the key the current layout has at the bind's position
int LiveVk(int vk) {
    UINT scan = ScanOf(vk);
    if (!scan) return vk;
    UINT live = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, Layout());
    return live ? int(live) : vk;
}

std::vector<int> Parse(const std::string& list) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        std::string name = list.substr(start, comma - start);
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        if (int vk = Vk(name)) out.push_back(vk);
        start = comma + 1;
    }
    return out;
}

bool Active() { return g_hwnd && GetForegroundWindow() == g_hwnd; }
bool Wheel(int vk) { return vk == kWheelUp || vk == kWheelDown; }
bool Down(int vk) {
    if (Wheel(vk)) return NowMs() - g_wheel_ms[vk == kWheelDown] < kTapMs;
    return GetAsyncKeyState(LiveVk(vk)) < 0;
}

void Clip() {
    RECT r;
    RECT cur;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    if (!GetClipCursor(&cur) || !EqualRect(&r, &cur)) ClipCursor(&r);
}

// Windows asks for the cursor only when the mouse moves
void RefreshCursor() {
    if (g_hwnd) PostMessageW(g_hwnd, WM_SETCURSOR, WPARAM(g_hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

void SetKeyboard(bool on) {
    if (g_keyboard.exchange(on) != on) RefreshCursor();
}

// keys held from before the game had the input wait for release; true once none is held
bool UpdateSettle(const Binds& binds) {
    if (!g_settle) return true;
    for (auto& b : binds)
        for (int vk : b)
            if (Down(vk)) return false;
    for (int vk : kFixed)
        if (Down(vk)) return false;
    g_settle = false;
    return true;
}

// the bound keys held this tick, beside the pad buttons they pressed
struct Held {
    bool used = false;
    bool walk = false;
    int stick[8] = {};  // move forward back left right, look up down left right
};

Held PressActions(const Binds& binds, bool play, Pad& p) {
    Held held;
    // in a menu the wheel and the mouse buttons do nothing, a bound key presses its button
    auto pressed = [play](int vk) {
        bool mouse = Wheel(vk) || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON;
        return Down(vk) && (play || !mouse);
    };
    for (size_t i = 0; i < kCount; i++) {
        int b = kActions[i].button;
        if (!std::any_of(binds[i].begin(), binds[i].end(), pressed)) continue;
        held.used = true;
        if (b == kWalk) {
            held.walk = true;
        } else if (b >= kFirstLookKey && !play) {
            continue;
        } else if (b >= kFirstMoveKey) {
            held.stick[b - kFirstMoveKey] = 1;
        } else if (b < kFirstButtonBit) {
            p.analog[b] = 0xFF;
        } else {
            // Start pauses and resumes; keep the pause binding identical in play and menus.
            p.buttons |= uint16_t(1u << (b - kFirstButtonBit));
        }
    }
    return held;
}

}  // namespace

void StartMouse(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE mouse{1, 2, 0, g_hwnd};  // generic desktop page, mouse usage
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

void OnRawInput(void* handle) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    HRAWINPUT input = static_cast<HRAWINPUT>(handle);
    if (GetRawInputData(input, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (ri.header.dwType != RIM_TYPEMOUSE || !Active()) return;
    const RAWMOUSE& m = ri.data.mouse;
    if (m.usButtonFlags & RI_MOUSE_WHEEL) g_wheel_ms[SHORT(m.usButtonData) < 0] = NowMs();
    // the cursor is free outside play
    if (!Playing()) {
        ClipCursor(nullptr);
        return;
    }
    SetKeyboard(true);
    Clip();
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        g_dx += m.lLastX;
        g_dy += m.lLastY;
    }
}

void ReleaseCursor() {
    ClipCursor(nullptr);
    g_settle = true;
}

bool Playing() { return NowMs() - g_take_ms < kPlayMs; }

bool TakeMouse(float& dx, float& dy) {
    uint64_t now = NowMs();
    bool stale = now - g_take_ms.exchange(now) >= kPlayMs;
    if (stale) RefreshCursor();
    int x = g_dx.exchange(0);
    int y = g_dy.exchange(0);
    if ((!x && !y) || stale || !Active()) return false;
    const game::Options& o = game::Opt();
    dx = float(o.invert_mouse_x ? -x : x) * o.mouse_sensitivity;
    dy = float(o.invert_mouse ? -y : y) * o.mouse_vertical_sensitivity;
    return true;
}

void Keyboard(Pad& p) {
    if (!Active()) {
        g_settle = true;
        return;
    }
    Binds binds;
    {
        std::lock_guard lock(g_bind_lock);
        binds = g_binds;
    }
    if (!UpdateSettle(binds)) return;
    bool play = Playing();
    bool fixed_down = std::any_of(std::begin(kFixed), std::end(kFixed), Down);
    Held held = PressActions(binds, play, p);
    // Enter, Space and Backspace act in menus; in play they do what they are bound to
    if (!play && (Down(VK_RETURN) || Down(VK_SPACE))) p.analog[0] = 0xFF;
    if (!play && Down(VK_BACK)) p.analog[1] = 0xFF;
    // the arrows, kFixed's first four, are the dpad's bits
    if (!play) {
        for (int i = 0; i < 4; i++) p.buttons |= uint16_t(Down(kFixed[i]) << i);
    }
    const int* stick = held.stick;
    float speed = held.walk ? float(std::clamp(game::Opt().walk_speed, 10u, 100u)) / 100.0f : 1.0f;
    if (int x = stick[3] - stick[2]) p.lx = int16_t(float(x) * speed * float(kStickMax));
    if (int y = stick[0] - stick[1]) p.ly = int16_t(float(y) * speed * float(kStickMax));
    if (int x = stick[7] - stick[6]) p.rx = int16_t(x * kStickMax);
    if (int y = stick[4] - stick[5]) p.ry = int16_t(y * kStickMax);
    if (fixed_down || held.used) SetKeyboard(true);
}

void SeenPad(const Pad& p) {
    constexpr int16_t kStickPushed = 16000;
    constexpr uint8_t kAnalogPushed = 0x20;
    auto pushed = [](int16_t v) { return v > kStickPushed || v < -kStickPushed; };
    auto pressed = [](uint8_t a) { return a > kAnalogPushed; };
    bool analog = std::any_of(std::begin(p.analog), std::end(p.analog), pressed);
    bool sticks = pushed(p.lx) || pushed(p.ly) || pushed(p.rx) || pushed(p.ry);
    if (p.buttons || analog || sticks) SetKeyboard(false);
}

bool KeyboardActive() { return g_keyboard; }

const Action* Actions(size_t* count) {
    static const auto list = [] {
        std::array<Action, kCount> a{};
        for (size_t i = 0; i < kCount; i++) a[i] = kActions[i].action;
        return a;
    }();
    *count = kCount;
    return list.data();
}

void LoadBindings() {
    std::lock_guard lock(g_bind_lock);
    const auto& keys = game::Opt().keys;
    for (size_t i = 0; i < kCount; i++) {
        auto it = keys.find(kActions[i].action.id);
        g_binds[i] = Parse(it != keys.end() ? it->second : kActions[i].action.defaults);
    }
}

}  // namespace input

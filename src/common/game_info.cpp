#include <algorithm>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "common/widescreen.h"

namespace game {

namespace {

// the system arrow, none in play while the keyboard and mouse are in use
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    switch (msg) {
        case WM_SYSCOMMAND:
            // a bound Alt must not open the window menu
            if ((wp & 0xFFF0) == SC_KEYMENU) {
                *result = 0;
                return true;
            }
            break;
        case WM_INPUT:
            input::OnRawInput(reinterpret_cast<void*>(lp));
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) input::ReleaseCursor();
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                static const HCURSOR arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
                SetCursor(input::Playing() && input::KeyboardActive() ? nullptr : arrow);
                *result = TRUE;
                return true;
            }
            break;
    }
    return false;
}

// the frames presented a second, over the top left corner of the picture
void Ui(double fps) {
    if (!Opt().fps_counter) return;
    // the picture is shown in the middle of the window, as BlitFrame places it
    ImVec2 window = ImGui::GetIO().DisplaySize;
    float aspect = app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = std::min(window.x, window.y * aspect);
    float h = w / aspect;
    float size = std::max(13.0f, h / 36.0f);
    ImVec2 at((window.x - w) / 2 + size * 0.5f, (window.y - h) / 2 + size * 0.4f);
    char text[16];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddText(nullptr, size, ImVec2(at.x + 2, at.y + 2), IM_COL32(0, 0, 0, 200), text);
    draw->AddText(nullptr, size, at, IM_COL32(255, 255, 255, 230), text);
}

// ImGui would set its arrow on every WM_SETCURSOR before Message sees it, and the window class has no cursor
void Window(void* hwnd) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    input::StartMouse(hwnd);
}

// port 0 has the keyboard and mouse beside any controller there
void Pad(int port, input::Pad& pad) {
    input::SeenPad(pad);
    if (port != 0) return;
    pad.connected = true;
    input::Keyboard(pad);
}

// no vibration while the keyboard and mouse are in use
void Rumble(int port, uint16_t& left, uint16_t& right) {
    if (port == 0 && input::KeyboardActive()) left = right = 0;
}

float ReadFloat(const toml::table& t, const char* key, float fallback) {
    return float(t[key].value_or(double(fallback)));
}

bool g_complete = false;  // the settings file has every game setting, each action's keys too

// what the options list cannot hold
void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    Options& o = Opt();
    g_complete = t.contains("mouse_sensitivity");
    for (const app::Option& option : app::kGame.options) g_complete = g_complete && t.contains(option.key);
    size_t count;
    const input::Action* actions = input::Actions(&count);
    for (size_t i = 0; i < count; i++) g_complete = g_complete && t["keys"][actions[i].id];
    o.mouse_sensitivity = ReadFloat(t, "mouse_sensitivity", o.mouse_sensitivity);
    o.mouse_vertical_sensitivity = ReadFloat(t, "mouse_vertical_sensitivity", o.mouse_vertical_sensitivity);
    if (const toml::table* keys = t["keys"].as_table())
        for (auto&& [id, value] : *keys)
            if (auto v = value.value<std::string>()) o.keys[std::string(id.str())] = *v;
}

// each action's keys, the defaults written out so the file shows what can be bound
void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    const Options& o = Opt();
    t.insert("mouse_sensitivity", double(o.mouse_sensitivity));
    t.insert("mouse_vertical_sensitivity", double(o.mouse_vertical_sensitivity));
    size_t count;
    const input::Action* actions = input::Actions(&count);
    toml::table keys;
    for (size_t i = 0; i < count; i++) {
        auto it = o.keys.find(actions[i].id);
        keys.insert(actions[i].id, it != o.keys.end() ? it->second : std::string(actions[i].defaults));
    }
    t.insert("keys", keys);
}

// after the settings load, before boot; a settings file missing a game setting or an action's keys is written out
// with them (keys of actions no longer bound drop out)
void Init() {
    StartWidescreen();
    input::LoadBindings();
    if (!g_complete) app::Save();
}

}  // namespace

Options& Opt() {
    static Options o;
    return o;
}

}  // namespace game

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Medal of Honor: Rising Sun",  // window and splash
    .short_title = L"MOHRS",                 // message boxes
    .name = L"MOHRS",                        // window classes, crash dumps, handover event
    .log = "mohrs",                          // log file prefix
    .settings = "settings.toml",             // settings file beside the exe
    .splash = L"splash",                     // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MOHRS"}, {"moh3rdc.xbe", "MOHRSCoop"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr},          // game settings kept in the settings file
                {"skip_cutscenes", &game::Opt().skip_cutscenes, nullptr},
                {"fps_counter", &game::Opt().fps_counter, nullptr},
                {"widescreen", &game::Opt().widescreen, nullptr},
                {"invert_mouse", &game::Opt().invert_mouse, nullptr},
                {"invert_mouse_x", &game::Opt().invert_mouse_x, nullptr},
                {"walk_speed", nullptr, &game::Opt().walk_speed}},
    .load = game::Load,                   // reads more from the settings file's toml::table
    .save = game::Save,                   // writes more into it
    .init = game::Init,                   // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = game::Window,               // the game window is up
    .message = game::Message,             // window messages first; true takes one
    .ui = game::Ui,                       // ImGui drawing each frame
    .pad = game::Pad,                     // changes a polled pad
    .rumble = game::Rumble,               // changes a rumble before it is set
    .widescreen = true,                   // MOHRS draws 4:3 only; src/common/widescreen.cpp widens its view for 16:9
    .widescreen_option = &game::Opt().widescreen,  // off: 4:3 on any output
};

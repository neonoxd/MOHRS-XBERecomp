#include "common/options_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/feng.h"
#include "common/fov.h"
#include "common/options.h"
#include "common/options_package.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

using kernel::R32;
using kernel::W32;

namespace game {

namespace {

// a package's current button (FEPackage + 0x80), whose name hash is at + 0x10
constexpr uint32_t kFocus = 0x80;
constexpr uint32_t kObjectHash = 0x10;
// a screen handler's package (+ 0x0C) and the copy of its name (+ 0x10)
constexpr uint32_t kHandlerPackage = 0x0C;
constexpr uint32_t kHandlerName = 0x10;

// row changes

std::string OnOff(bool on) { return on ? "On" : "Off"; }

// the step from value through the sorted list, which stops at its ends; a value off the list goes to its neighbour
template <typename T, size_t N>
T StepList(const T (&list)[N], T value, int dir) {
    size_t i = 0;
    while (i < N && list[i] < value) i++;
    bool on = i < N && list[i] == value;
    if (dir > 0) return on ? list[std::min(i + 1, N - 1)] : list[std::min(i, N - 1)];
    return i ? list[i - 1] : list[0];
}

const char* const kModes[] = {"Fullscreen", "Borderless", "Windowed"};
const uint32_t kScales[] = {50, 67, 75, 85, 100, 125, 150, 200};
// kFpsCapRefresh (1) the monitor's refresh rate, 0 none: at the list's ends
const uint32_t kCaps[] = {30, 60, 75, 90, 100, 120, 144, 165, 240, 360, 480};

int DisplayMode() { return std::min(int(app::Get().display_mode), int(std::size(kModes)) - 1); }

// 0 at 30 fps, 1 at 60, 2 unlocked
int FrameRate() { return Opt().unlocked_fps ? 2 : Opt().fps60 ? 1 : 0; }

std::string FpsCap() {
    uint32_t cap = app::Get().fps_cap;
    if (cap == app::kFpsCapRefresh) return "Monitor";
    if (!cap) return "Off";
    return std::to_string(cap);
}

void StepFpsCap(int dir) {
    uint32_t& cap = app::Get().fps_cap;
    // Monitor, 30 ... 480, Off
    if (cap == app::kFpsCapRefresh) {
        if (dir > 0) cap = kCaps[0];
    } else if (!cap) {
        if (dir < 0) cap = kCaps[std::size(kCaps) - 1];
    } else if (dir < 0 && cap <= kCaps[0]) {
        cap = app::kFpsCapRefresh;
    } else if (dir > 0 && cap >= kCaps[std::size(kCaps) - 1]) {
        cap = 0;
    } else {
        cap = StepList(kCaps, cap, dir);
    }
}

std::string Sensitivity() {
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f", Opt().mouse_sensitivity);
    return text;
}

void StepSensitivity(int dir) {
    float& s = Opt().mouse_sensitivity;
    // tenths, as shown
    int tenths = std::clamp(int(s * 10.0f + 0.5f) + dir, 1, 50);
    s = float(tenths) / 10.0f;
}

struct Row {
    const char* label;
    std::string (*show)();
    void (*step)(int dir);  // -1 left, 1 right
};

const Row kRows[] = {
    {"Display Mode", [] { return std::string(kModes[DisplayMode()]); },
     [](int dir) {
         app::Get().display_mode = app::DisplayMode(std::clamp(DisplayMode() + dir, 0, int(std::size(kModes)) - 1));
     }},
    {"Render Scale", [] { return std::to_string(app::Get().render_percent) + "%"; },
     [](int dir) { app::Get().render_percent = StepList(kScales, app::Get().render_percent, dir); }},
    {"Frame Rate", [] { return std::string(FrameRate() == 2 ? "Unlocked" : FrameRate() ? "60 FPS" : "30 FPS"); },
     [](int dir) {
         int rate = std::clamp(FrameRate() + dir, 0, 2);
         Opt().unlocked_fps = rate == 2;
         Opt().fps60 = rate >= 1;
     }},
    {"FPS Limit", FpsCap, StepFpsCap},
    {"VSync", [] { return OnOff(app::Get().vsync); }, [](int) { app::Get().vsync = !app::Get().vsync; }},
    {"Field of View", [] { return std::to_string(ClampFov(Opt().fov)); },
     [](int dir) { Opt().fov = ClampFov(uint32_t(int(ClampFov(Opt().fov)) + 5 * dir)); }},
    {"Widescreen", [] { return OnOff(Opt().widescreen); }, [](int) { Opt().widescreen = !Opt().widescreen; }},
    {"FPS Counter", [] { return OnOff(Opt().fps_counter); }, [](int) { Opt().fps_counter = !Opt().fps_counter; }},
    {"Mouse Sensitivity", Sensitivity, StepSensitivity},
    {"Invert Mouse", [] { return OnOff(Opt().invert_mouse); }, [](int) { Opt().invert_mouse = !Opt().invert_mouse; }},
};
static_assert(std::size(kRows) == menu::kRows);

// the settings on entering the screen, put back by a back: other threads read them while the game runs, so only the
// rows' fields are put back
struct Saved {
    app::DisplayMode display_mode;
    uint32_t render_percent, fps_cap;
    bool vsync;
    Options game;
};
Saved g_saved;  // game thread

// guest memory

// the packages made, each once: the archives keep theirs in memory, as these stay
struct Made {
    const char* name;
    bool pause;  // from a pause screen archive
    bool tabs = false;
    uint32_t va = 0;
};
Made g_made[] = {{menu::kScreen, false}, {menu::kScreen, true}, {"Option_overlay.fng", false},
                 {"Preferences.fng", true, true}, {"MPPreferences.fng", true, true},
                 {"ObjectivesOverlay.fng", true, true}, {"Notes.fng", true, true}, {"LoadRestart.fng", true, true},
                 {"MPLeaderBoard.fng", true, true}, {"MPLoadRestart.fng", true, true}};

uint32_t Copy(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0;
    uint32_t va = mem::AllocSystem(uint32_t(data.size()));
    if (va) std::memcpy(kernel::HostPtr(va), data.data(), data.size());
    return va;
}

// strings for one call into the game; the same few each time, so kept
uint32_t Text(const std::string& s) {
    static std::vector<std::pair<std::string, uint32_t>> kept;
    for (const auto& [text, va] : kept)
        if (text == s) return va;
    uint32_t va = mem::AllocSystem(uint32_t(s.size() + 1), 4);
    std::memcpy(kernel::HostPtr(va), s.c_str(), s.size() + 1);
    kept.emplace_back(s, va);
    return va;
}

// a scratch string for values, which change
uint32_t Scratch(const std::string& s) {
    static uint32_t va = mem::AllocSystem(64, 4);
    std::snprintf(static_cast<char*>(kernel::HostPtr(va)), 64, "%s", s.c_str());
    return va;
}

bool EqualNoCase(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return true;
}

std::string FileName(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool InPauseFolder(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
    return path.find("pausescreen") != std::string::npos;
}

// a package's size from its FEng header
size_t PackageSize(uint32_t va) {
    if (!va || R32(va) != 0xE76E4546) return 0;
    return 8 + size_t(R32(va + 4));
}

// the screen

void Show(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    for (size_t r = 0; r < std::size(kRows); r++) {
        uint32_t value = Text(menu::RowObject(r, "value"));
        kernel::CallGuest(ctx, calls.set_text, {package, value, Scratch(kRows[r].show())});
    }
}

void Setup(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    const app::Settings& s = app::Get();
    g_saved = {s.display_mode, s.render_percent, s.fps_cap, s.vsync, Opt()};
    for (size_t r = 0; r < std::size(kRows); r++) {
        kernel::CallGuest(ctx, calls.set_text, {package, Text(menu::RowObject(r, "text")), Text(kRows[r].label)});
        kernel::CallGuest(ctx, calls.run_script, {package, Text(menu::RowObject(r, "value")), Text("Show")});
    }
    Show(ctx, package, calls);
}

void Step(X86Context& ctx, uint32_t package, int dir, const MenuCalls& calls) {
    uint32_t focus = R32(package + kFocus);
    if (!focus) return;
    uint32_t hash = R32(focus + kObjectHash);
    for (size_t r = 0; r < std::size(kRows); r++) {
        if (feng::Hash(menu::RowObject(r, "button")) != hash) continue;
        std::string was = kRows[r].show();
        kRows[r].step(dir);
        if (kRows[r].show() != was) kernel::CallGuest(ctx, calls.sound, {dir > 0 ? 6u : 7u});
        Show(ctx, package, calls);
        return;
    }
}

// the display mode only changes here, a window switch at every step would be too much
void Confirm() {
    if (app::Get().display_mode != g_saved.display_mode) app::ApplyDisplay();
    app::Save();
    LOGI("PC options saved");
}

void Revert() {
    app::Settings& s = app::Get();
    s.display_mode = g_saved.display_mode;
    s.render_percent = g_saved.render_percent;
    s.fps_cap = g_saved.fps_cap;
    s.vsync = g_saved.vsync;
    Options& o = Opt();
    const Options& was = g_saved.game;
    o.fps60 = was.fps60;
    o.unlocked_fps = was.unlocked_fps;
    o.fov = was.fov;
    o.widescreen = was.widescreen;
    o.fps_counter = was.fps_counter;
    o.mouse_sensitivity = was.mouse_sensitivity;
    o.invert_mouse = was.invert_mouse;
}

}  // namespace

void MenuFile(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t esp = ctx.esp;
    uint32_t path = R32(esp + 8);
    std::string file = FileName(kernel::ReadCString(path, 260));
    Made* made = nullptr;
    bool pause = InPauseFolder(kernel::ReadCString(path, 260));
    for (Made& m : g_made)
        if (EqualNoCase(file, m.name) && (m.pause == pause || std::strcmp(m.name, menu::kScreen) != 0)) made = &m;
    if (!made) {
        original(ctx, base);
        return;
    }
    // the archive's own file: the screen's pattern or the list
    bool screen = std::strcmp(made->name, menu::kScreen) == 0;
    if (screen) W32(esp + 8, Text("HUD.fng"));
    original(ctx, base);
    W32(esp + 8, path);
    if (!made->va && ctx.eax) {
        size_t size = PackageSize(ctx.eax);
        const uint8_t* data = static_cast<const uint8_t*>(kernel::HostPtr(ctx.eax));
        std::vector<std::string> labels;
        for (const Row& row : kRows) labels.push_back(row.label);
        bool entry = EqualNoCase(file, "Option_overlay.fng") || EqualNoCase(file, "Preferences.fng") ||
                     EqualNoCase(file, "MPPreferences.fng");
        std::vector<uint8_t> modified = screen ? menu::MakeScreen(data, size, labels) :
                                        entry ? menu::AddEntry(data, size) : std::vector<uint8_t>(data, data + size);
        if (made->tabs && !modified.empty()) modified = menu::AddPauseTabs(modified.data(), modified.size());
        made->va = Copy(modified);
        if (made->va) {
            LOGR("Menus: made %s for the %s", file.c_str(), made->pause ? "pause screen" : "shell");
        } else {
            LOGE("Menus: could not make %s", file.c_str());
        }
    }
    // the stock list when its entry could not be added; no screen without a pattern
    if (made->va) ctx.eax = made->va;
    else if (screen) ctx.eax = 0;
}

void MenuScreen(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t esp = ctx.esp;
    uint32_t name = R32(esp + 4);
    bool ours = EqualNoCase(kernel::ReadCString(name, 64), menu::kScreen);
    if (ours) W32(esp + 4, Text("HUD.fng"));
    original(ctx, base);
    if (ours) W32(esp + 4, name);
}

void MenuEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls) {
    uint32_t handler = ctx.ecx;
    if (!EqualNoCase(kernel::ReadCString(handler + kHandlerName, 64), menu::kScreen)) {
        original(ctx, base);
        return;
    }
    // the HUD screen's events, which its package sends on
    static const uint32_t kSetup = feng::Hash("HUD Setup"), kConfirm = feng::Hash("HUD Config Confirm"),
                          kRevert = feng::Hash("HUD Config Revert"), kLeft = feng::Hash("Pad_Left"),
                          kRight = feng::Hash("Pad_Right");
    uint32_t package = R32(handler + kHandlerPackage);
    uint32_t event = R32(ctx.esp + 4);
    if (event == kSetup) {
        Setup(ctx, package, calls);
        // the HUD screen's own setup fills in the profile's name; the rows it would set are gone
        original(ctx, base);
        return;
    }
    if (event == kLeft || event == kRight) Step(ctx, package, event == kRight ? 1 : -1, calls);
    if (event == kConfirm) Confirm();
    if (event == kRevert) {
        Revert();
        // the screen stays up as the list's preview once left
        Show(ctx, package, calls);
    }
    kernel::Return(ctx, 0, 3);
}

}  // namespace game

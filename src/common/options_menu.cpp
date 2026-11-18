#include "common/options_menu.h"

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "common/cheats.h"
#include "common/custom_movement.h"
#include "common/feng.h"
#include "common/fov.h"
#include "common/guest_block.h"
#include "common/mission_timer.h"
#include "common/mouse_keyboard.h"
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

std::string Tenths(float value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
}

// tenths, as shown, 0.1 to 5.0
void StepTenths(float& value, int dir) {
    int tenths = std::clamp(int(value * 10.0f + 0.5f) + dir, 1, 50);
    value = float(tenths) / 10.0f;
}

struct Row {
    const char* label;
    std::string (*show)();
    void (*step)(int dir);  // -1 left, 1 right
};

// PC Options' rows under the category: a value row, or a key row with the action it binds
// (src/common/mouse_keyboard.h)
struct Item {
    const char* label;
    std::string (*show)() = nullptr;
    void (*step)(int dir) = nullptr;
    const char* key = nullptr;
};
constexpr Item Key(const char* label, const char* id) { return {label, nullptr, nullptr, id}; }

const Item kDisplay[] = {
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
    {"Keep Bodies", [] { return OnOff(Opt().keep_bodies); }, [](int) { Opt().keep_bodies = !Opt().keep_bodies; }},
};
const Item kMouse[] = {
    {"Sensitivity X", [] { return Tenths(Opt().mouse_sensitivity); },
     [](int dir) { StepTenths(Opt().mouse_sensitivity, dir); }},
    {"Sensitivity Y", [] { return Tenths(Opt().mouse_vertical_sensitivity); },
     [](int dir) { StepTenths(Opt().mouse_vertical_sensitivity, dir); }},
    {"Invert X", [] { return OnOff(Opt().invert_mouse_x); }, [](int) { Opt().invert_mouse_x = !Opt().invert_mouse_x; }},
    {"Invert Y", [] { return OnOff(Opt().invert_mouse); }, [](int) { Opt().invert_mouse = !Opt().invert_mouse; }},
};
const Item kMoveKeys[] = {
    Key("Move Forward", "move_forward"), Key("Move Back", "move_back"), Key("Strafe Left", "strafe_left"),
    Key("Strafe Right", "strafe_right"), Key("Jump", "jump"), Key("Crouch", "crouch"), Key("Walk", "walk"),
    {"Walk Speed", [] { return std::to_string(std::clamp(Opt().walk_speed, 10u, 100u)) + "%"; },
     [](int dir) { Opt().walk_speed = uint32_t(std::clamp(int(std::clamp(Opt().walk_speed, 10u, 100u)) + 10 * dir, 10, 100)); }},
};
const Item kCombatKeys[] = {
    Key("Fire", "fire"), Key("Aim", "aim_mode"), Key("Lean", "lean"), Key("Reload", "reload"), Key("Grenade", "grenade_throw"),
    Key("Melee", "melee_attack"), Key("Action", "action"),
};
const Item kWeaponKeys[] = {
    Key("Change Weapon", "change_weapon"), Key("Primary Weapon", "primary_weapon"), Key("Pistol", "secondary_weapon"),
    Key("Special Weapon", "special_weapon"), Key("Next Weapon", "next_weapon"), Key("Previous Weapon", "prev_weapon"),
    Key("Sniper Zoom In", "sniper_zoom_up"), Key("Sniper Zoom Out", "sniper_zoom_down"),
};
const Item kLookKeys[] = {
    Key("Look Up", "look_up"), Key("Look Down", "look_down"), Key("Look Left", "look_left"),
    Key("Look Right", "look_right"), Key("Center View", "center_view"),
};
const Item kOtherKeys[] = {
    Key("Objectives", "back"), Key("Pause", "pause"), Key("D-Pad Left", "dpad_left"), Key("D-Pad Right", "dpad_right"),
    Key("Quick Save", "quick_save"), Key("Quick Load", "quick_load"),
};

struct Page {
    const char* name;  // the category row's value
    const Item* items;
    size_t count;
};
template <size_t N>
constexpr Page MakePage(const char* name, const Item (&items)[N]) {
    static_assert(N <= menu::kKeyRows, "a page has a row under the category for each item");
    return {name, items, N};
}
const Page kPages[] = {MakePage("Display", kDisplay),         MakePage("Mouse", kMouse),
                       MakePage("Move Keys", kMoveKeys),      MakePage("Combat Keys", kCombatKeys),
                       MakePage("Weapon Keys", kWeaponKeys),  MakePage("Look Keys", kLookKeys),
                       MakePage("Other Keys", kOtherKeys)};
size_t g_page = 0;  // game thread: the category shown, kept while the game runs

// the item at a place under the category on the page shown, or null
const Item* ItemAt(size_t row) {
    const Page& page = kPages[g_page];
    return row >= 1 && row - 1 < page.count ? &page.items[row - 1] : nullptr;
}

// the labels the package is made with: the category, then the first page's
std::vector<std::string> PcLabels() {
    std::vector<std::string> labels{"Category"};
    for (size_t r = 1; r < menu::kRows; r++) labels.push_back(r - 1 < kPages[0].count ? kPages[0].items[r - 1].label : "");
    return labels;
}

// the cheats (src/common/cheats.h): switched at once, the game applies them from the next frame
template <Cheat C>
Row CheatRow(const char* label) {
    return {label, [] { return OnOff(CheatOn(C)); }, [](int) { SetCheat(C, !CheatOn(C)); }};
}
const Row kCheatRows[] = {CheatRow<Cheat::GiveWeapons>("Give All Weapons"),
                          CheatRow<Cheat::InfiniteAmmo>("Infinite Ammo"), CheatRow<Cheat::GodMode>("God Mode"),
                          CheatRow<Cheat::OneShotKills>("One Shot Kills"),
                          CheatRow<Cheat::BulletShield>("Bullet Shield"),
                          CheatRow<Cheat::AchillesHead>("Achilles Head"),
                          CheatRow<Cheat::InvisibleSoldiers>("Invisible Soldiers"),
                          CheatRow<Cheat::RubberGrenades>("Rubber Grenades"),
                          {"Quick Reload",
                           [] { return ReloadSpeed() > 1 ? std::to_string(ReloadSpeed()) + "x" : std::string("Off"); },
                           [](int dir) { SetReloadSpeed(uint32_t(std::max(int(ReloadSpeed()) + dir, 1))); }},
                          {"Custom Movement", [] { return std::string(MovementName(CustomMovement())); },
                           [](int dir) {
                               int m = std::clamp(int(CustomMovement()) + dir, 0, int(Movement::Count) - 1);
                               SetCustomMovement(Movement(m));
                           }}};
static_assert(std::size(kCheatRows) == menu::kCheatRows);
bool g_saved_cheats[size_t(Cheat::Count)];  // game thread
uint32_t g_saved_reload_speed = 1;
Movement g_saved_movement = Movement::Off;

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
    std::vector<uint8_t> data;  // the package, once made
    GuestBlock block;           // the guest copy the game is given (again after a quick load that lost it)
    uint32_t copies = 0;
};
Made g_made[] = {{menu::kScreen, false}, {menu::kScreen, true}, {menu::kCheatsScreen, true},
                 {"Option_overlay.fng", false}, {menu::kMainMenu, false},
                 {"Preferences.fng", true, true}, {"MPPreferences.fng", true, true},
                 {"ObjectivesOverlay.fng", true, true}, {"Notes.fng", true, true}, {"LoadRestart.fng", true, true},
                 {"MPLeaderBoard.fng", true, true}, {"MPLoadRestart.fng", true, true},
                 {menu::kSummary, true}, {menu::kHudScreen, false}, {menu::kHudScreen, true},
                 {menu::kControllerScreen, false}, {menu::kControllerScreen, true}};

// strings for one call into the game; the same few each time, so kept (src/common/guest_block.h)
uint32_t Text(const std::string& s) {
    static std::vector<std::pair<std::string, GuestBlock>> kept;
    for (auto& [text, block] : kept)
        if (text == s) return block.HoldText(s);
    kept.emplace_back(s, GuestBlock{});
    return kept.back().second.HoldText(s);
}

// a scratch string for values, which change
uint32_t Scratch(const std::string& s) {
    static GuestBlock block;
    uint32_t va = block.Get(64);
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

// the screens

// the display mode only changes here, a window switch at every step would be too much
void Confirm() {
    if (app::Get().display_mode != g_saved.display_mode) app::ApplyDisplay();
    app::Save();
    LOGI("PC options saved");
}

void Save() {
    const app::Settings& s = app::Get();
    g_saved = {s.display_mode, s.render_percent, s.fps_cap, s.vsync, Opt()};
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
    o.keep_bodies = was.keep_bodies;
    o.mouse_sensitivity = was.mouse_sensitivity;
    o.mouse_vertical_sensitivity = was.mouse_vertical_sensitivity;
    o.invert_mouse = was.invert_mouse;
    o.invert_mouse_x = was.invert_mouse_x;
    o.walk_speed = was.walk_speed;
    if (o.keys != was.keys) {
        o.keys = was.keys;
        input::LoadBindings();
    }
}

// the cheats stay for the session, never in the settings file
void SaveCheats() {
    for (size_t c = 0; c < size_t(Cheat::Count); c++) g_saved_cheats[c] = CheatOn(Cheat(c));
    g_saved_reload_speed = ReloadSpeed();
    g_saved_movement = CustomMovement();
}
void RevertCheats() {
    for (size_t c = 0; c < size_t(Cheat::Count); c++) SetCheat(Cheat(c), g_saved_cheats[c]);
    SetReloadSpeed(g_saved_reload_speed);
    SetCustomMovement(g_saved_movement);
}

struct Screen {
    const menu::ScreenSpec& spec;
    const Row* rows;  // null for PC Options' pages
    size_t count;
    void (*save)();     // on entry
    void (*confirm)();  // confirm keeps the rows
    void (*revert)();   // back puts them as they were on entry
};
const Screen kScreens[] = {{menu::kPcOptions, nullptr, menu::kRows, Save, Confirm, Revert},
                           {menu::kCheats, kCheatRows, std::size(kCheatRows), SaveCheats, [] {}, RevertCheats}};

// one of the screens made from HUD.fng, by package name, or null
const Screen* ScreenNamed(const std::string& name) {
    for (const Screen& screen : kScreens)
        if (EqualNoCase(name, screen.spec.file)) return &screen;
    return nullptr;
}

void Show(X86Context& ctx, uint32_t package, const Screen& screen, const MenuCalls& calls) {
    for (size_t r = 0; r < screen.count; r++) {
        uint32_t value = Text(menu::RowObject(r, "value"));
        kernel::CallGuest(ctx, calls.set_text, {package, value, Scratch(screen.rows[r].show())});
    }
}

void Setup(X86Context& ctx, uint32_t package, const Screen& screen, const MenuCalls& calls) {
    screen.save();
    for (size_t r = 0; r < screen.count; r++) {
        uint32_t text = Text(menu::RowObject(r, "text"));
        kernel::CallGuest(ctx, calls.set_text, {package, text, Text(screen.rows[r].label)});
        kernel::CallGuest(ctx, calls.run_script, {package, Text(menu::RowObject(r, "value")), Text("Show")});
    }
    Show(ctx, package, screen, calls);
}

void Step(X86Context& ctx, uint32_t package, int dir, const Screen& screen, const MenuCalls& calls) {
    uint32_t focus = R32(package + kFocus);
    if (!focus) return;
    uint32_t hash = R32(focus + kObjectHash);
    for (size_t r = 0; r < screen.count; r++) {
        if (feng::Hash(menu::RowObject(r, "button")) != hash) continue;
        const Row& row = screen.rows[r];
        std::string was = row.show();
        row.step(dir);
        if (row.show() != was) kernel::CallGuest(ctx, calls.sound, {dir > 0 ? 6u : 7u});
        Show(ctx, package, screen, calls);
        return;
    }
}

// PC Options: the category row's value and those of the rows on its page
void PcValues(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    kernel::CallGuest(ctx, calls.set_text, {package, Text(menu::RowObject(0, "value")), Scratch(kPages[g_page].name)});
    for (size_t r = 1; r < menu::kRows; r++) {
        const Item* item = ItemAt(r);
        if (!item) continue;
        std::string value = item->key ? input::BoundKeys(item->key) : item->show();
        uint32_t object = Text(item->key ? menu::KeyObject(r, "value") : menu::RowObject(r, "value"));
        kernel::CallGuest(ctx, calls.set_text, {package, object, Scratch(value)});
    }
}

// each place under the category shows its page's value row or key row, or neither: the other is emptied and its
// button disabled, so up and down pass over it (0x88630 skips flag 0x04000000) and the mouse finds nothing there
void PcPage(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    for (size_t r = 1; r < menu::kRows; r++) {
        const Item* item = ItemAt(r);
        for (bool key : {false, true}) {
            auto object = key ? menu::KeyObject : menu::RowObject;
            bool on = item && (item->key != nullptr) == key;
            kernel::CallGuest(ctx, on ? calls.enable : calls.disable, {package, Text(object(r, "button"))});
            kernel::CallGuest(ctx, calls.set_text, {package, Text(object(r, "text")), Text(on ? item->label : "")});
            if (on) kernel::CallGuest(ctx, calls.run_script, {package, Text(object(r, "value")), Text("Show")});
            else kernel::CallGuest(ctx, calls.set_text, {package, Text(object(r, "value")), Text("")});
        }
    }
    PcValues(ctx, package, calls);
}

void PcSetup(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    Save();
    kernel::CallGuest(ctx, calls.set_text, {package, Text(menu::RowObject(0, "text")), Text("Category")});
    kernel::CallGuest(ctx, calls.run_script, {package, Text(menu::RowObject(0, "value")), Text("Show")});
    PcPage(ctx, package, calls);
}

// the focused place under the category and whether it is its key row, or 0
size_t Focused(uint32_t package, bool& key) {
    uint32_t focus = R32(package + kFocus);
    if (!focus) return 0;
    uint32_t hash = R32(focus + kObjectHash);
    for (size_t r = 0; r < menu::kRows; r++) {
        key = feng::Hash(menu::KeyObject(r, "button")) == hash;
        if (key || feng::Hash(menu::RowObject(r, "button")) == hash) return r;
    }
    return 0;
}

void PcStep(X86Context& ctx, uint32_t package, int dir, const MenuCalls& calls) {
    uint32_t focus = R32(package + kFocus);
    if (focus && R32(focus + kObjectHash) == feng::Hash(menu::RowObject(0, "button"))) {
        // the categories go round
        g_page = (g_page + std::size(kPages) + size_t(dir)) % std::size(kPages);
        kernel::CallGuest(ctx, calls.sound, {dir > 0 ? 6u : 7u});
        PcPage(ctx, package, calls);
        return;
    }
    bool key = false;
    const Item* item = ItemAt(Focused(package, key));
    if (!item || key || !item->step) return;
    std::string was = item->show();
    item->step(dir);
    if (item->show() != was) kernel::CallGuest(ctx, calls.sound, {dir > 0 ? 6u : 7u});
    PcValues(ctx, package, calls);
}

// a key row waiting for its key (src/common/mouse_keyboard.h), the package showing it; game thread
struct KeyWait {
    uint32_t package = 0;
    size_t row = 0;
};
KeyWait g_key_wait;

// MOHRS_MENU_TRACE: also PC Options' events, key row presses and waits (test runs)
bool MenuTrace() {
    static const bool trace = std::getenv("MOHRS_MENU_TRACE") != nullptr;
    return trace;
}

// a press on a key row: its value asks for the key
void PcPress(X86Context& ctx, uint32_t package, const MenuCalls& calls) {
    bool key = false;
    size_t row = Focused(package, key);
    const Item* item = ItemAt(row);
    if (MenuTrace()) {
        uint32_t focus = R32(package + kFocus);
        LOGR("menu trace: PC Options press, focus %08X, place %zu%s, %s, waiting %08X", focus ? R32(focus + kObjectHash) : 0,
             row, key ? " (key row)" : "", item && item->key ? item->key : "no action", g_key_wait.package);
    }
    if (!item || !key || !item->key || g_key_wait.package) return;
    input::StartCapture();
    g_key_wait = {package, row};
    kernel::CallGuest(ctx, calls.sound, {6u});
    kernel::CallGuest(ctx, calls.set_text, {package, Text(menu::KeyObject(row, "value")), Text("Press a key")});
}

void PcEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls) {
    static const uint32_t kSetup = feng::Hash("HUD Setup"), kConfirm = feng::Hash("HUD Config Confirm"),
                          kRevert = feng::Hash("HUD Config Revert"), kLeft = feng::Hash("Pad_Left"),
                          kRight = feng::Hash("Pad_Right"), kPressed = feng::Hash("Button_Pressed");
    uint32_t package = R32(ctx.ecx + kHandlerPackage);
    uint32_t event = R32(ctx.esp + 4);
    if (MenuTrace()) LOGR("menu trace: PC Options event %08X", event);
    if (event == kSetup) {
        PcSetup(ctx, package, calls);
        // the HUD screen's own setup fills in the profile's name; the rows it would set are gone
        original(ctx, base);
        return;
    }
    if (event == kLeft || event == kRight) PcStep(ctx, package, event == kRight ? 1 : -1, calls);
    if (event == kPressed) PcPress(ctx, package, calls);
    if (event == kConfirm) Confirm();
    if (event == kRevert) {
        Revert();
        // the screen stays up as the list's preview once left
        PcValues(ctx, package, calls);
    }
    kernel::Return(ctx, 0, 3);
}

// HUD Options' rows added (src/common/options_package.h), each an On/Off setting
struct Toggle {
    const char* row;
    bool Options::*setting;
    bool saved = false;  // game thread: as the screen opened
};
Toggle g_toggles[] = {{menu::kTimerRow, &Options::mission_timer}, {menu::kFpsRow, &Options::fps_counter}};

// one of a row's On and Off texts shown
void ShowToggle(X86Context& ctx, uint32_t package, const Toggle& t, const MenuCalls& calls) {
    bool on = Opt().*t.setting;
    std::string row = t.row;
    kernel::CallGuest(ctx, calls.run_script, {package, Text(row + " On"), Text(on ? "Show" : "Hide")});
    kernel::CallGuest(ctx, calls.run_script, {package, Text(row + " Off"), Text(on ? "Hide" : "Show")});
}

// The game's HUD Options handler runs its rows (Crosshairs, HUD Type); the added rows' left and right are taken here
// (the handler logs an unknown option for them and changes nothing), the others go on to it.
void HudEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls) {
    static const uint32_t kSetup = feng::Hash("HUD Setup"), kConfirm = feng::Hash("HUD Config Confirm"),
                          kRevert = feng::Hash("HUD Config Revert"), kLeft = feng::Hash("Pad_Left"),
                          kRight = feng::Hash("Pad_Right");
    uint32_t package = R32(ctx.ecx + kHandlerPackage);
    uint32_t event = R32(ctx.esp + 4);
    if (event == kLeft || event == kRight) {
        uint32_t focus = R32(package + kFocus);
        for (Toggle& t : g_toggles) {
            if (!focus || R32(focus + kObjectHash) != feng::Hash(std::string(t.row) + " button")) continue;
            bool& on = Opt().*t.setting;
            on = !on;
            // the handler's toggle sounds: 1 on, 0 off
            kernel::CallGuest(ctx, calls.sound, {on ? 1u : 0u});
            ShowToggle(ctx, package, t, calls);
            kernel::Return(ctx, 0, 3);
            return;
        }
    }
    original(ctx, base);
    uint32_t result = ctx.eax;
    bool changed = false;
    for (Toggle& t : g_toggles) {
        bool& on = Opt().*t.setting;
        if (event == kSetup) t.saved = on;
        if (event == kRevert) on = t.saved;
        if (event == kConfirm && on != t.saved) {
            changed = true;
            t.saved = on;
        }
        if (event == kSetup || event == kRevert) ShowToggle(ctx, package, t, calls);
    }
    if (changed) app::Save();
    ctx.eax = result;
}

// Controller Options' Modern Aiming row (src/common/options_package.h): one of its On and Off texts shown
void ShowAim(X86Context& ctx, uint32_t package, bool on, const MenuCalls& calls) {
    std::string row = menu::kAimRow;
    kernel::CallGuest(ctx, calls.run_script, {package, Text(row + " On"), Text(on ? "Show" : "Hide")});
    kernel::CallGuest(ctx, calls.run_script, {package, Text(row + " Off"), Text(on ? "Hide" : "Show")});
}

bool g_aim_row = false;  // game thread: the row as shown, kept on confirm like the screen's own rows

}  // namespace

void ControllerMenuEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls) {
    static const uint32_t kSetup = feng::Hash("Controller Setup"), kConfirm = feng::Hash("Controller Options Confirm"),
                          kLeft = feng::Hash("Pad_Left"), kRight = feng::Hash("Pad_Right"),
                          kButton = feng::Hash(std::string(menu::kAimRow) + " button");
    uint32_t package = R32(ctx.ecx + kHandlerPackage);
    uint32_t event = R32(ctx.esp + 4);
    if (event == kLeft || event == kRight) {
        uint32_t focus = R32(package + kFocus);
        if (focus && R32(focus + kObjectHash) == kButton) {
            g_aim_row = !g_aim_row;
            // the handler's toggle sounds: 1 on, 0 off
            kernel::CallGuest(ctx, calls.sound, {g_aim_row ? 1u : 0u});
            ShowAim(ctx, package, g_aim_row, calls);
            kernel::Return(ctx, 0, 3);
            return;
        }
    }
    original(ctx, base);
    uint32_t result = ctx.eax;
    if (event == kSetup) {
        g_aim_row = Opt().modern_aiming;
        ShowAim(ctx, package, g_aim_row, calls);
    }
    if (event == kConfirm && g_aim_row != Opt().modern_aiming) {
        Opt().modern_aiming = g_aim_row;
        app::Save();
        LOGI("controller options: modern aiming %s", g_aim_row ? "on" : "off");
    }
    ctx.eax = result;
}

void MenuFile(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t esp = ctx.esp;
    uint32_t path = R32(esp + 8);
    std::string file = FileName(kernel::ReadCString(path, 260));
    Made* made = nullptr;
    bool pause = InPauseFolder(kernel::ReadCString(path, 260));
    // a package made for both archives (HUD.fng) is the one for its folder
    for (Made& m : g_made)
        if (EqualNoCase(file, m.name) && (m.pause == pause || !ScreenNamed(m.name)) && (!made || m.pause == pause))
            made = &m;
    if (!made) {
        original(ctx, base);
        return;
    }
    // the archive's own file: the screen's pattern or the list
    const Screen* screen = ScreenNamed(made->name);
    if (screen) W32(esp + 8, Text("HUD.fng"));
    original(ctx, base);
    W32(esp + 8, path);
    if (made->data.empty() && ctx.eax) {
        size_t size = PackageSize(ctx.eax);
        const uint8_t* data = static_cast<const uint8_t*>(kernel::HostPtr(ctx.eax));
        std::vector<uint8_t> modified;
        if (screen && !screen->rows) {
            modified = menu::MakeScreen(data, size, PcLabels(), screen->spec, menu::kKeyRows);
        } else if (screen) {
            std::vector<std::string> labels;
            for (size_t r = 0; r < screen->count; r++) labels.push_back(screen->rows[r].label);
            modified = menu::MakeScreen(data, size, labels, screen->spec);
        } else if (EqualNoCase(file, "Option_overlay.fng") || EqualNoCase(file, "MPPreferences.fng")) {
            modified = menu::AddEntry(data, size);
        } else if (EqualNoCase(file, menu::kHudScreen)) {
            modified = menu::AddHudRows(data, size);
        } else if (EqualNoCase(file, menu::kControllerScreen)) {
            modified = menu::AddAimRow(data, size);
        } else if (EqualNoCase(file, menu::kSummary)) {
            modified = menu::AddMissionTime(data, size);
        } else if (EqualNoCase(file, menu::kMainMenu) || EqualNoCase(file, "LoadRestart.fng")) {
            modified = menu::AddExitEntry(data, size);
            // the pause page still gets its mouse tabs without it
            if (modified.empty() && made->tabs) modified.assign(data, data + size);
        } else if (EqualNoCase(file, "Preferences.fng")) {
            // single player and co-op: PC Options, then Cheats under it
            modified = menu::AddEntry(data, size);
            if (!modified.empty()) {
                std::vector<uint8_t> cheats = menu::AddEntry(modified.data(), modified.size(), menu::kCheats);
                if (!cheats.empty()) modified = std::move(cheats);
            }
        } else {
            modified.assign(data, data + size);
        }
        if (made->tabs && !modified.empty()) modified = menu::AddPauseTabs(modified.data(), modified.size());
        made->data = std::move(modified);
        if (!made->data.empty()) LOGR("Menus: made %s for the %s", file.c_str(), made->pause ? "pause screen" : "shell");
        else LOGE("Menus: could not make %s", file.c_str());
    }
    // the guest copy, again when a quick load lost it (src/common/guest_block.h)
    uint32_t va = 0;
    if (!made->data.empty()) {
        bool fresh = false;
        va = made->block.Hold(made->data, &fresh);
        if (fresh && made->copies++) LOGR("Menus: %s copied again, a quick load took the last copy", file.c_str());
    }
    // the stock list when its entry could not be added; no screen without a pattern
    if (va) ctx.eax = va;
    else if (screen) ctx.eax = 0;
}

void MenuScreen(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t esp = ctx.esp;
    uint32_t name = R32(esp + 4);
    bool ours = ScreenNamed(kernel::ReadCString(name, 64)) != nullptr;
    // MOHRS_MENU_TRACE: every screen the game makes, by package name (test runs)
    static const bool trace = std::getenv("MOHRS_MENU_TRACE") != nullptr;
    if (trace) LOGR("menu trace: screen %s", kernel::ReadCString(name, 64).c_str());
    if (ours) W32(esp + 4, Text("HUD.fng"));
    original(ctx, base);
    if (ours) W32(esp + 4, name);
}

void MenuEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls) {
    uint32_t handler = ctx.ecx;
    std::string name = kernel::ReadCString(handler + kHandlerName, 64);
    const Screen* screen = ScreenNamed(name);
    if (!screen) {
        if (EqualNoCase(name, menu::kHudScreen)) HudEvent(ctx, base, original, calls);
        else original(ctx, base);
        return;
    }
    if (!screen->rows) {
        PcEvent(ctx, base, original, calls);
        return;
    }
    // the HUD screen's events, which its package sends on
    static const uint32_t kSetup = feng::Hash("HUD Setup"), kConfirm = feng::Hash("HUD Config Confirm"),
                          kRevert = feng::Hash("HUD Config Revert"), kLeft = feng::Hash("Pad_Left"),
                          kRight = feng::Hash("Pad_Right");
    uint32_t package = R32(handler + kHandlerPackage);
    uint32_t event = R32(ctx.esp + 4);
    if (event == kSetup) {
        Setup(ctx, package, *screen, calls);
        // the HUD screen's own setup fills in the profile's name; the rows it would set are gone
        original(ctx, base);
        return;
    }
    if (event == kLeft || event == kRight) Step(ctx, package, event == kRight ? 1 : -1, *screen, calls);
    if (event == kConfirm) screen->confirm();
    if (event == kRevert) {
        screen->revert();
        // the screen stays up as the list's preview once left
        Show(ctx, package, *screen, calls);
    }
    kernel::Return(ctx, 0, 3);
}

void MenuKeys(X86Context& ctx, const MenuCalls& calls) {
    if (!g_key_wait.package) return;
    std::string key;
    input::Capture result = input::TakeCapture(key);
    if (result == input::Capture::Waiting) return;
    if (MenuTrace()) LOGR("menu trace: PC Options key wait ended: %d '%s'", int(result), key.c_str());
    X86Context saved = ctx;
    const Item* item = ItemAt(g_key_wait.row);
    if (item && item->key && result == input::Capture::Key) input::BindKey(item->key, key);
    if (item && item->key && result == input::Capture::Clear) input::ClearKeys(item->key);
    if (item && item->key) LOGI("PC options: %s %s", item->key, input::BoundKeys(item->key).c_str());
    kernel::CallGuest(ctx, calls.sound, {result == input::Capture::Key ? 1u : 0u});
    // the key may have left other rows' actions too
    PcValues(ctx, g_key_wait.package, calls);
    g_key_wait = {};
    ctx = saved;
}

void MenuStats(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls, uint32_t state) {
    uint32_t package = R32(ctx.esp + 4);
    bool cumulative = kernel::G<uint8_t>(ctx.esp + 8) != 0;
    original(ctx, base);
    if (cumulative || !package) return;
    if (!R32(state)) return;
    std::string text = FormatMissionTime(MissionTicksNow(state));
    kernel::CallGuest(ctx, calls.set_text, {package, Text(menu::kTimeValue), Scratch(text)});
}

void MenuMessage(X86Context& ctx) {
    static const uint32_t kExit = feng::Hash(menu::kExitMessage);
    if (R32(ctx.esp + 4) != kExit) return;
    // as the window's close button: the window goes, the message loop ends and the process with it
    static bool closing = false;
    if (closing) return;
    closing = true;
    LOGI("Menus: exit to desktop");
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
}

}  // namespace game

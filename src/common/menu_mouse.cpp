#include "common/menu_mouse.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/feng.h"
#include "common/menu_pointer.h"
#include "common/mouse_keyboard.h"
#include "common/options_package.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace game {
namespace {
using kernel::R32;
using kernel::G;
using menu::Rect;
using Clock = std::chrono::steady_clock;
constexpr uint32_t kPressed = 0x0C407210, kReleased = 0x936A6A7F, kAccept = 0x406415E3;
constexpr uint32_t kBack = 0x911AB364, kLeft = 0x9120409E, kRight = 0xB5971BF1;
constexpr uint32_t kPackage = 0xFFFFFFFD, kHandler = 0xFFFFFFFB;
constexpr uint32_t kButton = 0x10000000, kDisabled = 0x04000000;

enum class Action { None, Activate, Focus, Left, Right, Accept, Back, Resume, Tab0, Tab1, Tab2, Tab3, Mission };
struct Object {
    uint32_t package = 0, button = 0, hash = 0, guid = 0;
    bool disabled = false;
    uint32_t address = 0, button_guid = 0;
};
struct Region {
    Object object;
    Rect rect;
    Action action = Action::None;
};
struct Frame {
    std::string name;
    std::vector<Region> regions;
};
struct State {
    uint32_t engine = 0, drawing = 0;
    std::unordered_map<uint32_t, Object> objects;
    std::unordered_map<uint32_t, Frame> frames;
    bool rendering = false;
    uint32_t object_draws = 0, known_draws = 0, text_draws = 0, sprite_draws = 0;
    Clock::time_point rendered{};
    menu::PointerButtons buttons;
    uint32_t active = 0, press_package = 0, press_object = 0;
    uint32_t press_guid = 0;
    std::string active_name;
    bool foreground = false;
    float x = -1, y = -1, width = 0, height = 0;
    uint32_t tab_package = 0, tab_message = 0;
    std::string tab_name;
};
thread_local State state;

bool Trace() {
    static const bool on = std::getenv("MOHRS_MENU_MOUSE_TRACE") != nullptr;
    return on;
}

bool Readable(uint32_t va, uint32_t size = 4) {
    return va >= 0x10000 && va <= UINT32_MAX - size && mem::IsCommitted(va) && mem::IsCommitted(va + size - 1);
}

std::string Name(uint32_t package) {
    uint32_t name = R32(package + 0x0C);
    return Readable(name, 64) ? kernel::ReadCString(name, 63) : std::string();
}

int Tab(const std::string& name) {
    if (_stricmp(name.c_str(), "ObjectivesOverlay.fng") == 0 || _stricmp(name.c_str(), "MPLeaderBoard.fng") == 0) return 0;
    if (_stricmp(name.c_str(), "Notes.fng") == 0 || _stricmp(name.c_str(), "MPNotes.fng") == 0) return 1;
    if (_stricmp(name.c_str(), "Preferences.fng") == 0 || _stricmp(name.c_str(), "MPPreferences.fng") == 0) return 2;
    if (_stricmp(name.c_str(), "LoadRestart.fng") == 0 || _stricmp(name.c_str(), "MPLoadRestart.fng") == 0) return 3;
    return -1;
}

bool PauseRoot(const std::string& name) {
    return _stricmp(name.c_str(), "Pause.fng") == 0 || _stricmp(name.c_str(), "MPPause.fng") == 0;
}

bool Backdrop(const std::string& name) {
    return PauseRoot(name) || _stricmp(name.c_str(), "Shell.fng") == 0;
}

Action TextAction(uint32_t hash, const std::string& package) {
    if (_stricmp(package.c_str(), "Map_overlay.fng") == 0 || _stricmp(package.c_str(), "CoopMapOverlay.fng") == 0) {
        for (int level = 1; level <= 11; level++)
            if (hash == feng::Hash("Level" + std::to_string(level) + " text")) return Action::Mission;
    }
    if (Tab(package) >= 0) {
        static const char* headers[] = {"Objectives text", "Notes text", "Preferences text", "Load text"};
        for (int i = 0; i < 4; i++)
            if (hash == feng::Hash(headers[i])) return Action(int(Action::Tab0) + i);
        if (hash == feng::Hash("Leaderboard text")) return Action::Tab0;
        if (hash == feng::Hash("Exit text")) return Action::Tab3;
    }
    if (PauseRoot(package) && (hash == feng::Hash("Press START Text") ||
                              hash == feng::Hash("Press START Text Minus Button Word"))) return Action::Resume;
    if (hash == feng::Hash("X button text")) return Action::Accept;
    if (hash == feng::Hash("Tri button text")) return Action::Back;
    return Action::None;
}

void Objects(uint32_t object, uint32_t package, uint32_t parent_button, bool disabled, int depth, size_t& budget) {
    if (depth > 32) return;
    while (Readable(object, 0x64) && budget) {
        --budget;
        uint32_t flags = R32(object + 0x1C);
        Object info{package, (flags & kButton) ? object : parent_button,
                    R32(object + 0x10), R32(object + 0x0C), disabled || (flags & kDisabled) != 0};
        info.address = object;
        info.button_guid = info.button ? R32(info.button + 0x0C) : 0;
        if (!state.objects.emplace(object, info).second) return;
        if (R32(object + 0x18) == 5) Objects(R32(object + 0x60), package, info.button, info.disabled, depth + 1, budget);
        object = R32(object + 4);
    }
}

const Object* Drawing() {
    if (!state.rendering || !state.drawing) return nullptr;
    auto it = state.objects.find(state.drawing);
    return it == state.objects.end() || it->second.disabled ? nullptr : &it->second;
}

void Add(const Object& object, const Rect& rect, Action action) {
    if (!rect.Valid() || rect.right - rect.left > 4096 || rect.bottom - rect.top > 4096) return;
    state.frames[object.package].regions.push_back({object, rect, action});
}

// Labels and values are siblings of their button group. Its arrows can be hidden until it gains focus, so the
// label must identify the owning row independently of any arrow geometry.
uint32_t LabelButton(uint32_t hash) {
    static const std::unordered_map<uint32_t, uint32_t> labels = [] {
        std::unordered_map<uint32_t, uint32_t> result;
        for (size_t row = 0; row < menu::kRows; row++) {
            std::string prefix = "PC Row " + std::to_string(row);
            uint32_t button = feng::Hash(prefix + " button");
            result[feng::Hash(prefix + " text")] = button;
            result[feng::Hash(prefix + " value")] = button;
        }
        const char* names[] = {"Crosshairs", "HUD", "Subtitles", "Sound Format", "Music Volume", "SFX Volume",
                               "Turn Speed", "Analog Sensitivity", "Invert Y Axis", "Vibrate", "Grenade Cook"};
        for (const char* name : names) {
            uint32_t button = feng::Hash(std::string(name) + " button");
            for (const char* suffix : {" text", " On", " Off", " Automatic"})
                result[feng::Hash(std::string(name) + suffix)] = button;
        }
        for (const char* value : {"Mono", "Stereo", "Dolby"}) result[feng::Hash(value)] = feng::Hash("Sound Format button");
        return result;
    }();
    auto it = labels.find(hash);
    return it == labels.end() ? 0 : it->second;
}

// Keep exact label/value hit targets, plus a row union when arrows are visible. Arrow targets retain their
// precise left/right action; selecting a label/value only focuses the row.
void Rows(Frame& frame) {
    for (Region& r : frame.regions) {
        if (r.object.button || r.action != Action::None) continue;
        uint32_t button_hash = LabelButton(r.object.hash);
        if (!button_hash) continue;
        for (const auto& [address, object] : state.objects) {
            if (object.package != r.object.package || object.hash != button_hash || object.button != address ||
                object.disabled) continue;
            r.object.button = address;
            r.object.button_guid = object.guid;
            r.action = Action::Focus;
            break;
        }
    }
    std::unordered_map<uint32_t, Region> rows;
    for (const Region& r : frame.regions) {
        if (r.action != Action::Left && r.action != Action::Right) continue;
        Region& row = rows[r.object.button];
        row.object = r.object;
        row.rect.Include(r.rect);
        row.action = Action::Focus;
    }
    for (auto& [button, row] : rows) {
        float centre = (row.rect.top + row.rect.bottom) * 0.5f;
        for (const Region& r : frame.regions) {
            if (r.object.button == button && r.action == Action::Focus) { row.rect.Include(r.rect); continue; }
            if (r.action != Action::None || r.object.button) continue;
            if (std::fabs((r.rect.top + r.rect.bottom) * 0.5f - centre) <= 12) row.rect.Include(r.rect);
        }
        frame.regions.push_back(row);
    }
}

uint32_t ThisCall(X86Context& ctx, uint32_t function, uint32_t receiver, std::initializer_list<uint32_t> args) {
    uint32_t saved = ctx.ecx;
    ctx.ecx = receiver;
    uint32_t result = kernel::CallGuest(ctx, function, args);
    ctx.ecx = saved;
    return result;
}

void Message(X86Context& ctx, const MenuMouseCalls& calls, uint32_t engine, uint32_t package,
             uint32_t message, uint32_t object = 0) {
    ThisCall(ctx, calls.message, engine, {message, 0, package, object ? object : kPackage, 1});
    ThisCall(ctx, calls.message, engine, {message, object, package, kHandler, 1});
}

void Focus(X86Context& ctx, const MenuMouseCalls& calls, uint32_t engine, uint32_t package, uint32_t object) {
    if (object && object != R32(package + 0x80)) ThisCall(ctx, calls.focus, package, {engine, object, 1});
}

bool Live(const Region& r) {
    if (!Readable(r.object.address, 0x30) || R32(r.object.address + 0x0C) != r.object.guid ||
        R32(r.object.address + 0x10) != r.object.hash) return false;
    uint32_t object = r.object.button;
    if (!object) return true;
    if (!Readable(object, 0x30) || R32(object + 0x0C) != r.object.button_guid ||
        (R32(object + 0x1C) & kDisabled)) return false;
    // The button must still be in this package. A recycled pointer or a newly pushed modal cannot be clicked
    // using a previous frame's geometry.
    uint32_t count = R32(r.object.package + 0x88), list = R32(r.object.package + 0x84);
    if (count > 2048 || !Readable(list, count * 4)) return false;
    for (uint32_t i = 0; i < count; i++) if (R32(list + 4 * i) == object) return true;
    return false;
}

uint32_t Active(uint32_t engine, std::string& name) {
    uint32_t selected = 0;
    if (!Readable(engine, 0x110) || !G<uint8_t>(engine + 1) || !G<uint8_t>(engine + 2)) return 0;
    for (uint32_t p = R32(engine + 0xE4), n = 0; Readable(p, 0xA4) && n < 128; p = R32(p + 4), n++) {
        if (!(R32(p + 0x1C) & kernel::R16(engine + 0xD8) & 1)) continue;
        auto it = state.frames.find(p);
        if (it == state.frames.end() || it->second.name != Name(p)) continue;
        if (Backdrop(it->second.name)) continue;
        bool actionable = std::any_of(it->second.regions.begin(), it->second.regions.end(),
                                      [](const Region& r) { return r.action != Action::None; });
        if (R32(p + 0x88) || Tab(it->second.name) >= 0 || actionable) {
            selected = p;
            name = it->second.name;
        }
    }
    return selected;
}

const Region* Hit(const Frame& frame, float x, float y) {
    const Region* hit = nullptr;
    int best = -1;
    for (const Region& r : frame.regions) {
        if (r.action == Action::None || !r.rect.Contains(x, y, 3) || !Live(r)) continue;
        int rank = r.action == Action::Focus ? 0 : r.action == Action::Left || r.action == Action::Right ? 2 : 1;
        if (rank >= best) { hit = &r; best = rank; }
    }
    return hit;
}

// Development probe: timed pointer clicks through the complete hit-test path, without moving the PC's cursor.
// MOHRS_MENU_MOUSE_PROBE="8:Preferences text,10:Audio button"; times are from the first FEng input update.
void ProbePointer(input::MenuPointer& pointer, const MenuMouseCalls& calls) {
    struct Click { double at; uint32_t hash; };
    static const std::vector<Click> script = [] {
        std::vector<Click> out;
        for (const char* p = std::getenv("MOHRS_MENU_MOUSE_PROBE"); p && *p;) {
            double at;
            char name[64] = {};
            int used = 0;
            if (std::sscanf(p, " %lf:%63[^,]%n", &at, name, &used) != 2) break;
            out.push_back({at, feng::Hash(name)});
            p += used;
            if (*p == ',') p++;
        }
        return out;
    }();
    if (script.empty()) return;
    static const Clock::time_point start = Clock::now();
    double now = std::chrono::duration<double>(Clock::now() - start).count();
    for (const Click& click : script) {
        if (now < click.at - 0.25 || now >= click.at + 0.1) continue;
        for (const auto& [package, frame] : state.frames) for (const Region& r : frame.regions) {
            if (r.object.hash != click.hash || !r.rect.Valid()) continue;
            app::Mode output = app::Output();
            pointer.width = float(output.width); pointer.height = float(output.height);
            float guest_w = float(R32(calls.width)), guest_h = float(R32(calls.height));
            float x = (r.rect.left + r.rect.right) * 0.5f, y = (r.rect.top + r.rect.bottom) * 0.5f;
            bool wide = app::Widescreen();
            if (wide) x = guest_w * 0.5f + (x - guest_w * 0.5f) * 0.75f;
            float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
            float w = std::min(pointer.width, pointer.height * aspect), h = w / aspect;
            pointer.x = (pointer.width - w) * 0.5f + x * w / guest_w;
            // A distinct pre-click move lets hover run after the probe's foreground/size transition settles.
            if (now < click.at - 0.1) pointer.x -= 1;
            pointer.y = (pointer.height - h) * 0.5f + y * h / guest_h;
            pointer.active = true;
            pointer.buttons = now >= click.at ? 1u : 0u;
            pointer.pressed = 0; pointer.wheel = 0;
            return;
        }
    }
}

void Update(X86Context& ctx, uint32_t engine, const MenuMouseCalls& calls) {
    std::string name;
    uint32_t package = state.engine == engine ? Active(engine, name) : 0;
    bool fresh = Clock::now() - state.rendered < std::chrono::milliseconds(500);
    if (!fresh) package = 0;
    input::SetMenuInput(package != 0);
    static thread_local bool announced = false;
    if (package && !announced) {
        announced = true;
        LOGR("menu mouse: enabled; hover, left click, right click/back and wheel");
    }
    input::MenuPointer pointer = input::ReadMenuPointer();
    ProbePointer(pointer, calls);
    bool changed = package != state.active || name != state.active_name || pointer.active != state.foreground ||
                   pointer.width != state.width || pointer.height != state.height;
    uint32_t pressed = state.buttons.Advance(pointer.buttons, pointer.pressed, changed || !pointer.active || !package);
    bool moved = pointer.x != state.x || pointer.y != state.y;
    state.active = package; state.active_name = name; state.foreground = pointer.active;
    state.x = pointer.x; state.y = pointer.y; state.width = pointer.width; state.height = pointer.height;
    if (changed) {
        state.press_object = state.press_package = 0;
        if (Trace()) LOGR("menu mouse: active %08X %s, %zu regions", package, name.c_str(),
                          package ? state.frames[package].regions.size() : 0);
        if (Trace() && package) for (const Region& r : state.frames[package].regions) {
            if (r.action != Action::None) LOGR("menu mouse: region hash %08X button %08X action %d rect %.1f %.1f %.1f %.1f",
                r.object.hash, r.object.button, int(r.action), r.rect.left, r.rect.top, r.rect.right, r.rect.bottom);
        }
    }
    if (!package || !pointer.active) return;
    int tab = Tab(name);
    if (!(pointer.buttons & 1) && state.press_object) {
        if (state.press_package == package && Readable(state.press_object, 0x30) &&
            R32(state.press_object + 0x0C) == state.press_guid &&
            ThisCall(ctx, calls.object_message, state.press_object, {kReleased})) {
            Message(ctx, calls, engine, package, kReleased, state.press_object);
        }
        state.press_object = 0;
    }
    float x, y;
    if (!menu::MapPointer(pointer.x, pointer.y, pointer.width, pointer.height, float(R32(calls.width)),
                          float(R32(calls.height)), app::Widescreen(), x, y)) return;
    if (changed) return;
    const Region* hit = Hit(state.frames[package], x, y);
    // Select/back hints live in the backdrop and act on the CURRENT package, including PC Options' confirm/revert.
    // Resume is only available on the pause pages, never through an options screen or a confirmation dialog.
    if (!hit) {
        for (const auto& [p, frame] : state.frames)
            if (Backdrop(frame.name)) {
                bool present = false;
                for (uint32_t live = R32(engine + 0xE4), n = 0; Readable(live, 0xA4) && n < 128; live = R32(live + 4), n++)
                    if (live == p && Name(live) == frame.name) { present = true; break; }
                if (!present) continue;
                const Region* root_hit = Hit(frame, x, y);
                if (root_hit && ((root_hit->action == Action::Resume && tab >= 0) ||
                                 root_hit->action == Action::Accept || root_hit->action == Action::Back)) hit = root_hit;
            }
    }
    if (!moved && !pressed && !pointer.wheel) return;
    input::UseMenuMouse();
    if (pressed & 2) {
        if (tab >= 0) input::MenuResume();
        else Message(ctx, calls, engine, package, kBack);
        return;
    }
    if (!hit) return;
    uint32_t button = hit->object.button;
    if (hit->action == Action::Mission) {
        // The bottom title is the selected mission's display, not an independent selectable button. Use the
        // native current selection, including its unlock/input checks, just as the controller's Confirm does.
        button = R32(package + 0x80);
        auto it = state.objects.find(button);
        if (it == state.objects.end() || it->second.package != package || it->second.button != button ||
            it->second.disabled) return;
        Region selected = *hit;
        selected.object.button = button;
        selected.object.button_guid = it->second.guid;
        if (!Live(selected)) return;
    }
    Focus(ctx, calls, engine, package, button);
    if (Trace() && (pressed || pointer.wheel)) LOGR("menu mouse: %s button %08X action %d at %.1f %.1f",
                                                   name.c_str(), button, int(hit->action), x, y);
    if (pointer.wheel && button) {
        bool row = hit->action == Action::Focus || hit->action == Action::Left || hit->action == Action::Right;
        if (row) Message(ctx, calls, engine, package, pointer.wheel > 0 ? kRight : kLeft);
        else {
            uint32_t next = ThisCall(ctx, calls.navigate, package + 0x84,
                                    {button, pointer.wheel > 0 ? 0u : 4u, R32(engine + 0x10C), 0});
            Focus(ctx, calls, engine, package, next);
        }
    }
    if (!(pressed & 1)) return;
    switch (hit->action) {
        case Action::Focus:
            // A group with its own press response is an ordinary compound button, rather than an options row.
            if (!ThisCall(ctx, calls.object_message, button, {kPressed})) break;
            [[fallthrough]];
        case Action::Mission:
        case Action::Activate:
            if (ThisCall(ctx, calls.object_message, button, {kPressed})) {
                Message(ctx, calls, engine, package, kPressed, button);
                state.press_object = button; state.press_package = package;
                state.press_guid = R32(button + 0x0C);
            } else Message(ctx, calls, engine, package, kAccept);
            break;
        case Action::Left: Message(ctx, calls, engine, package, kLeft); break;
        case Action::Right: Message(ctx, calls, engine, package, kRight); break;
        case Action::Accept: Message(ctx, calls, engine, package, kAccept); break;
        case Action::Back: Message(ctx, calls, engine, package, kBack); break;
        case Action::Resume: input::MenuResume(); break;
        case Action::Tab0: case Action::Tab1: case Action::Tab2: case Action::Tab3: {
            int target = int(hit->action) - int(Action::Tab0);
            if (tab < 0 || target == tab) break;
            state.tab_package = package;
            state.tab_name = name;
            state.tab_message = feng::Hash(menu::kPauseTabEvents[target]);
            break;
        }
        default: break;
    }
}

}  // namespace

void MenuMouseUpdate(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls) {
    // Helpers can use x87/SIMD and flags too; the original update must receive its complete incoming context.
    X86Context saved = ctx;
    Update(ctx, ctx.ecx, calls);
    ctx = saved;
    original(ctx, base);
    // Queue direct transitions after this update's initialization events have dispatched. This preserves their
    // ordering without waiting for animations or opening any intervening tabs. The cloned package response
    // retains the native handler/overlay cleanup and replaces the page in the normal FEng transition queue.
    if (state.tab_package) {
        X86Context result = ctx;
        uint32_t engine = saved.ecx;
        if (Readable(engine, 0x110) && G<uint8_t>(engine + 1) && G<uint8_t>(engine + 2)) {
            for (uint32_t p = R32(engine + 0xE4), n = 0; Readable(p, 0xA4) && n < 128; p = R32(p + 4), n++) {
                if (p != state.tab_package || Name(p) != state.tab_name ||
                    !(R32(p + 0x1C) & kernel::R16(engine + 0xD8) & 1)) continue;
                Message(ctx, calls, engine, p, state.tab_message);
                if (Trace()) LOGR("menu mouse: direct tab from %s, message %08X", state.tab_name.c_str(), state.tab_message);
                break;
            }
        }
        state.tab_package = 0;
        ctx = result;
    }
}

void MenuMouseRender(X86Context& ctx, uint8_t* base, GuestFunction original) {
    state.engine = ctx.ecx;
    state.objects.clear(); state.frames.clear(); state.drawing = 0;
    state.object_draws = state.known_draws = state.text_draws = state.sprite_draws = 0;
    uint32_t callback = R32(R32(R32(ctx.ecx + 0x10C)) + 0x38);
    for (uint32_t p = R32(ctx.ecx + 0xE4), n = 0; Readable(p, 0xA4) && n < 128; p = R32(p + 4), n++) {
        std::string name = Name(p);
        static thread_local int option_reports = 0;
        if (Trace() && name == menu::kScreen && option_reports < 6) {
            option_reports++;
            LOGR("menu mouse: options input flags %02X %02X ports %X mask %X buttons %u focus %08X",
                 G<uint8_t>(ctx.ecx + 1), G<uint8_t>(ctx.ecx + 2), kernel::R16(ctx.ecx + 0xD8),
                 R32(p + 0x1C), R32(p + 0x88), R32(p + 0x80));
            uint32_t parent = R32(p + 0x30);
            LOGR("menu mouse: options package %08X parent %08X %s", p, parent,
                 Readable(parent, 0xA4) ? Name(parent).c_str() : "none");
        }
        if (!(R32(p + 0x1C) & 1) && !Backdrop(name)) continue;
        state.frames[p].name = name;
        size_t budget = 4096;
        Objects(R32(p + 0x40), p, 0, false, 0, budget);
    }
    state.rendering = true;
    original(ctx, base);
    state.rendering = false;
    for (auto& [p, frame] : state.frames) Rows(frame);
    static thread_local int reports = 0;
    if (Trace() && !state.objects.empty() && reports < 8) {
        reports++;
        LOGR("menu mouse: draw callback %08X objects %zu calls %u known %u text %u sprite %u", callback,
             state.objects.size(), state.object_draws, state.known_draws, state.text_draws, state.sprite_draws);
    }
    state.rendered = Clock::now();
}

void MenuMouseObject(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t previous = state.drawing;
    state.drawing = R32(ctx.esp + 4);
    if (state.rendering) {
        state.object_draws++;
        if (state.objects.contains(state.drawing)) state.known_draws++;
    }
    original(ctx, base);
    state.drawing = previous;
}

void MenuMouseText(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls) {
    if (const Object* object = Drawing()) {
        state.text_draws++;
        uint32_t font = R32(ctx.esp + 4), text = R32(ctx.esp + 16);
        float x = G<float>(ctx.esp + 8), y = G<float>(ctx.esp + 12);
        X86Context saved = ctx;
        ctx.esp -= 32;
        uint32_t out = ctx.esp;
        for (int i = 0; i < 4; i++) kernel::W32(out + 4 * i, 0);
        kernel::CallGuest(ctx, calls.measure_text, {font, text, out, out + 4, out + 8, out + 12});
        float left = x + G<float>(out), top = y + G<float>(out + 4);
        Rect rect{left, top, left + G<float>(out + 8), top + G<float>(out + 12)};
        ctx = saved;
        Action action = object->button ? (R32(object->button + 0x18) == 5 ? Action::Focus : Action::Activate) :
                        TextAction(object->hash, state.frames[object->package].name);
        Add(*object, rect, action);
    }
    original(ctx, base);
}

void MenuMouseSprite(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls) {
    if (const Object* object = Drawing(); object && object->button) {
        state.sprite_draws++;
        uint32_t points = R32(ctx.esp + 4), sprite = ctx.ecx;
        Rect rect;
        if (Readable(points, 64)) {
            float min_x = G<float>(points), min_y = G<float>(points + 4), max_x = min_x, max_y = min_y;
            for (int i = 0; i < 4; i++) {
                float x = G<float>(points + 16 * i), y = G<float>(points + 16 * i + 4);
                if (G<uint8_t>(sprite + 0x1A)) x = std::floor(G<float>(calls.scale_x) * x + G<float>(calls.offset_x) + 0.5f);
                if (G<uint8_t>(sprite + 0x19)) y = std::floor(G<float>(calls.scale_y) * y + G<float>(calls.offset_y) + 0.5f);
                if (!i) min_x = max_x = x, min_y = max_y = y;
                min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                max_x = std::max(max_x, x); max_y = std::max(max_y, y);
            }
            rect = {min_x, min_y, max_x, max_y};
        }
        Action action = R32(object->button + 0x18) == 5 ? Action::Focus : Action::Activate;
        if (object->hash == feng::Hash("arrow_left")) action = Action::Left;
        if (object->hash == feng::Hash("arrow_right")) action = Action::Right;
        Add(*object, rect, action);
    }
    original(ctx, base);
}

}  // namespace game

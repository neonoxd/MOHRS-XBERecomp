// Host integration test with a synthetic guest UI: exercises the drawing/input adapter without launching a game.
// tools\dev.bat clang++ -std=c++20 -Isrc -Isdk/runtime tests/menu_mouse_test.cpp src/common/menu_mouse.cpp
//     src/common/feng.cpp -o re/menu_mouse_test.exe
#include "common/menu_mouse.h"
#include "common/mouse_keyboard.h"
#include "common/feng.h"
#include "common/options_package.h"
#include "kernel/kernel.h"
#include "app/app.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
std::vector<uint8_t> memory(0x80000);
constexpr uint32_t engine = 0x10000, package = 0x20000, object = 0x22000, other = 0x22400, name = 0x21000;
constexpr uint32_t arrow_right = 0x22800;
constexpr uint32_t second_row = 0x29000, row_label = 0x29400, row_value = 0x29800;
constexpr uint32_t stack = 0x70000;
constexpr game::MenuMouseCalls calls{1, 2, 3, 4, 5, 0x25000, 0x25004, 0x25008, 0x2500C, 0x25010, 0x25014, 0x30000};
input::MenuPointer pointer{true, 10, 10, 640, 480};
bool menu_active = false, resumed = false;
struct Sent { uint32_t hash, object, target; };
std::vector<Sent> messages;
std::vector<uint32_t> drawn{object};
X86Context incoming{};
bool original_called = false;
uint32_t expected_input_mask = 1;
const char* change_during_update = nullptr;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void Noop(X86Context&, uint8_t*) {}
void DrawObject(X86Context& ctx, uint8_t* base) {
    X86Context saved = ctx;
    uint32_t which = kernel::R32(ctx.esp + 4);
    ctx.esp -= 32;
    if (kernel::R32(which + 0x18) == 1) {
        ctx.ecx = 0x28000;
        kernel::W32(ctx.esp + 4, 0x27000);
        float left = which == other ? 240.0f : 300.0f;
        for (int i = 0; i < 4; i++) {
            kernel::G<float>(0x27000 + 16 * i) = left + (i == 1 || i == 2 ? 16.0f : 0.0f);
            kernel::G<float>(0x27004 + 16 * i) = 120 + (i >= 2 ? 16.0f : 0.0f);
        }
        game::MenuMouseSprite(ctx, base, Noop, calls);
        ctx = saved;
        return;
    }
    kernel::W32(ctx.esp + 4, 0x24000); kernel::W32(ctx.esp + 16, 0x26000);
    kernel::G<float>(ctx.esp + 8) = which == object ? 100.0f : 240.0f;
    kernel::G<float>(ctx.esp + 12) = 120;
    if (which == row_label || which == row_value) {
        kernel::G<float>(ctx.esp + 8) = which == row_label ? 40.0f : 300.0f;
        kernel::G<float>(ctx.esp + 12) = 200;
    }
    game::MenuMouseText(ctx, base, Noop, calls);
    ctx = saved;
}
void Draw(X86Context& ctx, uint8_t* base) {
    X86Context saved = ctx;
    for (uint32_t va : drawn) {
        ctx.esp -= 16; kernel::W32(ctx.esp + 4, va);
        game::MenuMouseObject(ctx, base, DrawObject);
        ctx = saved;
    }
}
void OriginalUpdate(X86Context& ctx, uint8_t*) {
    Check(std::memcmp(&incoming, &ctx, sizeof(ctx)) == 0, "helper calls must preserve the update's complete context");
    original_called = true;
    ctx.eax = 0x12345678; ctx.xmm[0].f[0] = 27.5f;
    if (change_during_update) {
        std::memcpy(memory.data() + name, change_during_update, std::strlen(change_during_update) + 1);
        change_during_update = nullptr;
    }
}
void Render(X86Context& ctx) { ctx.ecx = engine; ctx.esp = stack; game::MenuMouseRender(ctx, memory.data(), Draw, calls); }
void Update(X86Context& ctx) {
    ctx.ecx = engine; ctx.esp = stack; ctx.xmm[0].f[0] = 12.5f; ctx.st[0] = 3.25;
    incoming = ctx; original_called = false;
    game::MenuMouseUpdate(ctx, memory.data(), OriginalUpdate, calls);
    Check(original_called, "original update must still run");
    Check(ctx.eax == 0x12345678 && ctx.xmm[0].f[0] == 27.5f, "post-update tab helpers preserve the returned context");
    pointer.pressed = 0; pointer.wheel = 0;
}
void SetName(const char* text) { std::memcpy(memory.data() + name, text, std::strlen(text) + 1); }
void MakeObject(uint32_t va, const char* text, bool button) {
    kernel::W32(va + 0x0C, va); kernel::W32(va + 0x10, feng::Hash(text));
    kernel::W32(va + 0x18, 2); kernel::W32(va + 0x1C, button ? 0x50000000 : 0x40000000);
}
}
namespace kernel {
uint8_t* g_base = memory.data();
std::string ReadCString(uint32_t va, size_t max) {
    return std::string(reinterpret_cast<const char*>(g_base + va), strnlen(reinterpret_cast<const char*>(g_base + va), max));
}
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == calls.focus) { Check(count == 3 && args[0] == engine, "focus ABI"); W32(ctx.ecx + 0x80, args[1]); }
    if (fn == calls.message) {
        for (const char* event : game::menu::kPauseTabEvents)
            if (args[0] == feng::Hash(event)) Check(original_called, "tab events follow native initialization dispatch");
        Check(ctx.ecx == engine && count == 5 && args[4] == expected_input_mask, "message ABI and KBM player ownership"); messages.push_back({args[0], args[1], args[3]}); }
    if (fn == calls.object_message) return R32(ctx.ecx + 0x18) == 5 ? 0 : 1;
    if (fn == calls.measure_text) {
        G<float>(args[2]) = 0; G<float>(args[3]) = 0;
        G<float>(args[4]) = 80; G<float>(args[5]) = 20;
    }
    // Simulate volatile registers and FP state being used by native helpers.
    ctx.eax = 0xBAD; ctx.xmm[0].f[0] = -5; ctx.st[0] = -9; ctx.zf = !ctx.zf;
    return 0;
}
}
namespace mem { bool IsCommitted(uint32_t va) { return va >= 0x10000 && va < memory.size(); } }
namespace logx { void Write(Level, const char*, ...) {} }
namespace app { bool Widescreen() { return false; } Mode Output() { return {640, 480}; } }
namespace input {
MenuPointer ReadMenuPointer() { return pointer; }
void SetMenuInput(bool on) { menu_active = on; }
void UseMenuMouse() {}
void MenuResume() { resumed = true; }
}

void AssignKeyboard(int player) {
    memory[calls.input_manager + 0x23E0] = 4;
    for (int i = 0; i < 4; i++)
        memory[calls.input_manager + i * 0x8F8] = uint8_t(i == player ? 0 : i < player ? i + 1 : i);
    expected_input_mask = 1u << player;
}

void RunTests(int keyboard_player) {
    using kernel::W32;
    std::fill(memory.begin(), memory.end(), 0);
    pointer = {true, 10, 10, 640, 480};
    messages.clear(); drawn = {object}; resumed = false;
    AssignKeyboard(keyboard_player);
    X86Context ctx{};
    memory[engine + 1] = memory[engine + 2] = 1;
    kernel::W16(engine + 0xD8, 0xF);
    W32(engine + 0xE4, package); W32(package + 0x0C, name);
    W32(package + 0x1C, expected_input_mask); W32(package + 0x40, object);
    W32(package + 0x84, 0x23000); W32(package + 0x88, 1); W32(0x23000, object);
    W32(calls.width, 640); W32(calls.height, 480);
    SetName("MainMenu.fng"); MakeObject(object, "Options tab", true);
    Render(ctx); Update(ctx);
    Check(menu_active && messages.empty(), "first sample enables menus without clicking");
    pointer.x = 110; pointer.y = 130; Update(ctx);
    Check(kernel::R32(package + 0x80) == object, "hover focuses the rendered text button");
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash("Button_Pressed") && messages[0].target == object &&
          messages[1].target == 0xFFFFFFFB && messages[1].object == object, "click reaches object and package handler");
    messages.clear(); Update(ctx); Check(messages.empty(), "holding does not repeat activation");
    // Same allocation reused for a different package while the mouse is still down.
    SetName("ConfirmQuit.fng"); Render(ctx); Update(ctx);
    Check(messages.empty(), "a menu-opening click cannot activate the next package");
    pointer.buttons = 0; Update(ctx); messages.clear();
    pointer.buttons = pointer.pressed = 2; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash("Pad_Back"), "right click goes through the back event");
    messages.clear(); pointer.buttons = 0; Update(ctx);
    W32(object + 0x1C, 0x54000000); Render(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "disabled control cannot be clicked");
    pointer.buttons = 0; Update(ctx);
    MakeObject(object, "Options tab", true); Render(ctx);
    pointer.active = false; pointer.buttons = 1; Update(ctx);
    pointer.active = true; pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "alt-tab with a held button does not activate a menu");
    pointer.buttons = 0; Update(ctx);
    // A pause heading requests exactly the clicked page, even during an unfinished opening animation.
    SetName("ObjectivesOverlay.fng"); MakeObject(object, "Preferences text", false);
    W32(package + 0x88, 0); W32(object + 0x54, 0x2A000);
    W32(0x2A00C, 1000); W32(0x2A010, 0);
    Render(ctx); Update(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash(game::menu::kPauseTabEvents[2]),
          "pause heading requests Preferences directly without waiting for animations");
    messages.clear(); Update(ctx);
    Check(messages.empty(), "held click cannot repeat a direct tab transition");
    SetName("Preferences.fng"); Render(ctx); Update(ctx);
    Check(messages.empty(), "no follow-up left/right events after arriving at the requested tab");
    pointer.buttons = 0; Update(ctx);
    MakeObject(object, "Objectives text", false); Render(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash(game::menu::kPauseTabEvents[0]),
          "reverse jump requests Objectives directly");
    messages.clear(); pointer.buttons = 0; Update(ctx);
    pointer.buttons = pointer.pressed = 2; Update(ctx);
    Check(resumed && messages.empty(), "right click resumes through KBM's normal Start input for any assigned player");
    resumed = false; pointer.buttons = 0; Update(ctx);
    MakeObject(object, "Preferences text", false); Render(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "clicking the current tab does not reload it");
    pointer.buttons = 0; Update(ctx);
    MakeObject(object, "Notes text", false); Render(ctx);
    change_during_update = "ConfirmQuit.fng";
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "discard a tab click if the native update replaced its package");
    W32(object + 0x54, 0);
    pointer.buttons = 0; Update(ctx);
    // Ownership follows the assigned player, even when the controller owns player 1 and KBM owns player 2.
    W32(package + 0x1C, 1u << ((keyboard_player + 1) % 4));
    Update(ctx); Check(!menu_active, "keyboard mouse does not take another player's menu");
    W32(package + 0x1C, expected_input_mask); memory[engine + 2] = 0;
    Update(ctx); Check(!menu_active, "engine input gate respected");
    memory[engine + 2] = 1; kernel::W16(engine + 0xD8, uint16_t(0xF & ~expected_input_mask));
    Update(ctx); Check(!menu_active, "engine's player mask can disable KBM input");
    kernel::W16(engine + 0xD8, 0xF);
    // A settings row uses arrow images rather than button objects; clicking the row only focuses it.
    memory[engine + 2] = 1; W32(package + 0x88, 1);
    SetName("PCOptions.fng"); MakeObject(object, "PC Row 0 button", true); W32(object + 0x18, 5);
    MakeObject(other, "arrow_left", false); MakeObject(arrow_right, "arrow_right", false);
    W32(other + 0x18, 1); W32(arrow_right + 0x18, 1);
    W32(object + 0x60, other); W32(other + 4, arrow_right);
    drawn = {other, arrow_right}; pointer.buttons = 0; Render(ctx); Update(ctx);
    pointer.x = 280; pointer.y = 128; Update(ctx); messages.clear();
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "settings row click must not confirm and close the screen");
    pointer.buttons = 0; Update(ctx); messages.clear();
    pointer.x = 305; pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash("Pad_Right"), "decorative arrow adjusts its parent row");
    messages.clear(); pointer.buttons = 0; Update(ctx);
    pointer.wheel = -120; Update(ctx);
    Check(messages.size() == 2 && messages[0].hash == feng::Hash("Pad_Left"), "wheel adjusts focused options row");
    // The unselected row has NO rendered arrows. Its separate label and value must still select that row.
    messages.clear(); pointer.buttons = 0;
    MakeObject(second_row, "PC Row 1 button", true); W32(second_row + 0x18, 5);
    MakeObject(row_label, "PC Row 1 text", false); MakeObject(row_value, "PC Row 1 value", false);
    W32(object + 4, second_row); W32(second_row + 4, row_label); W32(row_label + 4, row_value);
    W32(0x23004, second_row); W32(package + 0x88, 2);
    drawn = {other, arrow_right, row_label, row_value};
    Render(ctx); Update(ctx);
    pointer.x = 50; pointer.y = 210; Update(ctx);
    Check(kernel::R32(package + 0x80) == second_row, "hovering a row label works without visible arrows");
    W32(package + 0x80, object);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(kernel::R32(package + 0x80) == second_row && messages.empty(), "clicking the label selects its row without confirming");
    pointer.buttons = 0; Update(ctx); W32(package + 0x80, object);
    pointer.x = 310; pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(kernel::R32(package + 0x80) == second_row && messages.empty(), "clicking the displayed value selects its row");
    pointer.buttons = 0; Update(ctx);
    // The same association applies to stock settings labels, and does not bypass disabled rows.
    SetName("Audio.fng"); MakeObject(second_row, "Music Volume button", true); W32(second_row + 0x18, 5);
    MakeObject(row_label, "Music Volume text", false); Render(ctx); Update(ctx);
    pointer.x = 50; pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(kernel::R32(package + 0x80) == second_row && messages.empty(), "stock Audio label selects its row");
    pointer.buttons = 0; Update(ctx); W32(package + 0x80, object);
    W32(second_row + 0x1C, 0x54000000); Render(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(kernel::R32(package + 0x80) == object && messages.empty(), "disabled row cannot be selected through its label");
    // Only mission-map titles activate the native current selection, including during label transitions.
    pointer.buttons = 0; Update(ctx); messages.clear();
    W32(object + 4, other); W32(object + 0x60, 0); W32(other + 4, 0);
    W32(package + 0x88, 1); W32(0x23000, object); drawn = {other};
    struct Mission { const char* package; const char* label; const char* button; };
    const Mission missions[] = {
        {"Map_overlay.fng", "Level1 text", "Proxy Button 01"},
        {"CoopMapOverlay.fng", "Level1 text", "Proxy Button 11"},
    };
    for (const Mission& mission : missions) {
        SetName(mission.package); MakeObject(object, mission.button, true); MakeObject(other, mission.label, false);
        W32(package + 0x80, object); Render(ctx); Update(ctx);
        pointer.x = 250; pointer.y = 130; pointer.buttons = pointer.pressed = 1; Update(ctx);
        Check(kernel::R32(package + 0x80) == object && messages.size() == 2 &&
              messages[0].hash == feng::Hash("Button_Pressed") && messages[0].target == object,
              "mission title confirms the current selection without guessing from its label");
        messages.clear(); Update(ctx); Check(messages.empty(), "held mission-title click does not repeat");
        pointer.buttons = 0; Update(ctx); messages.clear();
    }
    W32(object + 0x1C, 0x54000000); Render(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.empty(), "mission title cannot activate a disabled mission button");
    pointer.buttons = 0; Update(ctx); MakeObject(object, "Play Game button", true);
    MakeObject(other, "Level1 text", false);
    for (const char* screen : {"Level01.fng", "CoopLevel02.fng", "Audio.fng"}) {
        SetName(screen); Render(ctx); Update(ctx);
        pointer.buttons = pointer.pressed = 1; Update(ctx);
        Check(messages.empty(), "mission details headings and unrelated labels are not clickable");
        pointer.buttons = 0; Update(ctx); messages.clear();
    }
    // A reassignment with the same package/name must settle a held click and rebuild the accessible geometry.
    SetName("Preferences.fng"); MakeObject(object, "Audio button", true);
    W32(object + 4, 0); drawn = {object}; pointer.x = 110; Render(ctx); Update(ctx);
    pointer.buttons = pointer.pressed = 1; Update(ctx); messages.clear();
    AssignKeyboard((keyboard_player + 1) % 4);
    W32(package + 0x1C, expected_input_mask); Render(ctx); Update(ctx);
    Check(messages.empty(), "held click cannot activate a menu after the KBM player assignment changes");
    pointer.buttons = 0; Update(ctx); messages.clear();
    pointer.buttons = pointer.pressed = 1; Update(ctx);
    Check(messages.size() == 2, "fresh click uses KBM's new player mask after reassignment");
    pointer.buttons = 0; Update(ctx); messages.clear();
    // KBM may be absent from a controller-only match; do not fall back to the first player's mouse ownership.
    memory[calls.input_manager + 0x23E0] = 3;
    for (int i = 0; i < 3; i++) memory[calls.input_manager + i * 0x8F8] = uint8_t(i + 1);
    Render(ctx); Update(ctx);
    Check(!menu_active, "no KBM participant means no mouse takeover of a controller player's menu");
}

int main() {
    for (int player = 0; player < 4; player++) RunTests(player);
    std::puts("menu mouse: drawing, routing, transitions, pause tabs and mission titles passed for KBM players 1-4");
}

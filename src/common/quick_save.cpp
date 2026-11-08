#include "common/quick_save.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "app/snapshot.h"
#include "common/mouse_keyboard.h"
#include "core/log.h"

namespace game {

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kShowSeconds = 2.0;

std::mutex g_message_lock;
std::string g_message;
Clock::time_point g_message_at;

void Show(const char* text) {
    LOGR("quick save: %s", text);
    std::lock_guard<std::mutex> lock(g_message_lock);
    g_message = text;
    g_message_at = Clock::now();
}

const char* Explain(snapshot::Result r, bool load) {
    switch (r) {
        case snapshot::Result::Done: return load ? "Quick loaded" : "Quick saved";
        case snapshot::Result::Busy: return load ? "Quick load failed: the game is busy" : "Quick save failed: the game is busy";
        case snapshot::Result::Missing: return "No quick save yet";
        case snapshot::Result::Elsewhere: return "Quick save is from elsewhere";
        case snapshot::Result::Mismatch: return "Quick save does not fit this run";
        case snapshot::Result::NotYet: return "Quick load failed: the game stays busy, try again";
    }
    return "";
}

// MOHRS_QUICKSAVE_PROBE="12:save,20:load,30:load": seconds after the first gameplay frame, for runs without input
struct Step {
    double at;
    bool load;
};

std::vector<Step> ProbeSteps() {
    std::vector<Step> out;
    const char* v = std::getenv("MOHRS_QUICKSAVE_PROBE");
    for (const char* p = v; p && *p;) {
        double at = 0;
        char what[8] = {};
        int used = 0;
        if (std::sscanf(p, " %lf:%7[a-z]%n", &at, what, &used) != 2) break;
        out.push_back({at, std::string(what) == "load"});
        p += used;
        if (*p == ',') p++;
    }
    if (!out.empty()) LOGR("MOHRS_QUICKSAVE_PROBE: %zu steps", out.size());
    return out;
}

}  // namespace

void QuickSaveFrame(X86Context& ctx, const void* site) {
    static bool save_held = false, load_held = false;
    static std::vector<Step> steps = ProbeSteps();
    static const Clock::time_point first = Clock::now();
    bool save_now = input::HotkeyHeld(input::Hotkey::QuickSave);
    bool load_now = input::HotkeyHeld(input::Hotkey::QuickLoad);
    bool save = save_now && !save_held, load = load_now && !load_held;
    save_held = save_now;
    load_held = load_now;
    if (!steps.empty() && std::chrono::duration<double>(Clock::now() - first).count() >= steps.front().at) {
        (steps.front().load ? load : save) = true;
        steps.erase(steps.begin());
    }
    // a load waits, a frame at a time, until the game's other threads are parked where they were at the save
    constexpr double kWaitSeconds = 5;
    static bool pending = false;
    static Clock::time_point pending_since;
    if (load && !pending) {
        pending = true;
        pending_since = Clock::now();
    }
    if (save) {
        Show(Explain(snapshot::Save(ctx, site), false));
    } else if (pending) {
        snapshot::Result r = snapshot::Load(ctx, site);
        if (r != snapshot::Result::NotYet ||
            std::chrono::duration<double>(Clock::now() - pending_since).count() > kWaitSeconds) {
            pending = false;
            Show(Explain(r, true));
        }
    }
}

void QuickSaveUi() {
    std::string text;
    double age;
    {
        std::lock_guard<std::mutex> lock(g_message_lock);
        if (g_message.empty()) return;
        age = std::chrono::duration<double>(Clock::now() - g_message_at).count();
        if (age > kShowSeconds) return;
        text = g_message;
    }
    // top centre of the picture, as BlitFrame places it, fading out
    ImVec2 window = ImGui::GetIO().DisplaySize;
    float aspect = app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = std::min(window.x, window.y * aspect);
    float h = w / aspect;
    float size = std::max(16.0f, h / 28.0f);
    ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    ImVec2 at((window.x - extent.x) / 2, (window.y - h) / 2 + size * 1.5f);
    int alpha = int(255 * std::clamp((kShowSeconds - age) / 0.5, 0.0, 1.0));
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddText(nullptr, size, ImVec2(at.x + 2, at.y + 2), IM_COL32(0, 0, 0, alpha * 200 / 255), text.c_str());
    draw->AddText(nullptr, size, at, IM_COL32(255, 255, 255, alpha * 230 / 255), text.c_str());
}

}  // namespace game

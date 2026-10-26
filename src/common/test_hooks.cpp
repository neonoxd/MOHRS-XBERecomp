#include "common/test_hooks.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

using kernel::R32;
using kernel::W32;

namespace game {

namespace {

std::string GuestString(uint32_t va, size_t max = 64) {
    std::string s;
    for (char c; s.size() < max && (c = char(kernel::g_base[va])) != 0; va++) s += c;
    return s;
}

}  // namespace

void PickStartLevel(X86Context& ctx) {
    static bool first = true;
    if (!first) return;
    first = false;
    int mission = 0, level = 0;
    const char* v = std::getenv("MOHRS_START_LEVEL");
    // thiscall: mission and level are the stack arguments after the return address
    if (v && std::sscanf(v, "%d_%d", &mission, &level) == 2 && R32(ctx.esp + 4) == 1 && R32(ctx.esp + 8) == 1) {
        LOGR("MOHRS_START_LEVEL: starting %d_%d instead of 1_1", mission, level);
        W32(ctx.esp + 4, uint32_t(mission));
        W32(ctx.esp + 8, uint32_t(level));
    }
}

void SkipMovie(uint32_t key, uint32_t buffer, uint32_t size) {
    const Options& o = Opt();
    if (size < 5 || (!o.skip_intros && !o.skip_cutscenes)) return;
    std::string k = GuestString(key);
    bool intro = k == "legal_movie" || k == "legal_intro_movie";
    bool cutscene = k == "intro_movie" || k == "outro_movie";
    if ((intro && o.skip_intros) || (cutscene && o.skip_cutscenes)) {
        LOG_FIRST(32, "movies: skipping %s %s", k.c_str(), GuestString(buffer).c_str());
        std::memcpy(kernel::g_base + buffer, "none", 5);
    }
}

void FrameWait(uint32_t& blanks) {
    if (Opt().unlocked_fps && blanks < 2) blanks = 2;
    else if (Opt().fps60 && blanks == 1) blanks = 2;
}

void ProbePad(input::Pad& pad) {
    struct Press {
        double at;
        std::string what;
        bool captured = false;
    };
    using Clock = std::chrono::steady_clock;
    static const std::vector<Press> script = [] {
        std::vector<Press> out;
        const char* v = std::getenv("MOHRS_MENU_PROBE");
        for (const char* p = v; p && *p;) {
            double at = 0;
            char what[16] = {};
            int used = 0;
            if (std::sscanf(p, " %lf:%15[a-z]%n", &at, what, &used) != 2) break;
            out.push_back({at, what});
            p += used;
            if (*p == ',') p++;
        }
        if (!out.empty()) LOGR("MOHRS_MENU_PROBE: %zu presses", out.size());
        return out;
    }();
    static std::vector<Press> left = script;
    if (left.empty()) return;
    static const Clock::time_point start = Clock::now();
    double now = std::chrono::duration<double>(Clock::now() - start).count();
    constexpr double kHold = 0.1;
    for (Press& p : left) {
        if (now < p.at || now > p.at + kHold) continue;
        const std::string& w = p.what;
        if (w == "up") pad.buttons |= 0x01;
        if (w == "down") pad.buttons |= 0x02;
        if (w == "left") pad.buttons |= 0x04;
        if (w == "right") pad.buttons |= 0x08;
        if (w == "start") pad.buttons |= 0x10;
        if (w == "back") pad.buttons |= 0x20;
        if (w == "a") pad.analog[0] = 255;
        if (w == "b") pad.analog[1] = 255;
        if (w == "capture" && !p.captured) {
            p.captured = true;
            gpu::g_capture = true;
            LOGR("MOHRS_MENU_PROBE: capture at %.1f s", now);
        }
    }
}

}  // namespace game

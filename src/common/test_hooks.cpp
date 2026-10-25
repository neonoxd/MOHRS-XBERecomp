#include "common/test_hooks.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/options.h"
#include "core/log.h"
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
    if (Opt().fps60 && blanks == 1) blanks = 2;
}

}  // namespace game

// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using kernel::R32;
using kernel::W32;

namespace {

std::string GuestString(uint32_t va, size_t max = 64) {
    std::string s;
    for (char c; s.size() < max && (c = char(kernel::g_base[va])) != 0; va++) s += c;
    return s;
}

}  // namespace

// Movie skipping. The game reads every movie's name from data\game.cfg through its config's GetString(key, buffer,
// size), and "none" there means no movie, as game.cfg itself writes for levels without one. Skipped keys answer
// "none": skip_intros the legal_movie and legal_intro_movie the boot plays (EA logo, the opening Pearl Harbor
// movie), skip_cutscenes each level's intro_movie and outro_movie. The bonus movies picked in the menus play.
X86_EXTERN_FUNC(__imp__sub_0010EA00);
X86_FUNC(sub_0010EA00) {
    uint32_t key = R32(ctx.esp + 4), buffer = R32(ctx.esp + 8), size = R32(ctx.esp + 12);
    __imp__sub_0010EA00(ctx, base);
    if (!(ctx.eax & 0xFF) || size < 5) return;  // not found: nothing was written
    const game::Options& o = game::Opt();
    if (!o.skip_intros && !o.skip_cutscenes) return;
    std::string k = GuestString(key);
    bool intro = k == "legal_movie" || k == "legal_intro_movie";
    bool cutscene = k == "intro_movie" || k == "outro_movie";
    if ((intro && o.skip_intros) || (cutscene && o.skip_cutscenes)) {
        LOG_FIRST(32, "movies: skipping %s %s", k.c_str(), GuestString(buffer).c_str());
        std::memcpy(kernel::g_base + buffer, "none", 5);
    }
}

// Development: MOHRS_START_LEVEL=<mission>_<level> (e.g. 1_2) starts that level instead of the first one. The boot
// code (0x12614 to 0x12642) picks the level by calling the game state's SetCurrentLevel(mission, level) once: 1, 1
// on a fresh profile (legal intro, then the level), 1, 16 for the logo and the front end. Only that first call is
// changed, and only on the fresh profile path, so the menus and level changes later on behave as on the console.
X86_EXTERN_FUNC(__imp__sub_0009D180);
X86_FUNC(sub_0009D180) {
    static bool first = true;
    if (first) {
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
    __imp__sub_0009D180(ctx, base);
}

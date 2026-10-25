// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include <cstdio>
#include <cstdlib>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using kernel::R32;
using kernel::W32;

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

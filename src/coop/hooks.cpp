// Hooks for moh3rdc.xbe (co-op). A strong definition replaces the recompiled function at that address. The same
// game functions as in src/game/hooks.cpp, found by their bytes in this build of the game code.
#include "common/test_hooks.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using kernel::R32;

// the game state's SetCurrentLevel(mission, level), which the boot calls at 0x125E3 and 0x125FD
X86_EXTERN_FUNC(__imp__sub_0008FB00);
X86_FUNC(sub_0008FB00) {
    game::PickStartLevel(ctx);
    __imp__sub_0008FB00(ctx, base);
}

// the config's GetString(key, buffer, size), which reads data\game.cfg; true when the key exists. 0x1071B0 has the
// same bytes, but the movie keys (0x949AD, 0x914AF, 0x914EB, 0x949D2) are read through this one
X86_EXTERN_FUNC(__imp__sub_00107280);
X86_FUNC(sub_00107280) {
    uint32_t key = R32(ctx.esp + 4), buffer = R32(ctx.esp + 8), size = R32(ctx.esp + 12);
    __imp__sub_00107280(ctx, base);
    if (ctx.eax & 0xFF) game::SkipMovie(key, buffer, size);
}

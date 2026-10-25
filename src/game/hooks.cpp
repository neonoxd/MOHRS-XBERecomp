// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "common/test_hooks.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using kernel::R32;

// the game state's SetCurrentLevel(mission, level), which the boot calls at 0x12623 and 0x1263D
X86_EXTERN_FUNC(__imp__sub_0009D180);
X86_FUNC(sub_0009D180) {
    game::PickStartLevel(ctx);
    __imp__sub_0009D180(ctx, base);
}

// the config's GetString(key, buffer, size), which reads data\game.cfg; true when the key exists
X86_EXTERN_FUNC(__imp__sub_0010EA00);
X86_FUNC(sub_0010EA00) {
    uint32_t key = R32(ctx.esp + 4), buffer = R32(ctx.esp + 8), size = R32(ctx.esp + 12);
    __imp__sub_0010EA00(ctx, base);
    if (ctx.eax & 0xFF) game::SkipMovie(key, buffer, size);
}

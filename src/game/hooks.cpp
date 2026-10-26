// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "common/mouse_look.h"
#include "common/fov.h"
#include "common/frame_timing.h"
#include "common/options_menu.h"
#include "common/test_hooks.h"
#include "common/widescreen.h"
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

// mouse look (src/common/mouse_look.h); the player list is the game's, by controller
constexpr uint32_t kPlayers = 0x270108;
// [[hook]] 0xE0AEE in the player's look update 0xE0A50: esi the player, before the stick's rates are added
void MouseLook(X86Context& ctx, uint8_t*) { game::MouseLook(ctx, kPlayers); }
// [[hook]] 0xDC911 in the mounted gun's update: esi the player, before the stick's rates are added
void TurretLook(X86Context& ctx, uint8_t*) { game::TurretLook(ctx, kPlayers); }

// the camera's SetFov(half width, half height) in radians (thiscall, ret 8), 0x82BA0: the view widened for 16:9
X86_EXTERN_FUNC(__imp__sub_00082BA0);
X86_FUNC(sub_00082BA0) {
    uint32_t camera = ctx.ecx;
    __imp__sub_00082BA0(ctx, base);
    game::WidenView(camera);
    game::AdjustPlayerFov(camera, kPlayers, 0x1FC658, 0x1150);
}

// 60 fps (src/common/test_hooks.h): [[hook]]s on the frame's end compares of the blanks passed with 2, eax at 0x13AAF and
// ecx at 0x13ACA
void FrameWaitEax(X86Context& ctx, uint8_t*) { game::FrameWait(ctx.eax); }
void FrameWaitEcx(X86Context& ctx, uint8_t*) { game::FrameWait(ctx.ecx); }

void FrameDelta(X86Context& ctx, uint8_t*) {
    game::FrameDelta(ctx);
    game::TimingFrame(ctx.xmm[0].f[0], 0x270108, 0x2447D8);
}
void MissionTicks(X86Context& ctx, uint8_t*) { game::MissionTicks(ctx); }
void MenuTicks(X86Context& ctx, uint8_t*) { game::MenuTicks(ctx); }
void MessageTicks(X86Context& ctx, uint8_t*) { game::MessageTicks(ctx); }
void ResetLoadingAnimation(X86Context&, uint8_t*) { game::ResetLoadingAnimation(); }
void LoadingAnimation(X86Context& ctx, uint8_t*) { game::LoadingAnimation(ctx); }
void ResetPopupAnimation(X86Context&, uint8_t*) { game::ResetPopupAnimation(); }
void PopupAnimation(X86Context& ctx, uint8_t*) { game::PopupAnimation(ctx); }

// PC Options in the options menus (src/common/options_menu.h): SetText 0x138A40, RunScript 0x138330, the menu sound
// 0x123C10
constexpr game::MenuCalls kMenuCalls = {0x138A40, 0x138330, 0x123C10};
// the open archive's file lookup, which the shell's package loader (0x122BC0) asks for each .fng
X86_EXTERN_FUNC(__imp__sub_001132E0);
X86_FUNC(sub_001132E0) { game::MenuFile(ctx, base, __imp__sub_001132E0); }
// the screen handler factory, by package name
X86_EXTERN_FUNC(__imp__sub_00120F00);
X86_FUNC(sub_00120F00) { game::MenuScreen(ctx, base, __imp__sub_00120F00); }
// the HUD options screen's event handler (vtable 0x1ED40C)
X86_EXTERN_FUNC(__imp__sub_0012A620);
X86_FUNC(sub_0012A620) { game::MenuEvent(ctx, base, __imp__sub_0012A620, kMenuCalls); }

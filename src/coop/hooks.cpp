// Hooks for moh3rdc.xbe (co-op). A strong definition replaces the recompiled function at that address. The same
// game functions as in src/game/hooks.cpp, found by their bytes in this build of the game code.
#include "common/mouse_look.h"
#include "common/controller_routing.h"
#include "common/fov.h"
#include "common/frame_timing.h"
#include "common/options_menu.h"
#include "common/menu_mouse.h"
#include "common/test_hooks.h"
#include "common/widescreen.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using kernel::R32;

X86_EXTERN_FUNC(__imp__sub_0015AA90);
X86_FUNC(sub_0015AA90) {
    game::UpdateControllerMode({0x23E5B1, 0x23A615, 0x23A616, 0x206F90, 0x206F91});
    __imp__sub_0015AA90(ctx, base);
}

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

// Mouse look, using the native player-to-port assignments (default.xbe's input manager at 0x271188).
constexpr uint32_t kPlayers = 0x265278;
// [[hook]] 0xD8A1E in the player's look update 0xD8980: esi the player, before the stick's rates are added.
void MouseLook(X86Context& ctx, uint8_t*) { game::MouseLook(ctx, kPlayers, 0x266618, game::kCoopMouseLook); }
// [[hook]] 0xD6151 in the mounted gun branch of 0xD4BF0, before angle integration and the native co-op limits.
void TurretLook(X86Context& ctx, uint8_t*) { game::TurretLook(ctx, kPlayers, 0x266618, game::kCoopMouseLook); }

// the camera's SetFov(half width, half height) in radians (thiscall, ret 8), the bytes of default.xbe's 0x82BA0: the
// view widened for 16:9
X86_EXTERN_FUNC(__imp__sub_000754F0);
X86_FUNC(sub_000754F0) {
    uint32_t camera = ctx.ecx;
    __imp__sub_000754F0(ctx, base);
    game::WidenView(camera);
    game::AdjustPlayerFov(camera, kPlayers, 0x1F54B8, 0x1230);
}

// 60 fps (src/common/test_hooks.h): [[hook]]s on the frame's end compares of the blanks passed with 2, eax at 0x13AFF and
// ecx at 0x13B1A
void FrameWaitEax(X86Context& ctx, uint8_t*) { game::FrameWait(ctx.eax); }
void FrameWaitEcx(X86Context& ctx, uint8_t*) { game::FrameWait(ctx.ecx); }

void FrameDelta(X86Context& ctx, uint8_t*) {
    game::FrameDelta(ctx);
    game::TimingFrame(ctx.xmm[0].f[0], 0x265278, 0x23C9D0);
}
void MissionTicks(X86Context& ctx, uint8_t*) { game::MissionTicks(ctx); }
void MenuTicks(X86Context& ctx, uint8_t*) { game::MenuTicks(ctx); }
void MessageTicks(X86Context& ctx, uint8_t*) { game::MessageTicks(ctx); }
void ResetLoadingAnimation(X86Context&, uint8_t*) { game::ResetLoadingAnimation(); }
void LoadingAnimation(X86Context& ctx, uint8_t*) { game::LoadingAnimation(ctx); }
void ResetPopupAnimation(X86Context&, uint8_t*) { game::ResetPopupAnimation(); }
void PopupAnimation(X86Context& ctx, uint8_t*) { game::PopupAnimation(ctx); }
// [[hook]] 0x625B4, the animation event scan's loop wrap branch; true goes to its forward scan at 0x6254D
bool AnimEventFirstScan(X86Context& ctx, uint8_t*) { return game::AnimEventFirstScan(ctx); }

// PC Options in the options menus (src/common/options_menu.h), the bytes of default.xbe's: SetText 0x131D10 (0x138A40),
// RunScript 0x131600 (0x138330), the menu sound 0x11DC90 (0x123C10)
constexpr game::MenuCalls kMenuCalls = {0x131D10, 0x131600, 0x11DC90};
// the open archive's file lookup (0x1132E0)
X86_EXTERN_FUNC(__imp__sub_0010BDA0);
X86_FUNC(sub_0010BDA0) { game::MenuFile(ctx, base, __imp__sub_0010BDA0); }
// the screen handler factory (0x120F00)
X86_EXTERN_FUNC(__imp__sub_0011B1B0);
X86_FUNC(sub_0011B1B0) { game::MenuScreen(ctx, base, __imp__sub_0011B1B0); }
// the HUD options screen's event handler (0x12A620)
X86_EXTERN_FUNC(__imp__sub_00124C20);
X86_FUNC(sub_00124C20) { game::MenuEvent(ctx, base, __imp__sub_00124C20, kMenuCalls); }

// The same FEng pointer/drawing paths as default.xbe, verified in the co-op image.
constexpr game::MenuMouseCalls kMouseMenu = {
    0x892A0, 0x85BC0, 0x88270, 0x15BEF0, 0x7AFB0,
    0x1F54E0, 0x1F54E4, 0x23E66C, 0x23E670, 0x23E62C, 0x23E628, 0x266618};
X86_EXTERN_FUNC(__imp__sub_00087B80);
X86_FUNC(sub_00087B80) { game::MenuMouseUpdate(ctx, base, __imp__sub_00087B80, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_00086840);
X86_FUNC(sub_00086840) { game::MenuMouseRender(ctx, base, __imp__sub_00086840, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_0010EFB0);
X86_FUNC(sub_0010EFB0) { game::MenuMouseObject(ctx, base, __imp__sub_0010EFB0); }
// ShellManager's drawing override (default.xbe 0x125E10).
X86_EXTERN_FUNC(__imp__sub_0011FE90);
X86_FUNC(sub_0011FE90) { game::MenuMouseObject(ctx, base, __imp__sub_0011FE90); }
X86_EXTERN_FUNC(__imp__sub_0015BEB0);
X86_FUNC(sub_0015BEB0) { game::MenuMouseText(ctx, base, __imp__sub_0015BEB0, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_000AB430);
X86_FUNC(sub_000AB430) { game::MenuMouseSprite(ctx, base, __imp__sub_000AB430, kMouseMenu); }

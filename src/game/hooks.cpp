// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
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

// Publish the native shell/game mode before this frame's XInput reads (docs/controller-routing.md).
X86_EXTERN_FUNC(__imp__sub_001616C0);
X86_FUNC(sub_001616C0) {
    game::UpdateControllerMode({0x2463B1, 0x242435, 0x242436, 0x20EDB0, 0x20EDB1});
    __imp__sub_001616C0(ctx, base);
}

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

// Mouse look (src/common/mouse_look.h), using the native player-to-port assignments.
constexpr uint32_t kPlayers = 0x270108;
// [[hook]] 0xE0AEE in the player's look update 0xE0A50: esi the player, before the stick's rates are added
void MouseLook(X86Context& ctx, uint8_t*) { game::MouseLook(ctx, kPlayers, 0x271188); }
// [[hook]] 0xDC911 in the mounted gun's update: esi the player, before the stick's rates are added
void TurretLook(X86Context& ctx, uint8_t*) { game::TurretLook(ctx, kPlayers, 0x271188); }

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
// [[hook]] 0x6FB74, the animation event scan's loop wrap branch; true goes to its forward scan at 0x6FB0D
bool AnimEventFirstScan(X86Context& ctx, uint8_t*) { return game::AnimEventFirstScan(ctx); }

// an entity's animation event listener (vtable 0x1E84F0 slot 1): dialogue lines start here (opcode 0xCAF0)
X86_EXTERN_FUNC(__imp__sub_000CF780);
X86_FUNC(sub_000CF780) {
    game::TraceAnimEvent(ctx);
    __imp__sub_000CF780(ctx, base);
}

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

// FEng menu pointer adapter; actual text/sprite draws supply the clickable geometry (docs/menu-mouse.md).
constexpr game::MenuMouseCalls kMouseMenu = {
    0x96900, 0x93260, 0x958D0, 0x162B20, 0x88630,
    0x1FC680, 0x1FC684, 0x246450, 0x246454, 0x246424, 0x246420, 0x271188};
X86_EXTERN_FUNC(__imp__sub_00095220);
X86_FUNC(sub_00095220) { game::MenuMouseUpdate(ctx, base, __imp__sub_00095220, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_00093EE0);
X86_FUNC(sub_00093EE0) { game::MenuMouseRender(ctx, base, __imp__sub_00093EE0, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_001164E0);
X86_FUNC(sub_001164E0) { game::MenuMouseObject(ctx, base, __imp__sub_001164E0); }
// ShellManager overrides the base FengInterface drawing callback; both shell and pause use this vtable slot.
X86_EXTERN_FUNC(__imp__sub_00125E10);
X86_FUNC(sub_00125E10) { game::MenuMouseObject(ctx, base, __imp__sub_00125E10); }
X86_EXTERN_FUNC(__imp__sub_00162AE0);
X86_FUNC(sub_00162AE0) { game::MenuMouseText(ctx, base, __imp__sub_00162AE0, kMouseMenu); }
X86_EXTERN_FUNC(__imp__sub_000B6B90);
X86_FUNC(sub_000B6B90) { game::MenuMouseSprite(ctx, base, __imp__sub_000B6B90, kMouseMenu); }

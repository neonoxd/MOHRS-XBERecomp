// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "common/mouse_look.h"
#include "common/controller_routing.h"
#include "common/fov.h"
#include "common/frame_timing.h"
#include "common/options_menu.h"
#include "common/quick_save.h"
#include "common/menu_mouse.h"
#include "common/stream_lock.h"
#include "common/test_hooks.h"
#include "common/weapon_select.h"
#include "common/widescreen.h"
#include "common/mouse_keyboard.h"
#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

using kernel::R32;

// Publish the native shell/game mode before this frame's XInput reads (docs/input.md).
X86_EXTERN_FUNC(__imp__sub_001616C0);
X86_FUNC(sub_001616C0) {
    game::UpdateControllerMode({0x2463B1, 0x242435, 0x242436, 0x20EDB0, 0x20EDB1});
    input::SetFrontEnd(kernel::G<uint8_t>(0x2463B1) != 0);
    __imp__sub_001616C0(ctx, base);
}

// the EA stream library's packet take, under its stream's lock (src/common/stream_lock.h)
X86_EXTERN_FUNC(__imp__sub_00165390);
X86_FUNC(sub_00165390) { game::LockedStreamTake(ctx, base, __imp__sub_00165390, game::kDefaultStreams); }

// Weapon keys (src/common/weapon_select.h): the input manager's action query and the weapon select
X86_EXTERN_FUNC(__imp__sub_000B83E0);
X86_FUNC(sub_000B83E0) {
    uint32_t index = R32(ctx.esp + 4), action = kernel::G<uint16_t>(ctx.esp + 8);
    __imp__sub_000B83E0(ctx, base);
    game::WeaponSwitchQuery(ctx, index, action, game::kDefaultWeapons);
}
X86_EXTERN_FUNC(__imp__sub_000D8720);
X86_FUNC(sub_000D8720) {
    game::WeaponSelect(ctx, game::kDefaultWeapons);
    __imp__sub_000D8720(ctx, base);
}

// the game state's SetCurrentLevel(mission, level), which the boot calls at 0x12623 and 0x1263D
X86_EXTERN_FUNC(__imp__sub_0009D180);
X86_FUNC(sub_0009D180) {
    game::PickStartLevel(ctx);
    __imp__sub_0009D180(ctx, base);
}

// the main gameplay frame, called once a frame from the level's loop at 0x128B9: its entry is where a quick save is
// taken and put back (src/common/quick_save.h), the host's call chain being the same there every frame
X86_EXTERN_FUNC(__imp__sub_000A4A90);
X86_FUNC(sub_000A4A90) {
    game::QuickSaveFrame(ctx, reinterpret_cast<const void*>(&sub_000A4A90));
    __imp__sub_000A4A90(ctx, base);
}

// the script system's frame update (docs/frame-timing.md): at the console's 30 Hz in the 60 FPS and unlocked modes
X86_EXTERN_FUNC(__imp__sub_0004C0F0);
X86_FUNC(sub_0004C0F0) {
    if (game::ScriptFrame(ctx)) __imp__sub_0004C0F0(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}

// the entity manager's update (docs/frame-timing.md): steps under 1/64 tick carried over in the 60 FPS and unlocked
// modes instead of standing still
X86_EXTERN_FUNC(__imp__sub_000FAB00);
X86_FUNC(sub_000FAB00) {
    if (game::EntityStep(ctx)) __imp__sub_000FAB00(ctx, base);
    else kernel::Return(ctx, ctx.eax, 1);
}

// the player's view shake and recoil update (docs/frame-timing.md): at the console's 30 Hz in the 60 FPS and unlocked
// modes
X86_EXTERN_FUNC(__imp__sub_000D6700);
X86_FUNC(sub_000D6700) {
    if (game::ShakeFrame(ctx)) __imp__sub_000D6700(ctx, base);
    else kernel::Return(ctx, ctx.eax, 3);
}

// the game's vertex helper, stdcall (position, color, uv): SetVertexDataColor(3), SetVertexData2f(9) and
// SetVertexData4f(-1), one call each per vertex of every effect (38 callers); here the values go to the primitive
// directly (docs/performance.md)
X86_FUNC(sub_0017E4C0) {
    (void)base;
    uint32_t pos = kernel::R32(ctx.esp + 4), color = kernel::R32(ctx.esp + 8);
    d3d::ImmediateColor(3, kernel::R32(color));
    uint32_t uv = kernel::R32(ctx.esp + 12);
    float t[4] = {kernel::G<float>(uv), kernel::G<float>(uv + 4), 0, 1};
    d3d::ImmediateData(9, t);
    float v[4] = {kernel::G<float>(pos), kernel::G<float>(pos + 4), kernel::G<float>(pos + 8), kernel::G<float>(pos + 12)};
    d3d::ImmediateData(0, v);
    kernel::Return(ctx, ctx.eax, 3);
}

// a weapon's muzzle puff, fastcall (weapon): at most once per console frame above 30 FPS (docs/frame-timing.md)
X86_EXTERN_FUNC(__imp__sub_000E2CF0);
X86_FUNC(sub_000E2CF0) {
    if (game::MuzzlePuffFrame(ctx.ecx)) __imp__sub_000E2CF0(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}

// a weapon's state update, thiscall (time, no-fire flag), ret 8: its time for RefireTime (docs/frame-timing.md)
X86_EXTERN_FUNC(__imp__sub_00151320);
X86_FUNC(sub_00151320) {
    game::WeaponUpdate(ctx);
    __imp__sub_00151320(ctx, base);
    game::WeaponUpdateDone();
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
void MouseLook(X86Context& ctx, uint8_t*) { game::MouseLook(ctx, kPlayers, 0x271188, game::kDefaultMouseLook); }
// [[hook]] 0xDC911 in the mounted gun's update: esi the player, before the stick's rates are added
void TurretLook(X86Context& ctx, uint8_t*) { game::TurretLook(ctx, kPlayers, 0x271188, game::kDefaultMouseLook); }

// the camera's SetFov(half width, half height) in radians (thiscall, ret 8), 0x82BA0: the view widened for 16:9
X86_EXTERN_FUNC(__imp__sub_00082BA0);
X86_FUNC(sub_00082BA0) {
    uint32_t camera = ctx.ecx;
    __imp__sub_00082BA0(ctx, base);
    game::WidenView(camera);
    game::AdjustPlayerFov(camera, kPlayers, 0x1FC658, 0x1150);
}

// The player's near plane pulled in while the view is wider than the original (src/common/fov.h, docs/fov.md):
// the camera's frustum build 0x82E50 (fastcall camera) and the view's projection build 0x84290 (thiscall (flag),
// ret 4, camera at [view + 0xE4]) are what read it
X86_EXTERN_FUNC(__imp__sub_00082E50);
X86_FUNC(sub_00082E50) {
    game::PlayerNearPlane near(ctx.ecx, kPlayers, 0x1FC658, 0x1150);
    __imp__sub_00082E50(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00084290);
X86_FUNC(sub_00084290) {
    game::PlayerNearPlane near(R32(ctx.ecx + 0xE4), kPlayers, 0x1FC658, 0x1150);
    __imp__sub_00084290(ctx, base);
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
void PopupTicks(X86Context& ctx, uint8_t*) { game::PopupTicks(ctx); }
void MessageTicks(X86Context& ctx, uint8_t*) { game::MessageTicks(ctx); }
// [[hook]]s in the soldier AI brain: goal timer ticks at 0x197C6, threat memory decay at 0x223A8
void BrainTicks(X86Context& ctx, uint8_t*) { game::BrainTicks(ctx); }
void ThreatDecay(X86Context& ctx, uint8_t*) { game::ThreatDecay(ctx); }
void FloorRayWorld(X86Context& ctx, uint8_t*) { game::FloorRayWorld(ctx); }
void TraceNative(X86Context& ctx, uint8_t*) { game::TraceNative(ctx); }
// [[hook]] 0x162E70, the top of the resource loader worker's loop (0x162E30): its slot from the table at [0x399758]
// again, the worker's index being its argument at [esp+0x24] there, so a quick load's table is the one it serves
void LoaderSlot(X86Context& ctx, uint8_t*) { ctx.ebx = R32(0x399758) + R32(ctx.esp + 0x24) * 0xB8; }
void FloorRayRetry(X86Context& ctx, uint8_t*) {
    game::FloorRayRetry(ctx, {0x80, 0xC0, 0x40, 0x144C30, 0x142910, 0x142AB0, 0x7A8A0});
}
void ResetLoadingAnimation(X86Context&, uint8_t*) { game::ResetLoadingAnimation(); }
void LoadingAnimation(X86Context& ctx, uint8_t*) { game::LoadingAnimation(ctx); }
void ResetPopupAnimation(X86Context&, uint8_t*) { game::ResetPopupAnimation(); }
void PopupAnimation(X86Context& ctx, uint8_t*) { game::PopupAnimation(ctx); }
// [[hook]] 0x6FB74, the animation event scan's loop wrap branch; true goes to its forward scan at 0x6FB0D
bool AnimEventFirstScan(X86Context& ctx, uint8_t*) { return game::AnimEventFirstScan(ctx); }
// [[hook]] 0xDDA3F, a mounted gun's firing kick; true goes past it to 0xDDC23. 0xDDB92: the oscillator's step in XMM3
bool MountedKick(X86Context& ctx, uint8_t*) { return game::MountedKickSkip(ctx); }
void MountedKickStep(X86Context& ctx, uint8_t*) { game::MountedKickStep(ctx, ctx.xmm[3].f[0]); }
// [[hook]]s after keep = 1 - drag in the particle updates: 0xA7F16 (keep XMM7, seconds XMM3), 0xABD70 (XMM1, XMM0)
void ParticleDragTimed(X86Context& ctx, uint8_t*) { game::ParticleDrag(ctx.xmm[7].f[0], ctx.xmm[3].f[0]); }
void ParticleDragPooled(X86Context& ctx, uint8_t*) { game::ParticleDrag(ctx.xmm[1].f[0], ctx.xmm[0].f[0]); }
// [[hook]] 0x151234 in a weapon's fire routine 0x1506C0, before the refire ticks in XMM0 are added to its timer
void RefireTime(X86Context& ctx, uint8_t*) { game::RefireTime(ctx.xmm[0].f[0]); }

// an entity's animation event listener (vtable 0x1E84F0 slot 1): dialogue lines start here (opcode 0xCAF0)
X86_EXTERN_FUNC(__imp__sub_000CF780);
X86_FUNC(sub_000CF780) {
    game::TraceAnimEvent(ctx);
    __imp__sub_000CF780(ctx, base);
}

// the animation event scan (default 0x6FA30): MOHRS_ANIM_TRACE=<listener> logs one listener's channel each call
X86_EXTERN_FUNC(__imp__sub_0006FA30);
X86_FUNC(sub_0006FA30) {
    game::TraceAnim(ctx);
    __imp__sub_0006FA30(ctx, base);
}

// an animation channel's event table is set when it starts (0x6F9A0, from 0x6DAF0): MOHRS_ANIM_TRACE logs who starts it
X86_EXTERN_FUNC(__imp__sub_0006F9A0);
X86_FUNC(sub_0006F9A0) {
    game::TraceAnimStart(ctx);
    __imp__sub_0006F9A0(ctx, base);
}

// an animation graph's set-variable-by-name (0x6A120, vtable 0x1E2E20): MOHRS_GRAPH_TRACE logs each call
X86_EXTERN_FUNC(__imp__sub_0006A120);
X86_FUNC(sub_0006A120) {
    game::TraceGraphSet(ctx);
    __imp__sub_0006A120(ctx, base);
}

// a character's play-animation-state method (0xCE030, vtable 0x1E86A8): the same trace, with its caller
X86_EXTERN_FUNC(__imp__sub_000CE030);
X86_FUNC(sub_000CE030) {
    game::TraceGraphPlay(ctx);
    __imp__sub_000CE030(ctx, base);
}

// post a message to an object's script (0x4ADB0: target, message, argument, sender, flag): MOHRS_MESSAGE_TRACE
X86_EXTERN_FUNC(__imp__sub_0004ADB0);
X86_FUNC(sub_0004ADB0) {
    game::TraceMessage(ctx);
    __imp__sub_0004ADB0(ctx, base);
}

// the player's contact pair handler (player vtable 0x1E9100 slot +0x24): MOHRS_CONTACT_TRACE logs what it touches
X86_EXTERN_FUNC(__imp__sub_000DEFA0);
X86_FUNC(sub_000DEFA0) {
    game::TraceContact(ctx);
    __imp__sub_000DEFA0(ctx, base);
}

// the player's collision resolve (player vtable +0x28): MOHRS_STEP_TRACE logs each step near a point
X86_EXTERN_FUNC(__imp__sub_000D8260);
X86_FUNC(sub_000D8260) {
    game::TraceStep(ctx, true);
    __imp__sub_000D8260(ctx, base);
    game::TraceStep(ctx, false);
}

// the player's movement step (player vtable +0x1C): the same trace, tagged "move"
X86_EXTERN_FUNC(__imp__sub_000E0E50);
X86_FUNC(sub_000E0E50) {
    game::TraceMove(ctx, true);
    __imp__sub_000E0E50(ctx, base);
    game::TraceMove(ctx, false);
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

// FEng menu pointer adapter; actual text/sprite draws supply the clickable geometry (docs/menus.md).
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

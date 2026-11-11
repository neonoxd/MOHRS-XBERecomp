// Hooks for moh3rdc.xbe (co-op). A strong definition replaces the recompiled function at that address. The same
// game functions as in src/game/hooks.cpp, found by their bytes in this build of the game code.
#include "common/mouse_look.h"
#include "common/cheats.h"
#include "common/controller_routing.h"
#include "common/fov.h"
#include "common/frame_timing.h"
#include "common/options_menu.h"
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

X86_EXTERN_FUNC(__imp__sub_0015AA90);
X86_FUNC(sub_0015AA90) {
    game::UpdateControllerMode({0x23E5B1, 0x23A615, 0x23A616, 0x206F90, 0x206F91});
    input::SetFrontEnd(kernel::G<uint8_t>(0x23E5B1) != 0);
    input::SetSaveMenu(kernel::G<uint8_t>(0x23C959) != 0);  // the in-game menu's save byte (docs/input.md)
    __imp__sub_0015AA90(ctx, base);
}

// the main gameplay frame, default.xbe's 0xA4A90 (its "Gameloop Update" profile name): the cheats applied, the
// players being live there (src/common/cheats.h)
X86_EXTERN_FUNC(__imp__sub_00098A40);
X86_FUNC(sub_00098A40) {
    game::CheatFrame(ctx, game::kCoopCheats);
    __imp__sub_00098A40(ctx, base);
}

// the player's holder insert, default.xbe's 0xDF320 (src/common/cheats.h)
X86_EXTERN_FUNC(__imp__sub_000D3400);
X86_FUNC(sub_000D3400) {
    game::CheatRingBuilt(ctx.ecx);
    __imp__sub_000D3400(ctx, base);
}

// the EA stream library's packet take, default.xbe's 0x165390, under its stream's lock (src/common/stream_lock.h)
X86_EXTERN_FUNC(__imp__sub_0015E760);
X86_FUNC(sub_0015E760) { game::LockedStreamTake(ctx, base, __imp__sub_0015E760, game::kCoopStreams); }

// Weapon keys (src/common/weapon_select.h): the input manager's action query and the weapon select, default.xbe's
// 0xB83E0 and 0xD8720
X86_EXTERN_FUNC(__imp__sub_000ACC80);
X86_FUNC(sub_000ACC80) {
    uint32_t index = R32(ctx.esp + 4), action = kernel::G<uint16_t>(ctx.esp + 8);
    __imp__sub_000ACC80(ctx, base);
    game::WeaponSwitchQuery(ctx, index, action, game::kCoopWeapons);
}
X86_EXTERN_FUNC(__imp__sub_000CD820);
X86_FUNC(sub_000CD820) {
    game::WeaponSelect(ctx, game::kCoopWeapons);
    __imp__sub_000CD820(ctx, base);
}

// the script system's frame update, default.xbe's 0x4C0F0: at the console's 30 Hz in the 60 FPS and unlocked modes
X86_EXTERN_FUNC(__imp__sub_0004F220);
X86_FUNC(sub_0004F220) {
    if (game::ScriptFrame(ctx)) __imp__sub_0004F220(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}

// the script system's message queue function, default.xbe's 0x481F0 (src/common/frame_timing.h)
X86_EXTERN_FUNC(__imp__sub_0004B340);
X86_FUNC(sub_0004B340) { game::QueuePost(ctx, base, __imp__sub_0004B340, 0x24D974); }

// the entity manager's update, default.xbe's 0xFAB00: short steps carried over in the 60 FPS and unlocked modes
X86_EXTERN_FUNC(__imp__sub_000F35D0);
X86_FUNC(sub_000F35D0) {
    if (game::EntityStep(ctx)) __imp__sub_000F35D0(ctx, base);
    else kernel::Return(ctx, ctx.eax, 1);
}

// the player's view shake and recoil update, default.xbe's 0xD6700: at the console's 30 Hz in the 60 FPS and
// unlocked modes
X86_EXTERN_FUNC(__imp__sub_000CB440);
X86_FUNC(sub_000CB440) {
    if (game::ShakeFrame(ctx)) __imp__sub_000CB440(ctx, base);
    else kernel::Return(ctx, ctx.eax, 3);
}

// the game's vertex helper, stdcall (position, color): SetVertexDataColor(3) and
// SetVertexData4f(-1), one call each per vertex of every effect (38 callers); here the values go to the primitive
// directly (docs/performance.md)
X86_FUNC(sub_001776C0) {
    (void)base;
    uint32_t pos = kernel::R32(ctx.esp + 4), color = kernel::R32(ctx.esp + 8);
    d3d::ImmediateColor(3, kernel::R32(color));
    float v[4] = {kernel::G<float>(pos), kernel::G<float>(pos + 4), kernel::G<float>(pos + 8), kernel::G<float>(pos + 12)};
    d3d::ImmediateData(0, v);
    kernel::Return(ctx, ctx.eax, 2);
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
void MouseLook(X86Context& ctx, uint8_t*) {
    game::MouseLook(ctx, kPlayers, 0x266618, game::kCoopMouseLook);
    game::WeaponScope(ctx.esi, game::kCoopWeapons);
}
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

// The player's near plane pulled in while the view is wider than the original (src/common/fov.h, docs/fov.md):
// the frustum build 0x757A0 and the projection build 0x76BE0 (camera at [view + 0xE4]), default.xbe's 0x82E50/0x84290
X86_EXTERN_FUNC(__imp__sub_000757A0);
X86_FUNC(sub_000757A0) {
    game::PlayerNearPlane near(ctx.ecx, kPlayers, 0x1F54B8, 0x1230);
    __imp__sub_000757A0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00076BE0);
X86_FUNC(sub_00076BE0) {
    game::PlayerNearPlane near(R32(ctx.ecx + 0xE4), kPlayers, 0x1F54B8, 0x1230);
    __imp__sub_00076BE0(ctx, base);
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
void PopupTicks(X86Context& ctx, uint8_t*) { game::PopupTicks(ctx); }
void MessageTicks(X86Context& ctx, uint8_t*) { game::MessageTicks(ctx); }
// [[hook]]s in the soldier AI brain: goal timer ticks at 0x19056, threat memory decay at 0x21F58
void BrainTicks(X86Context& ctx, uint8_t*) { game::BrainTicks(ctx); }
void ThreatDecay(X86Context& ctx, uint8_t*) { game::ThreatDecay(ctx); }
void FloorRayWorld(X86Context& ctx, uint8_t*) { game::FloorRayWorld(ctx); }
void FloorRayRetry(X86Context& ctx, uint8_t*) {
    game::FloorRayRetry(ctx, {0x90, 0xE0, 0x50, 0x13DF00, 0x13BBE0, 0x13BD80, 0x6D290});
}
void ResetLoadingAnimation(X86Context&, uint8_t*) { game::ResetLoadingAnimation(); }
void LoadingAnimation(X86Context& ctx, uint8_t*) { game::LoadingAnimation(ctx); }
void ResetPopupAnimation(X86Context&, uint8_t*) { game::ResetPopupAnimation(); }
void PopupAnimation(X86Context& ctx, uint8_t*) { game::PopupAnimation(ctx); }
// [[hook]] 0x625B4, the animation event scan's loop wrap branch; true goes to its forward scan at 0x6254D
bool AnimEventFirstScan(X86Context& ctx, uint8_t*) { return game::AnimEventFirstScan(ctx); }
// [[hook]] 0xD75F5, a mounted gun's firing kick; true goes past it to 0xD7848. Its seat oscillator steps at 0xD774D
// (step at player +0xE7C), the shared one at 0xD77BC (step in XMM2)
bool MountedKick(X86Context& ctx, uint8_t*) { return game::MountedKickSkip(ctx); }
void MountedKickSeatStep(X86Context& ctx, uint8_t*) { game::MountedKickStep(ctx, kernel::G<float>(ctx.esi + 0xE7C)); }
void MountedKickStep(X86Context& ctx, uint8_t*) { game::MountedKickStep(ctx, ctx.xmm[2].f[0]); }
// [[hook]]s after keep = 1 - drag in the particle updates: 0x9C156 (keep XMM7, seconds XMM3), 0xA06E0 (XMM1, XMM0)
void ParticleDragTimed(X86Context& ctx, uint8_t*) { game::ParticleDrag(ctx.xmm[7].f[0], ctx.xmm[3].f[0]); }
void ParticleDragPooled(X86Context& ctx, uint8_t*) { game::ParticleDrag(ctx.xmm[1].f[0], ctx.xmm[0].f[0]); }
// [[hook]] 0x14A5B2 in a weapon's fire routine 0x149A90 (default.xbe's 0x151234), before the refire ticks in XMM0 are
// added to its timer
void RefireTime(X86Context& ctx, uint8_t*) { game::RefireTime(ctx.xmm[0].f[0]); }

// a weapon's state update, default.xbe's 0x151320 (thiscall (time, no-fire flag), ret 8): its time for RefireTime
X86_EXTERN_FUNC(__imp__sub_0014A6A0);
X86_FUNC(sub_0014A6A0) {
    game::WeaponUpdate(ctx);
    __imp__sub_0014A6A0(ctx, base);
    game::WeaponUpdateDone();
}

// a weapon's muzzle puff, fastcall (weapon): at most once per console frame above 30 FPS (docs/frame-timing.md)
X86_EXTERN_FUNC(__imp__sub_000DAE10);
X86_FUNC(sub_000DAE10) {
    if (game::MuzzlePuffFrame(ctx.ecx)) __imp__sub_000DAE10(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}

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

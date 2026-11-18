// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include "common/mouse_look.h"
#include "common/cheats.h"
#include "common/controller_routing.h"
#include "common/custom_movement.h"
#include "common/fov.h"
#include "common/frame_timing.h"
#include "common/keep_bodies.h"
#include "common/level_weapons.h"
#include "common/loop_seam.h"
#include "common/options_menu.h"
#include "common/pool_lock.h"
#include "common/quick_save.h"
#include "common/script_guard.h"
#include "common/menu_mouse.h"
#include "common/mission_timer.h"
#include "common/modern_aim.h"
#include "common/stream_lock.h"
#include "common/test_hooks.h"
#include "common/weapon_select.h"
#include "common/widescreen.h"
#include "common/mouse_keyboard.h"
#include <cstdlib>

#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

using kernel::R32;

// Publish the native shell/game mode before this frame's XInput reads (docs/input.md).
X86_EXTERN_FUNC(__imp__sub_001616C0);
X86_FUNC(sub_001616C0) {
    game::UpdateControllerMode({0x2463B1, 0x242435, 0x242436, 0x20EDB0, 0x20EDB1});
    input::SetFrontEnd(kernel::G<uint8_t>(0x2463B1) != 0);
    input::SetSaveMenu(kernel::G<uint8_t>(0x244779) != 0);  // the in-game menu's save byte (docs/input.md)
    __imp__sub_001616C0(ctx, base);
}

// the player's holder insert (thiscall player, weapon hash; ret 4): the ring a level's player setup builds, which
// Give All Weapons gives from (src/common/cheats.h)
// Weapons from other levels (src/common/level_weapons.h): XPhysicalAlloc, which the main heap's 48 MB block comes from
// at boot (0x10FFD0), and the animation bank's load, lookup and release
X86_EXTERN_FUNC(__imp__sub_001A60FA);
X86_FUNC(sub_001A60FA) { game::HeapAlloc(ctx, base, __imp__sub_001A60FA); }
X86_EXTERN_FUNC(__imp__sub_00068BD0);
X86_FUNC(sub_00068BD0) { game::LoadWeaponBank(ctx, base, __imp__sub_00068BD0); }
X86_EXTERN_FUNC(__imp__sub_00068CE0);
X86_FUNC(sub_00068CE0) { game::FindAnimation(ctx, base, __imp__sub_00068CE0); }
X86_EXTERN_FUNC(__imp__sub_00068BA0);
X86_FUNC(sub_00068BA0) { game::ReleaseWeaponBank(ctx, base, __imp__sub_00068BA0); }

X86_EXTERN_FUNC(__imp__sub_000DF320);
X86_FUNC(sub_000DF320) {
    game::CheatRingBuilt(ctx.ecx);
    __imp__sub_000DF320(ctx, base);
}

// the small-block pools' search and free, under one lock (src/common/pool_lock.h)
X86_EXTERN_FUNC(__imp__sub_00111BE0);
X86_FUNC(sub_00111BE0) { game::LockedPool(ctx, base, __imp__sub_00111BE0); }
X86_EXTERN_FUNC(__imp__sub_00111C30);
X86_FUNC(sub_00111C30) { game::LockedPool(ctx, base, __imp__sub_00111C30); }

// the EA stream library's packet take, under its stream's lock (src/common/stream_lock.h)
X86_EXTERN_FUNC(__imp__sub_00165390);
X86_FUNC(sub_00165390) { game::LockedStreamTake(ctx, base, __imp__sub_00165390, game::kDefaultStreams); }

// the script's native call opcode (src/common/script_guard.h): a native given a destroyed script object does
// not run
X86_EXTERN_FUNC(__imp__sub_000497D0);
X86_FUNC(sub_000497D0) {
    if (game::NativeOnDestroyed(ctx, game::kDefaultScripts)) return;
    // the player's script starting a switch a weapon key asked for (src/common/weapon_select.h)
    game::WeaponNative(game::RunningNative(game::kDefaultScripts), game::kDefaultWeapons);
    __imp__sub_000497D0(ctx, base);
}
// a slot lookup with no owner, which the native 0x3B060 makes when its second object is null: none found
// (src/common/script_guard.h)
X86_EXTERN_FUNC(__imp__sub_000D2D60);
X86_FUNC(sub_000D2D60) {
    if (!game::SlotLookupWithoutOwner(ctx)) __imp__sub_000D2D60(ctx, base);
}

// Weapon keys (src/common/weapon_select.h): the input manager's action query and the weapon select
X86_EXTERN_FUNC(__imp__sub_000B83E0);
X86_FUNC(sub_000B83E0) {
    uint32_t index = R32(ctx.esp + 4), action = kernel::G<uint16_t>(ctx.esp + 8);
    __imp__sub_000B83E0(ctx, base);
    game::WeaponSwitchQuery(ctx, index, action, game::kDefaultWeapons);
    game::MovementQuery(ctx, index, action);
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

// the main gameplay frame, called once a frame from the level's loop at 0x128B9, paused too: its entry is where a
// quick save is taken and put back (src/common/quick_save.h), the host's call chain being the same there every frame,
// and where the cheats are applied (src/common/cheats.h), the players being live there, and the level's time
// published for the overlay (src/common/mission_timer.h)
X86_EXTERN_FUNC(__imp__sub_000A4A90);
X86_FUNC(sub_000A4A90) {
    game::QuickSaveFrame(ctx, reinterpret_cast<const void*>(&sub_000A4A90));
    game::CheatFrame(ctx, game::kDefaultCheats);
    game::MissionTimerFrame(game::kDefaultMissionTimer);
    __imp__sub_000A4A90(ctx, base);
}

// the script system's frame update (docs/frame-timing.md): at the console's 30 Hz in the 60 FPS and unlocked modes
X86_EXTERN_FUNC(__imp__sub_0004C0F0);
X86_FUNC(sub_0004C0F0) {
    if (game::ScriptFrame(ctx)) __imp__sub_0004C0F0(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}

// the script system's message queue function (src/common/frame_timing.h): repeats between two script runs and posts
// to a full pool dropped
X86_EXTERN_FUNC(__imp__sub_000481F0);
X86_FUNC(sub_000481F0) { game::QueuePost(ctx, base, __imp__sub_000481F0, 0x255748); }

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

// an animation graph's update, thiscall (time), ret 4: Quick Reload's faster time for a reloading player's holder
// (src/common/cheats.h)
X86_EXTERN_FUNC(__imp__sub_00069BD0);
X86_FUNC(sub_00069BD0) {
    uint32_t arg = ctx.esp + 4;
    float time = kernel::G<float>(arg);
    kernel::G<float>(arg) = game::GraphTime(ctx.ecx, time, game::kDefaultCheats);
    __imp__sub_00069BD0(ctx, base);
    kernel::G<float>(arg) = time;
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
void MouseLook(X86Context& ctx, uint8_t*) {
    game::MouseLook(ctx, kPlayers, 0x271188, game::kDefaultMouseLook);
    game::WeaponScope(ctx.esi, game::kDefaultWeapons);
}
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

// the movie player (docs/widescreen.md): [[hook]] 0xFC372 sets its scales for the widened frame
void MovieScale(X86Context& ctx, uint8_t*) { game::MovieScale(ctx); }
X86_EXTERN_FUNC(__imp__sub_000FC170);
X86_FUNC(sub_000FC170) {
    __imp__sub_000FC170(ctx, base);
    game::MovieEnded();
}

// Keep Bodies (src/common/keep_bodies.h) and MOHRS_CORPSE_TRACE (src/common/test_hooks.h). kSoldiers: the first AI
// soldier record (next +0xC)
constexpr uint32_t kSoldiers = 0x25E9B0;
// an AI soldier's record destroy (record vtable +8, fastcall)
X86_EXTERN_FUNC(__imp__sub_0004F990);
X86_FUNC(sub_0004F990) {
    game::TraceSoldierRemoval(ctx);
    __imp__sub_0004F990(ctx, base);
}
// [[hook]] 0x4F9CD, the record destroy's model removal (esi the record); true goes past it to 0x4F9FD
bool KeepBodyModel(X86Context& ctx, uint8_t*) { return game::KeepBodyModel(ctx, game::kDefaultBodies); }
// [[hook]] 0xCE3FD, the model release's world removal (esi the model); true goes past it to 0xCE408
bool KeepBodyInWorld(X86Context& ctx, uint8_t*) { return game::KeepBodyInWorld(ctx, game::kDefaultBodies); }
// [[hook]] 0xF9560, the world's draw after its objects' loop (esi the world, ebp the view)
void DrawBodies(X86Context& ctx, uint8_t*) { game::DrawBodies(ctx, game::kDefaultBodies); }
// the AI controllers' projectile watch list removal (cdecl entry): passed over for a body's controller, which left
// the list when the body was kept
X86_EXTERN_FUNC(__imp__sub_00027D50);
X86_FUNC(sub_00027D50) {
    if (game::OnWatchList(R32(ctx.esp + 4), game::kDefaultBodies)) __imp__sub_00027D50(ctx, base);
    else kernel::Return(ctx, ctx.eax, 0);
}
// an object's removal from the world (thiscall world, object): MOHRS_CORPSE_TRACE
X86_EXTERN_FUNC(__imp__sub_000FA3E0);
X86_FUNC(sub_000FA3E0) {
    game::TraceWorldRemoval(ctx, kSoldiers, R32(ctx.esp + 4), "leaves the world");
    __imp__sub_000FA3E0(ctx, base);
}
// a model's free (thiscall factory, model), reset (fastcall model) and deactivate (model vtable +0x168, ecx the model):
// MOHRS_CORPSE_TRACE
X86_EXTERN_FUNC(__imp__sub_000CA7C0);
X86_FUNC(sub_000CA7C0) {
    game::TraceWorldRemoval(ctx, kSoldiers, R32(ctx.esp + 4), "is freed");
    __imp__sub_000CA7C0(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_000CFE20);
X86_FUNC(sub_000CFE20) {
    game::TraceWorldRemoval(ctx, kSoldiers, ctx.ecx, "is reset");
    __imp__sub_000CFE20(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_000E4CB0);
X86_FUNC(sub_000E4CB0) {
    game::TraceWorldRemoval(ctx, kSoldiers, ctx.ecx, "is deactivated");
    __imp__sub_000E4CB0(ctx, base);
}
// the soldier model's fade (vtable +0x1B0: thiscall model, seconds; ret 4)
X86_EXTERN_FUNC(__imp__sub_000E57C0);
X86_FUNC(sub_000E57C0) {
    uint32_t model = ctx.ecx;
    __imp__sub_000E57C0(ctx, base);
    game::KeepBodyFade(model, game::kDefaultBodies);
}
// the deferred-delete pass, each game loop update
X86_EXTERN_FUNC(__imp__sub_000F8680);
X86_FUNC(sub_000F8680) {
    game::KeepBodiesPass(ctx, game::kDefaultBodies);
    __imp__sub_000F8680(ctx, base);
}
// a game object pool's creation (thiscall pool, size, count, flags; ret 0xC): the soldier models get spares. Should
// the larger block not be had, the pool is made with the game's count.
X86_EXTERN_FUNC(__imp__sub_000D1E90);
X86_FUNC(sub_000D1E90) {
    uint32_t esp = ctx.esp, count = R32(esp + 8);
    uint32_t more = game::ModelPoolCount(ctx.ecx, count, game::kDefaultBodies);
    uint32_t pool = ctx.ecx;
    kernel::W32(esp + 8, more);
    __imp__sub_000D1E90(ctx, base);
    if (ctx.eax || more == count) return;
    LOGR("keep bodies: no room for %u more soldier models", more - count);
    ctx.esp = esp;
    ctx.ecx = pool;
    kernel::W32(esp + 8, count);
    __imp__sub_000D1E90(ctx, base);
}
// a level's AI records start (lists, count and limit)
X86_EXTERN_FUNC(__imp__sub_0004E690);
X86_FUNC(sub_0004E690) {
    game::ForgetBodies();
    __imp__sub_0004E690(ctx, base);
}
// the level's shutdown ("Gameloop_Shutdown"): the bodies are freed while the world and the pools are still there
X86_EXTERN_FUNC(__imp__sub_000A3380);
X86_FUNC(sub_000A3380) {
    game::BodiesShutdown(ctx, game::kDefaultBodies, true);
    __imp__sub_000A3380(ctx, base);
    game::BodiesShutdown(ctx, game::kDefaultBodies, false);
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
// [[hook]] 0x8599E in the light update 0x85990: its keyframe ticks
void LightTicks(X86Context& ctx, uint8_t*) { game::LightTicks(ctx); }
// [[hook]] 0xD6DFA in the footstep and landing sounds 0xD6C70: the late landing sounds' countdown ticks
void LandingTicks(X86Context& ctx, uint8_t*) { game::LandingTicks(ctx); }
// [[hook]] 0x6FDA9 in the clip channel update 0x6FCE0, after its sample: the first-person weapon's loop seams
void LoopSeam(X86Context& ctx, uint8_t*) { game::LoopSeam(ctx, game::kDefaultLoopSeam); }
void FloorRayWorld(X86Context& ctx, uint8_t*) { game::FloorRayWorld(ctx); }
// [[hook]] 0xDB7C4 in the player update 0xDB530, after its ground test (bl): Custom Movement's rules
void MovementGround(X86Context& ctx, uint8_t*) {
    game::MovementGround(ctx, (ctx.ebx & 0xFF) != 0, game::kDefaultMovement);
}
// [[hook]]s in the player update 0xDB530 (src/common/modern_aim.h): ModernAim passes over the aim's ground test
// (0xDBB48) and its eye raise and lower (0xDBDEE, 0xDBE75), ModernAimWalk the aim test before the walk code
// (0xDCF41); the aim block's velocity part (0xDC1D2) and the join after the walk code (0xDD4A2)
bool ModernAim(X86Context& ctx, uint8_t*) { return game::ModernAim(ctx, game::kDefaultAim); }
bool ModernAimWalk(X86Context& ctx, uint8_t*) { return game::ModernAimWalk(ctx, game::kDefaultAim); }
bool ModernAimHold(X86Context& ctx, uint8_t*) { return game::ModernAimHold(ctx, game::kDefaultAim); }
void ModernAimWalked(X86Context& ctx, uint8_t*) { game::ModernAimWalked(ctx, game::kDefaultAim); }
// [[hook]]s in the multiplayer bot update 0x65A80: its AI update test at 0x65A95, the AI update's time at 0x65ABA
void BotThink(X86Context& ctx, uint8_t*) { game::BotThink(ctx, 0x270140); }
void BotThinkTime(X86Context& ctx, uint8_t*) { game::BotThinkTime(ctx); }
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

// post a message to an object's script (0x4ADB0: target, message, argument, sender, flag; true when a handler took
// it): MOHRS_MESSAGE_TRACE, and a weapon key's switch message no handler took (src/common/weapon_select.h)
X86_EXTERN_FUNC(__imp__sub_0004ADB0);
X86_FUNC(sub_0004ADB0) {
    game::TraceMessage(ctx);
    uint32_t target = R32(ctx.esp + 4), message = R32(ctx.esp + 8);
    __imp__sub_0004ADB0(ctx, base);
    game::TraceMessageTaken(target, message, (ctx.eax & 0xFF) != 0);
    game::WeaponSwitchPosted(target, message, (ctx.eax & 0xFF) != 0, game::kDefaultWeapons);
}

// the player's contact pair handler (player vtable 0x1E9100 slot +0x24): MOHRS_CONTACT_TRACE logs what it touches;
// Custom Movement takes the floors it goes up (src/common/custom_movement.h)
X86_EXTERN_FUNC(__imp__sub_000DEFA0);
X86_FUNC(sub_000DEFA0) {
    game::TraceContact(ctx);
    game::MovementContact(ctx);
    __imp__sub_000DEFA0(ctx, base);
}

// the player's collision resolve (player vtable +0x28): MOHRS_STEP_TRACE logs each step near a point
X86_EXTERN_FUNC(__imp__sub_000D8260);
X86_FUNC(sub_000D8260) {
    game::TraceStep(ctx, true);
    __imp__sub_000D8260(ctx, base);
    game::TraceStep(ctx, false);
}

// the player's update (player vtable 0x1E9100 slot +0x10, thiscall (ticks), ret 4): Custom Movement's velocity
// (src/common/custom_movement.h)
X86_EXTERN_FUNC(__imp__sub_000DB530);
X86_FUNC(sub_000DB530) {
    uint32_t player = ctx.ecx;
    game::MovementUpdateBegin(ctx, game::kDefaultMovement);
    __imp__sub_000DB530(ctx, base);
    game::MovementUpdateEnd(player, game::kDefaultMovement);
}

// the player's movement step (player vtable +0x1C): Custom Movement gives back the step's gravity; the trace of
// MOHRS_STEP_TRACE, tagged "move"
X86_EXTERN_FUNC(__imp__sub_000E0E50);
X86_FUNC(sub_000E0E50) {
    game::MovementStep(ctx, game::kDefaultMovement);
    game::TraceMove(ctx, true);
    __imp__sub_000E0E50(ctx, base);
    game::TraceMove(ctx, false);
}

// an explosion's apply to one object in reach (vtable 0x1F2880 slot +4, thiscall (target), ret 4): with Custom
// Movement a player's own blast does them no damage and pushes them (src/common/custom_movement.h)
X86_EXTERN_FUNC(__imp__sub_0014B020);
X86_FUNC(sub_0014B020) { game::MovementBlast(ctx, base, __imp__sub_0014B020, game::kDefaultMovement); }
// the player's view shake start (thiscall (amplitude, ramp up, ramp down, hold)): lighter for one's own blast
X86_EXTERN_FUNC(__imp__sub_000D6670);
X86_FUNC(sub_000D6670) {
    game::MovementShake(ctx);
    __imp__sub_000D6670(ctx, base);
}

// PC Options in the options menus (src/common/options_menu.h): SetText 0x138A40, RunScript 0x138330, the menu sound
// 0x123C10, a button enabled 0x138B30 and disabled 0x138B50
constexpr game::MenuCalls kMenuCalls = {0x138A40, 0x138330, 0x123C10, 0x138B30, 0x138B50};
// the open archive's file lookup, which the shell's package loader (0x122BC0) asks for each .fng
X86_EXTERN_FUNC(__imp__sub_001132E0);
X86_FUNC(sub_001132E0) { game::MenuFile(ctx, base, __imp__sub_001132E0); }
// the screen handler factory, by package name
X86_EXTERN_FUNC(__imp__sub_00120F00);
X86_FUNC(sub_00120F00) { game::MenuScreen(ctx, base, __imp__sub_00120F00); }
// the HUD options screen's event handler (vtable 0x1ED40C)
X86_EXTERN_FUNC(__imp__sub_0012A620);
X86_FUNC(sub_0012A620) { game::MenuEvent(ctx, base, __imp__sub_0012A620, kMenuCalls); }
// the controller options screen's event handler (vtable 0x1ED4B4): the Modern Aiming row
X86_EXTERN_FUNC(__imp__sub_00127FD0);
X86_FUNC(sub_00127FD0) { game::ControllerMenuEvent(ctx, base, __imp__sub_00127FD0, kMenuCalls); }
// the stats screens' fill, with the game state pointer at 0x2447D8: the end-of-level summary's mission time
X86_EXTERN_FUNC(__imp__sub_00136D00);
X86_FUNC(sub_00136D00) { game::MenuStats(ctx, base, __imp__sub_00136D00, kMenuCalls, 0x2447D8); }

// FEng menu pointer adapter; actual text/sprite draws supply the clickable geometry (docs/menus.md).
constexpr game::MenuMouseCalls kMouseMenu = {
    0x96900, 0x93260, 0x958D0, 0x162B20, 0x88630,
    0x1FC680, 0x1FC684, 0x246450, 0x246454, 0x246424, 0x246420, 0x271188};
X86_EXTERN_FUNC(__imp__sub_00095220);
X86_FUNC(sub_00095220) {
    game::MenuKeys(ctx, kMenuCalls);
    game::MenuMouseUpdate(ctx, base, __imp__sub_00095220, kMouseMenu);
}
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

// the sound event lookup and play (thiscall: sound, event hash; ret 8; 0 or less when nothing played):
// MOHRS_SOUND_TRACE logs the weapon events' (docs/debugging.md)
X86_EXTERN_FUNC(__imp__sub_00108370);
X86_FUNC(sub_00108370) {
    static const bool trace = std::getenv("MOHRS_SOUND_TRACE") != nullptr;
    uint32_t hash = R32(ctx.esp + 8);
    __imp__sub_00108370(ctx, base);
    if (trace) LOGR("sound trace: event %08X -> %d", hash, int(ctx.eax));
}

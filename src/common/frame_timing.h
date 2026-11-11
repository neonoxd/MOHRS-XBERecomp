#pragma once
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

// Called after the frame end's integer-to-float conversion, before the game's PAL/NTSC scale is applied.
void FrameDelta(X86Context& ctx);
// The main loop's two integer consumers: mission elapsed ticks, and front-end/UI elapsed ticks. MenuTicks also
// serves the in-game menu update (0x11DD20), which feeds the same FEng manager its dt * 20 units.
void MissionTicks(X86Context& ctx);
void MenuTicks(X86Context& ctx);
// The save/profile popup's FEng update truncates dt * 20 the same way; it keeps its own fraction.
void PopupTicks(X86Context& ctx);
// Entity manager's delayed messages already store float timers; stop truncating their delta.
void MessageTicks(X86Context& ctx);
// Soldier AI brain update (0x197C0, co-op 0x19050), after cvttss2si eax,[esp+4]: its goal timers (0x18D40 ->
// 0x243C0) add whole ticks and re-score a goal once they pass its period (60, 10 or 1 ticks). Below one tick a frame
// the truncation gave 0 and NPCs stopped re-thinking; ecx is the brain, which keeps its own fraction.
void BrainTicks(X86Context& ctx);
// Threat memory decay (0x223A0, co-op 0x21F50) after its 0.01 load into xmm2: one step per brain update, a 30 FPS
// frame on the console; scaled by the update's ticks, its ignored stack argument.
void ThreatDecay(X86Context& ctx);
// The player's end-of-step floor check (default 0xDA7D0, co-op 0xD104A) casts a ray straight down from 0.39 above the
// player; a miss (result 1) zeroes the velocity and puts the player back at the previous step's end. A seam in the
// collision mesh (1_1's lip at x -41.88) misses too, and short steps keep landing in it: the player is stuck.
// FloorRayWorld keeps the collision world (EDI before the call); FloorRayRetry, at cmp edi, 1, casts again a little to
// each side before a miss stands.
// The script system's frame update (default 0x4C0F0, co-op 0x4F220; cdecl, its time at [esp+4]) runs scripts,
// messages, condition polls counted in frames (0x2E0C0) and frame queues (0x511B0); the console ran it at 30 Hz.
// ScriptFrame is false on frames between two console frames, else it sets the time since the last run.
bool ScriptFrame(X86Context& ctx);
// The script system's message queue function (default 0x481F0, co-op 0x4B340; cdecl (target, handler entry,
// argument, word, argument object, sender, message)) takes an entry from a 256-entry pool (free list at default
// 0x255748, co-op 0x24D974) with no check; the queue drains in the script update. A multiplayer player posts 300 to
// its script (4 handlers, 4 entries) every frame it stands (0xCC9A0): at about 1,700 FPS, 64 frames between two
// drains filled the pool and the next post read a null entry (0x48253). QueuePost drops a repeated post between two
// script runs (RepeatedPosts) and any post while the pool is empty, through the function's own path for a gone
// target (+0x24 == -1), which also releases a pooled argument object.
using QueueFunction = void (*)(X86Context&, uint8_t*);
void QueuePost(X86Context& ctx, uint8_t* base, QueueFunction original, uint32_t free_list);
// A multiplayer bot's update (default.xbe 0x65A80, thiscall (time); moh3rdc.xbe has no bots) runs its AI when the
// frame counter (0x270140) and the bot's number (+0x1938) differ in parity, with twice the frame's time: at 30 FPS
// once each 4 ticks, alternating bots. Its checks count AI updates (0x62380, counter +0x1D40): a bot not getting
// closer to its goal jumps after 8, picks a new goal after 30 and goes into recovery after 60; recovery (0x5BD80)
// posts the bot 100 damage (message 0x28, a suicide) after 150 more, 10 s on the console. Every 15 updates the
// below-the-floor check (0x62260) counts too, with the same suicide past 30. At 1,700 FPS the AI ran 800 times a
// second and recovery ended in a suicide 0.2 s in: 705 in a minute of a 7-bot match, none with this.
// BotThink, at xor ecx,[0x270140] (0x65A95), sets ECX so the parity test passes once per 4 ticks of that
// bot's time (BotThinkClock); BotThinkTime, after the doubled time is in XMM0 (0x65ABA), sets the time since that
// bot's last AI update.
void BotThink(X86Context& ctx, uint32_t frame_counter);
void BotThinkTime(X86Context& ctx);
// The player's view shake and recoil update (default 0xD6700, co-op 0xCB440; thiscall (yaw offset*, pitch offset*,
// time), ret 0xC), called each frame on foot and on a mounted gun. Each call adds a random jolt to the view offsets at
// +0x284/+0x288, which persist, and steps a mounted gun's firing shake by a fixed amount: more frames, more jolts
// and a faster build-up. ShakeFrame is false between two console frames of that player, else it sets the time since
// its last run.
bool ShakeFrame(X86Context& ctx);
// A mounted gun's firing kick in the player update (default 0xDDA3F..0xDDC23, co-op 0xD75F5..0xD7848; player in ESI,
// time at [EBP+8]) moves the gun, or the seat of a gun that has one (1_11's boat turret), along its axis by
// 0.006 or 0.02 times an oscillator, added to its present place each frame, then steps the oscillator (+-1, one a tick)
// in time. At 30 FPS the oscillator flips each frame and the gun only twitches; at more frames it ramps and the
// additions pile up into a swing many times as large. MountedKickSkip is true (skip the block) between two console
// frames of that player; MountedKickStep, after the oscillator's step * time, sets XMM0 to step * the run's time.
bool MountedKickSkip(const X86Context& ctx);
void MountedKickStep(X86Context& ctx, float step);
// Particle velocity damping (docs/frame-timing.md): both particle updates (default 0xA7ED0 / 0xABD10, co-op 0x9C110 /
// 0xA0690) compute keep = 1 - drag once and apply v = v * keep + acceleration * seconds to every particle each frame,
// so more frames damp harder and particles barely leave their emitter. Raises keep to the frame's console frames
// (seconds * 30), so a 30 FPS frame keeps the console's factor.
void ParticleDrag(float& keep, float seconds);
// A weapon's muzzle puff (default 0xE2CF0, co-op 0xDAE10, fastcall weapon) spawns a 0.54 s effect every frame while
// the weapon's flash is on. Above 30 FPS the puffs can fill the 128-emitter pool (0xB0120), where an effect that needs
// ten free slots silently fails to spawn (1_1's floor-fire burst, a level-script spawn, failed at 60 FPS). True at most
// once per console frame (1/30 s) per weapon.
bool MuzzlePuffFrame(uint32_t weapon);
// A weapon's state update (default 0x151320, co-op 0x14A6A0; thiscall (time, no-fire flag), ret 8), once a frame for
// every weapon (players, mounted guns, NPCs), either counts its refire timer (+0x300, co-op +0x310) down by the time
// or, at zero or below, fires (0x1506C0, co-op 0x149A90), which adds the refire ticks to the timer: never both in one
// frame. The firing frame's time is not counted, so a shot takes refire + one frame: refire + 2 ticks at 30 FPS,
// about refire unlocked (up to twice the console's rate), and the extra bullets fill the 64-bullet pool, where a
// shot silently makes no bullet, sound or ammo use. WeaponUpdate keeps the update's time (WeaponUpdateDone after it);
// RefireTime, before the fire routine adds the refire ticks in XMM0, adds the rest of a console frame.
void WeaponUpdate(const X86Context& ctx);
void WeaponUpdateDone();
void RefireTime(float& refire);
// The entity manager's update (default 0xFAB00, co-op 0xF35D0; thiscall, its time at [esp+4], ret 4) only runs a
// zero-length physics step and skips the post-step updates when a step is under 1/64 tick. Slow motion (0xA4F30 scales
// the frame's time) at high frame rates goes below that, so nothing moved (1_1's ending). EntityStep is false while
// such short steps are carried over, else it sets the carried time plus this frame's.
bool EntityStep(X86Context& ctx);
struct FloorRaySite {
    uint32_t input, segment, hit;                     // the caller's ray input, segment and hit record, from ESP
    uint32_t make_segment, init_hit, free_hit, flag;  // 0x144C30, 0x142910, 0x142AB0, 0x7A8A0 in default.xbe
};
void FloorRayWorld(const X86Context& ctx);
void FloorRayRetry(X86Context& ctx, const FloorRaySite& site);
// Loading-screen medal/star phase: the original counter advances once per draw, wrapping after 33 draws.
void ResetLoadingAnimation();
void LoadingAnimation(X86Context& ctx);
// Profile checking and save prompts use the same animation in a separate memory-card popup renderer.
void ResetPopupAnimation();
void PopupAnimation(X86Context& ctx);
// Animation event scan, at its loop wrap branch: ebx the previous animation frame, edx the current one. True (go to
// the forward scan) when they are equal, which only gets here on an animation's first scan; the wrap branch would
// fire all its events. 30 FPS never has equal frames there, 60 FPS and faster do.
bool AnimEventFirstScan(const X86Context& ctx);
// Optional MOHRS_TIMING diagnostics: actual player tick frequency and time advanced.
void PlayerTick(float delta);
// At frame end, also covers the front end, loading and pause paths.
void TimingFrame(float delta, uint32_t players, uint32_t state);

}  // namespace game

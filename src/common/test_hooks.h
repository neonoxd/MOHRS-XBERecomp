#pragma once
// Hook bodies both images share (default.xbe in src/game, moh3rdc.xbe in src/coop); each image wraps them at its
// own addresses of the same game functions.
#include <cstdint>

#include "core/input.h"
#include "cpu/x86_context.h"

namespace game {

// Development: MOHRS_START_LEVEL=<mission>_<level> (e.g. 1_2) starts that level instead of the first one. The boot
// picks the level with the game state's SetCurrentLevel(mission, level): 1, 1 on a fresh profile (legal intro, then
// the level), 1, 16 for the logo and the front end. Called at that function's entry; changes only its first call,
// and only the fresh profile's 1, 1, so the menus and later level changes behave as on the console.
void PickStartLevel(X86Context& ctx);

// Movie skipping, after the game's config GetString(key, buffer, size) returned found: game.cfg names each movie,
// and "none" there means no movie. Answers "none" for legal_movie and legal_intro_movie (EA logo, the opening Pearl
// Harbor movie) with skip_intros, and for each level's intro_movie and outro_movie with skip_cutscenes. The bonus
// movies picked in the menus play.
void SkipMovie(uint32_t key, uint32_t buffer, uint32_t size);

// 60 fps: the frame's end (default.xbe 0x13A90) waits until two vertical blanks have passed since the last frame, then
// runs the game one fixed tick per blank passed. With fps60 a blank passed counts as two at both of its waiting
// compares, so a frame is shown each blank and the game runs one tick a frame: the same speed, twice the frames, as
// the PS2 version's 60 fps patch (sltiu v0, v0, 2 -> 1).
// With unlocked_fps even zero blanks pass the wait; frame_timing.cpp supplies elapsed ticks to every caller.
void FrameWait(uint32_t& blanks);

// Development: MOHRS_MENU_PROBE="<seconds>:<press>,..." presses controller buttons on port 0 inside the game, timed
// from its first controller poll, for menu tests without the PC's keyboard: up down left right start back a b, or
// capture, which saves the next frame under XBE_CAPTURE_DIR. Each press is held a tenth of a second.
void ProbePad(input::Pad& pad);

// Development: MOHRS_EVENT_TRACE logs each animation event an entity's listener handles (default.xbe 0xCF780, thiscall
// with the event record: opcode at +2, argument at +4), with the caller and the listener.
void TraceAnimEvent(const X86Context& ctx);
// Development: MOHRS_CONTACT_TRACE logs each object the player's contact pair handler (0xDEFA0, thiscall player, pair
// at [esp+4]) sees, once per object and again when its position changes.
void TraceContact(const X86Context& ctx);
// Development: MOHRS_ANIM_TRACE=<event table>[,<event table>...] (hex, the event records MOHRS_EVENT_TRACE logs lie in
// them) logs each animation channel playing one (event scan 0x6FA30, fastcall channel): time +0x38, scanned time
// +0xE0, +0x3C..+0x48, and the channel's first words and events when it starts one.
void TraceAnim(const X86Context& ctx);
void TraceAnimStart(const X86Context& ctx);
void TraceGraphSet(const X86Context& ctx);
void TraceGraphPlay(const X86Context& ctx);
// Development: MOHRS_NATIVE_TRACE=find logs the script owner calling native 0x303A0 (play animation state);
// MOHRS_NATIVE_TRACE=<owner hex> logs every native that owner's script calls (0x497F3: index, function, frame time).
void TraceNative(const X86Context& ctx);
// Development: MOHRS_MESSAGE_TRACE logs each message posted through 0x4ADB0 with its target, sender and caller
// (MOHRS_MESSAGE_TRACE=0x28: only that message).
void TraceMessage(const X86Context& ctx);  // 0xCE030 (thiscall character, state, arg)  // 0x6A120 (thiscall graph, name, value, ...): MOHRS_GRAPH_TRACE  // 0x6F9A0 (thiscall channel, count, table): the guest call chain
// Development: MOHRS_STEP_TRACE="x,y,radius" logs the player's collision resolve (0xD8260, fastcall player) within
// radius of x,y: position before and after, step dt (+0x10C8), velocities (+0x230, +0x240), result and flags.
void TraceStep(X86Context& ctx, bool before);
void TraceMove(X86Context& ctx, bool before);  // the movement step 0xE0E50 (thiscall player, step dt at [esp+4])

}  // namespace game

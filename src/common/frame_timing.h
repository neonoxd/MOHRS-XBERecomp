#pragma once
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

// Called after the frame end's integer-to-float conversion, before the game's PAL/NTSC scale is applied.
void FrameDelta(X86Context& ctx);
// The main loop's two integer consumers: mission elapsed ticks, and front-end/UI elapsed ticks.
void MissionTicks(X86Context& ctx);
void MenuTicks(X86Context& ctx);
// Entity manager's delayed messages already store float timers; stop truncating their delta.
void MessageTicks(X86Context& ctx);
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

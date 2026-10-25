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
// Optional MOHRS_TIMING diagnostics: actual player tick frequency and time advanced.
void PlayerTick(float delta);
// At frame end, also covers the front end, loading and pause paths.
void TimingFrame(float delta, uint32_t players, uint32_t state);

}  // namespace game

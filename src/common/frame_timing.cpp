#include "common/frame_timing.h"

#include <chrono>
#include <cstdlib>

#include "common/frame_clock.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace game {
namespace {
using Clock = std::chrono::steady_clock;
thread_local FrameClock clock;
thread_local TickRemainder mission, menu;
thread_local LoadingAnimationClock loading, popup;
bool Diagnostics() {
    static bool on = std::getenv("MOHRS_TIMING") != nullptr;
    return on;
}
thread_local uint32_t player_calls = 0, frames = 0;
thread_local double player_ticks = 0, frame_ticks = 0;
thread_local Clock::time_point report = Clock::now();

void Animate(X86Context& ctx, LoadingAnimationClock& animation, const char* name) {
    if (!Opt().unlocked_fps && !Opt().fps60) return;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    bool first = !animation.started;
    ctx.xmm[0].f[0] = animation.Sample(ns);
    if (first && Diagnostics()) LOGR("timing: %s stars use a 1.1 second cycle (30 phase steps/s)", name);
}
}  // namespace

void FrameDelta(X86Context& ctx) {
    if (!Opt().unlocked_fps) {
        clock = {};
        mission = menu = {};
        return;
    }
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    ctx.xmm[0].f[0] = clock.Advance(ns);
}

void MissionTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.eax = mission.Advance(ctx.xmm[0].f[0]);
}

void MenuTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.eax = menu.Advance(ctx.xmm[0].f[0]);
}

void MessageTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.xmm[3].f[0] = ctx.xmm[2].f[0];
}

// equal frames reach the wrap branch only when the scan's started flag (+0xE4) is clear: the original 30 FPS steps
// always move the first scan a frame on, so this is the forward scan the console ran there
bool AnimEventFirstScan(const X86Context& ctx) { return ctx.ebx == ctx.edx; }

void ResetLoadingAnimation() { loading = {}; }

void LoadingAnimation(X86Context& ctx) {
    Animate(ctx, loading, "loading");
}

void ResetPopupAnimation() { popup = {}; }
void PopupAnimation(X86Context& ctx) { Animate(ctx, popup, "profile/save popup"); }

void PlayerTick(float delta) {
    if (!Diagnostics()) return;
    player_calls++;
    player_ticks += delta;
}

void TimingFrame(float delta, uint32_t players, uint32_t state) {
    if (!Diagnostics()) return;
    frames++;
    frame_ticks += delta;
    auto now = Clock::now();
    double seconds = std::chrono::duration<double>(now - report).count();
    if (seconds < 1) return;
    uint32_t p = kernel::R32(players), save = kernel::R32(state);
    LOGR("timing: %.1f frames/s, %.3f frame ticks/s; %.1f player updates/s, %.3f player ticks/s; "
         "mission ticks %u; player %08X xyz %.3f %.3f %.3f", frames / seconds, frame_ticks / seconds,
         player_calls / seconds, player_ticks / seconds, save ? kernel::R32(save + 0x31760) : 0, p,
         p ? kernel::G<float>(p + 0x80) : 0, p ? kernel::G<float>(p + 0x84) : 0,
         p ? kernel::G<float>(p + 0x88) : 0);
    report = now;
    frames = player_calls = 0;
    frame_ticks = player_ticks = 0;
}

}  // namespace game

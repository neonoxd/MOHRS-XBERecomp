#include "common/mission_timer.h"

#include <atomic>
#include <chrono>
#include <cstdio>

#include "kernel/kernel.h"

namespace game {

namespace {

using Clock = std::chrono::steady_clock;
constexpr int64_t kStaleNs = 250'000'000;

std::atomic<uint32_t> g_ticks{0};
std::atomic<int64_t> g_at{0};  // steady clock ns of the last publish, 0 none

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

}  // namespace

uint32_t MissionTicksNow(uint32_t state) {
    uint32_t game = kernel::R32(state);
    return game ? kernel::R32(game + kMissionTicks) : 0;
}

std::string FormatMissionTime(uint32_t ticks) {
    uint32_t seconds = ticks / 60, ms = ticks % 60 * 1000 / 60;
    char text[24];
    if (seconds >= 3600)
        std::snprintf(text, sizeof(text), "%u:%02u:%02u.%03u", seconds / 3600, seconds / 60 % 60, seconds % 60, ms);
    else
        std::snprintf(text, sizeof(text), "%u:%02u.%03u", seconds / 60, seconds % 60, ms);
    return text;
}

void MissionTimerFrame(const MissionTimerLayout& layout) {
    if (kernel::G<uint8_t>(layout.front_end) != 0 || !kernel::R32(layout.state)) {
        g_at.store(0, std::memory_order_relaxed);
        return;
    }
    g_ticks.store(MissionTicksNow(layout.state), std::memory_order_relaxed);
    g_at.store(Now(), std::memory_order_release);
}

bool MissionTimerTicks(uint32_t& ticks) {
    int64_t at = g_at.load(std::memory_order_acquire);
    if (!at || Now() - at > kStaleNs) return false;
    ticks = g_ticks.load(std::memory_order_relaxed);
    return true;
}

}  // namespace game

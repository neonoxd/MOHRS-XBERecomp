#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game {

// The game's time unit is one 60 Hz tick. Keep fractions until an API explicitly needs whole ticks.
struct FrameClock {
    int64_t previous = 0;
    bool started = false;

    float Advance(int64_t now_ns) {
        float ticks = started ? float(double(now_ns - previous) * 60.0 / 1e9) : 1.0f;
        previous = now_ns;
        started = true;
        // A load, debugger stop or suspended window must not advance the world by seconds in one update.
        return std::clamp(ticks, 0.000001f, 6.0f);
    }
};

struct TickRemainder {
    double fraction = 0;

    uint32_t Advance(float ticks) {
        double total = fraction + std::max(0.0f, ticks);
        uint32_t whole = uint32_t(total);
        fraction = total - whole;
        return whole;
    }
};

// Loading-screen stars have a 33-draw cycle on the original 30 FPS renderer (2*pi/33 radians per draw).
// Sample wall time directly so extra draws do not advance the animation, including while loading stalls.
struct LoadingAnimationClock {
    int64_t epoch = 0;
    bool started = false;

    float Sample(int64_t now_ns) {
        if (!started) { epoch = now_ns; started = true; }
        double seconds = std::max(0.0, double(now_ns - epoch) / 1e9);
        return float(std::fmod(seconds * 30.0, 33.0));
    }
};

}  // namespace game

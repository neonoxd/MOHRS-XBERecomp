#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

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

// The same, with one fraction per owner: soldier AI brains add whole ticks to their goal timers on each of their own
// updates, so a brain that skips frames keeps its own remainder. Owners come and go with the level; the map is
// dropped when it grows past any level's population, which costs at most a fraction of a tick per brain.
struct KeyedTickRemainder {
    std::unordered_map<uint32_t, double> fraction;

    uint32_t Advance(uint32_t key, float ticks) {
        if (fraction.size() > 4096) fraction.clear();
        double& f = fraction[key];
        double total = f + std::max(0.0f, ticks);
        uint32_t whole = uint32_t(total);
        f = total - whole;
        return whole;
    }
};

// A multiplayer bot's AI update: at 30 FPS it ran on every other frame, alternating bots by the parity of their
// number, so once each 4 ticks with 4 ticks of time. Due is true once per 4 ticks of the bot's own time, odd bots
// half a period after even ones; time is then the ticks since its last AI update.
struct BotThinkClock {
    static constexpr double kPeriod = 4;
    double total = 0, ran = 0;  // ticks plus the bot's phase
    int64_t periods = 0;
    bool started = false;

    bool Due(float ticks, bool odd, float& time) {
        if (!started) {
            total = odd ? kPeriod / 2 : 0;
            ran = 0;  // the first update gets a full period, as on the console
            periods = 0;
            started = true;
        }
        total += std::max(0.0f, ticks);
        auto period = int64_t(total / kPeriod);
        if (period <= periods) return false;
        periods = period;
        time = float(total - ran);
        ran = total;
        return true;
    }
};

// A post to the script system's message queue: its queue function's stack arguments (target, handler entry,
// argument, its word, argument object, sender, message), the two 16-bit ones masked.
struct QueuedPost {
    uint32_t target, handler, argument, word, object, sender, message;
    bool operator==(const QueuedPost&) const = default;
};

// The script system drains its message queue once per console frame (ScriptFrame), and code that runs every frame
// posts every frame: many copies per drain at high frame rates. Repeat is true for a post, on a later frame, equal to
// one of the posts its target got on the last frame it got any: it has had nothing else since, and on the console a
// drain would have come in between. Posts within one frame always stand, as do all after a different one.
struct RepeatedPosts {
    struct Burst {
        uint64_t frame = 0;
        std::vector<QueuedPost> posts;
    };
    std::unordered_map<uint32_t, Burst> targets;
    uint64_t frame = 0;

    void NextFrame() { frame++; }
    void Drained() { targets.clear(); }
    bool Repeat(const QueuedPost& post) {
        auto [it, fresh] = targets.try_emplace(post.target);
        Burst& burst = it->second;
        if (fresh) burst.frame = frame;
        else if (burst.frame != frame) {
            if (std::find(burst.posts.begin(), burst.posts.end(), post) != burst.posts.end()) return true;
            burst.frame = frame;
            burst.posts.clear();
        }
        burst.posts.push_back(post);
        return false;
    }
};

// A decay the console applied once per 30 FPS frame (two ticks), scaled to the ticks this update covers.
inline float PerConsoleFrame(float amount, float ticks) { return amount * std::max(0.0f, ticks) * 0.5f; }

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

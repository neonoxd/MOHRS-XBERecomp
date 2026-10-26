// Standalone: tools\dev.bat clang++ -std=c++20 -Isrc tests/frame_clock_test.cpp -o re/frame_clock_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "common/frame_clock.h"

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    for (int fps : {30, 60, 120, 144, 240, 1000}) {
        game::FrameClock clock;
        game::TickRemainder timer;
        Check(clock.Advance(0) == 1, "first frame must use one tick");
        double sum = 0;
        uint32_t whole = 0;
        for (int frame = 1; frame <= fps * 10; frame++) {
            float delta = clock.Advance(int64_t(frame) * 1000000000 / fps);
            sum += delta;
            whole += timer.Advance(delta);
        }
        Check(std::abs(sum - 600) < 0.001, "game time must match wall time at every frame rate");
        Check(std::abs(double(whole) + timer.fraction - sum) < 0.00001, "integer timers must retain all fractions");
        Check(whole >= 599 && whole <= 600, "integer timers must keep advancing above 60 FPS");
        Check(timer.fraction >= 0 && timer.fraction < 1, "fraction must stay normalized");
        std::printf("%d FPS: %.6f ticks, %u integer ticks + %.6f remainder\n", fps, sum, whole, timer.fraction);
    }
    game::FrameClock clock;
    clock.Advance(0);
    Check(clock.Advance(5000000000) == 6, "long stalls must be capped at the game's six tick limit");
    Check(clock.Advance(5008333333) < 0.51f, "a stall must not leave a catch-up backlog");
    Check(clock.Advance(5008333333) > 0, "zero-duration frames must stay positive");
    game::TickRemainder timer;
    Check(timer.Advance(0.5f) == 0 && timer.Advance(0.5f) == 1, "two half ticks must produce one whole tick");
    Check(timer.Advance(0) == 0 && timer.Advance(-1) == 0, "paused timers must not advance");
    for (int fps : {30, 60, 120, 144, 240, 1000}) {
        game::LoadingAnimationClock animation;
        Check(animation.Sample(123000000) == 0, "loading animation must start at phase zero");
        for (int frame = 1; frame <= fps * 5; frame++) {
            int64_t elapsed = int64_t(frame) * 1000000000 / fps;
            float phase = animation.Sample(123000000 + elapsed);
            Check(phase >= 0 && phase < 33, "loading animation phase must stay in its cycle");
        }
        Check(std::abs(animation.Sample(123000000 + 5000000000LL) - 18) < 0.00001f,
              "five seconds must leave phase 18 regardless of how many frames were drawn");
        Check(std::abs(animation.Sample(123000000 + 5500000000LL)) < 0.00001f,
              "five loading animation cycles must take 5.5 seconds");
        for (int duplicate = 0; duplicate < 100; duplicate++)
            Check(std::abs(animation.Sample(123000000 + 6050000000LL) - 16.5f) < 0.00001f,
                  "repeated draws at the same time must leave the animation phase unchanged");
        animation = {};
        Check(animation.Sample(9000000000LL) == 0, "a new loading screen must reset the phase");
    }
    std::puts("frame clock checks passed");
}

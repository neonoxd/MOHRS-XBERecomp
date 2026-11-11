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
    // Soldier AI goal timers (0x243C0): counter += whole ticks each brain update; past the period the goal is
    // re-scored and the counter restarts. The console's 30 FPS gave 2 a frame, the 60 FPS mode 1.
    // fps 0: the 60 FPS mode's exact one-tick frames; 30: the console's two-tick frames; others: unlocked
    auto goal_scores = [](int fps, int period, bool truncate) {
        game::FrameClock clock;
        game::KeyedTickRemainder brains;
        clock.Advance(0);
        int counter = 0, scores = 0;
        int frames = fps == 0 ? 600 : fps * 10;
        for (int frame = 1; frame <= frames; frame++) {
            float delta = fps == 0 ? 1.0f : fps == 30 ? 2.0f : clock.Advance(int64_t(frame) * 1000000000 / fps);
            counter += truncate ? int(delta) : int(brains.Advance(0x12345678, delta));
            if (counter > period) { counter = 0; scores++; }
        }
        return scores;
    };
    for (int period : {1, 10, 60}) {
        int at60 = goal_scores(0, period, true);
        Check(goal_scores(0, period, false) == at60, "the 60 FPS mode must be unchanged");
        Check(goal_scores(144, period, true) == 0, "the original truncation must be what stalls goals above 60 FPS");
        for (int fps : {60, 120, 144, 240, 1000}) {
            int scores = goal_scores(fps, period, false);
            Check(std::abs(scores - at60) <= 1, "goal re-scoring must keep the 60 FPS rate at any frame rate");
        }
        std::printf("goal period %d: %d re-scores in 10 s at 30 FPS, %d at 60 and above\n", period,
                    goal_scores(30, period, true), at60);
    }
    // FEng menus (0x11DD20 in-game, 0xF68A0 save/profile popup): (int)(dt * 20) units, 1,200 a second at 30/60 FPS.
    // Truncation gives 8 per frame at 144 FPS (4% slow) and nothing at all above 1,200 FPS.
    for (int fps : {60, 144, 1000, 3000, 5000}) {
        game::FrameClock clock;
        game::TickRemainder menu;
        clock.Advance(0);
        uint32_t units = 0, truncated = 0;
        for (int frame = 1; frame <= fps * 10; frame++) {
            float delta = clock.Advance(int64_t(frame) * 1000000000 / fps);
            units += menu.Advance(delta * 20);
            truncated += uint32_t(delta * 20);
        }
        Check(units >= 11999 && units <= 12000, "menus must get 1,200 FEng units a second at any frame rate");
        if (fps >= 3000) Check(truncated == 0, "the original truncation must be what freezes menus above 1,200 FPS");
        std::printf("%d FPS: menus get %u FEng units in 10 s (truncated: %u)\n", fps, units, truncated);
    }
    game::KeyedTickRemainder brains;
    Check(brains.Advance(1, 0.75f) == 0 && brains.Advance(2, 0.75f) == 0, "brains keep separate fractions");
    Check(brains.Advance(1, 0.5f) == 1 && brains.Advance(2, 0.1f) == 0, "each brain must carry its own remainder");
    Check(brains.Advance(3, -1) == 0, "a negative delta must not advance a brain");
    // Threat memory (0x223A0): a priority of 2.0 decayed by 0.01 per 30 FPS frame lasts 200 frames + 1.
    for (int fps : {30, 60, 120, 144, 240, 1000}) {
        game::FrameClock clock;
        clock.Advance(0);
        float threat = 2.0f;
        int frame = 0;
        while (threat >= 0 && frame < fps * 60) {
            frame++;
            float delta = fps == 30 ? 2.0f : clock.Advance(int64_t(frame) * 1000000000 / fps);
            threat -= game::PerConsoleFrame(0.01f, delta);
        }
        double seconds = double(frame) / fps;
        Check(std::abs(seconds - 201.0 / 30) < 0.05, "threat memory must last as long as at 30 FPS");
        std::printf("%d FPS: threat memory lasts %.3f s\n", fps, seconds);
    }
    Check(game::PerConsoleFrame(0.01f, 2) == 0.01f, "a 30 FPS frame must decay by the original step");
    Check(game::PerConsoleFrame(0.01f, -1) == 0, "a negative delta must not decay");
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
    for (int fps : {30, 60, 144, 1700}) {
        // a multiplayer bot's AI: 150 updates in 10 seconds at any frame rate, odd bots half a period later
        game::BotThinkClock even, odd;
        float ticks = 60.0f / fps, time = 0, even_time = 0, odd_time = 0;
        int even_runs = 0, odd_runs = 0, first_even = -1, first_odd = -1;
        for (int frame = 0; frame < fps * 10; frame++) {
            if (even.Due(ticks, false, time)) {
                even_runs++;
                even_time += time;
                if (first_even < 0) first_even = frame;
            }
            if (odd.Due(ticks, true, time)) {
                odd_runs++;
                odd_time += time;
                if (first_odd < 0) first_odd = frame;
                Check(std::abs(time - 4) <= ticks + 0.001f, "a bot's AI update must get the 4 ticks since its last");
            }
        }
        Check(even_runs >= 149 && even_runs <= 150 && odd_runs >= 149 && odd_runs <= 150,
              "bots must run their AI 15 times a second at every frame rate");
        Check(std::abs(even_time - 4 * even_runs) < 0.5f, "the AI updates' time must add up to their period");
        Check(first_odd < first_even, "odd bots must run half a period apart from even ones");
        std::printf("%d FPS: bot AI %d/%d updates in 10 s\n", fps, even_runs, odd_runs);
    }
    {
        // the multiplayer player's stand message: 300 to its script, one entry per handler, every frame
        game::RepeatedPosts posts;
        auto stand = [](uint32_t handler) { return game::QueuedPost{0x826890B0, handler, 0, 0, 0, 0, 300}; };
        auto turn = game::QueuedPost{0x826890B0, 0x82659C00, 0, 0, 0, 0, 299};
        auto other = game::QueuedPost{0x82700000, 0x82659B70, 0, 0, 0, 0, 300};
        int kept = 0;
        for (int frame = 0; frame < 100; frame++) {
            posts.NextFrame();
            for (uint32_t h : {0x82659B70u, 0x82659B90u, 0x82659BB0u, 0x82659BD0u}) kept += !posts.Repeat(stand(h));
        }
        Check(kept == 4, "a post repeated on later frames must be queued once per drain");
        Check(posts.Repeat(stand(0x82659B70)), "a repeat stays dropped within the drain");
        posts.NextFrame();
        Check(!posts.Repeat(other), "another target's post must stand");
        Check(!posts.Repeat(turn), "a different post must stand");
        posts.NextFrame();
        Check(!posts.Repeat(stand(0x82659B70)), "a post after a different one must stand, keeping the order");
        Check(!posts.Repeat(stand(0x82659B90)), "posts within one frame must stand");
        Check(!posts.Repeat(stand(0x82659B90)), "a post repeated within one frame must stand");
        posts.NextFrame();
        Check(posts.Repeat(stand(0x82659B70)), "the next frame's repeat must be dropped");
        Check(!posts.Repeat({0x826890B0, 0x82659B70, 0, 0, 0x8302F198, 0, 300}),
              "a post with another argument must stand");
        posts.Drained();
        posts.NextFrame();
        Check(!posts.Repeat(stand(0x82659B70)), "the first post after a drain must stand");
    }
    std::puts("frame clock checks passed");
}

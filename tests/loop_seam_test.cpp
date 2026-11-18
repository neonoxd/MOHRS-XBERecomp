// Host test: loop seam blending (src/common/loop_seam.h) on a clip played the way the channel update (0x6FCE0) and
// the decoders (0x18B640 -> 0x19B7E0) play it: time += dt, wrapped by the length (keys - 1) / 30, keys interpolated
// per component.
// tools\dev.bat clang++ -std=c++20 -Isrc -Isdk/runtime tests/loop_seam_test.cpp -o re/loop_seam_test.exe
#include "common/loop_seam.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

using Joint = std::vector<float>;  // kPoseFloats

// a clip's key i: a smooth cycle, and its last key either key 0 again (seamless) or off by `seam`
static Joint Key(int i, int keys, float seam) {
    Joint j(game::kPoseFloats, 0);
    double phase = 2 * 3.14159265358979 * i / (keys - 1);
    for (size_t k = 0; k < j.size(); k++) j[k] = float(std::sin(phase + k));
    if (i == keys - 1)
        for (float& v : j) v += seam;
    return j;
}

// the decoders: clamp to the last key, lerp to the next
static Joint Sample(float t, int keys, float seam) {
    float f = std::min(t * 30, float(keys - 1));
    int i = int(f);
    Joint a = Key(i, keys, seam), out = a;
    if (f != float(i) && i + 1 < keys) {
        Joint b = Key(i + 1, keys, seam);
        for (size_t k = 0; k < out.size(); k++) out[k] = a[k] + (b[k] - a[k]) * (f - i);
    }
    return out;
}

static float Distance(const Joint& a, const Joint& b) {
    float d = 0;
    for (size_t k = 0; k < a.size(); k++) d = std::max(d, std::fabs(a[k] - b[k]));
    return d;
}

struct Run {
    std::vector<float> times;
    std::vector<Joint> poses;
};

// plays the clip for 3 s at fps (30 and 60 as the console's exact two- and one-tick frames), blending as LoopSeam
static Run Play(int fps, int keys, float seam, bool smooth) {
    float length = (keys - 1) * game::kKeySeconds, dt = 1.0f / fps, t = 0;
    Joint end = Sample(length, keys, seam), first = Sample(game::kKeySeconds, keys, seam);
    bool window = false;
    Run run;
    for (int frame = 0; frame < fps * 3; frame++) {
        float before = t;
        t += dt;
        while (t > length) t -= length;
        bool wrap = t < before;
        if (wrap) window = game::SeamWeight(t) >= 0;
        else if (window && game::SeamWeight(t) < 0) window = false;
        Joint pose = Sample(t, keys, seam);
        if (smooth && window) game::SeamJoint(end.data(), first.data(), game::SeamWeight(t), pose.data());
        run.times.push_back(t);
        run.poses.push_back(pose);
    }
    return run;
}

int main() {
    const int keys = 25;  // a 0.8 s cycle
    // a clip whose last key is key 0 plays exactly as before, at every rate
    for (int fps : {30, 60, 144, 240}) {
        Run a = Play(fps, keys, 0, false), b = Play(fps, keys, 0, true);
        for (size_t i = 0; i < a.poses.size(); i++)
            Check(Distance(a.poses[i], b.poses[i]) < 1e-5f, "a seamless loop must not change");
    }
    // console frames (exact multiples of 1/30 s; the hook is off in the 30 FPS mode, where float sums of 1/30 can
    // wrap to a hair above 0): after a wrap the time is a whole key gap in, past the window
    for (int key = 1; key < keys; key++) Check(game::SeamWeight(key / 30.0f) < 0, "a key moment is never blended");
    // with a seam: the largest frame-to-frame change at high rates, against the clip's normal largest step
    for (int fps : {60, 144, 240, 1000}) {
        Run before = Play(fps, keys, 0.5f, false), after = Play(fps, keys, 0.5f, true);
        Run clean = Play(fps, keys, 0, false);
        auto biggest = [](const Run& r) {
            float d = 0;
            for (size_t i = 1; i < r.poses.size(); i++) d = std::max(d, Distance(r.poses[i - 1], r.poses[i]));
            return d;
        };
        float normal = biggest(clean), jump = biggest(before), smoothed = biggest(after);
        Check(jump > normal * 2, "the test clip must have a visible seam");
        // the last key to key 1 over one key gap is about two normal steps plus the seam, spread over the gap
        Check(smoothed < normal * 2 + 0.5f * 30.0f / fps + 1e-4f, "the seam must be spread over the first gap");
        Check(smoothed < jump, "smoothing must make the biggest change smaller");
        // across each wrap the pose carries on from the last key: no bigger a change than a frame's share of the gap
        int wraps = 0;
        for (size_t i = 1; i < after.times.size(); i++) {
            if (after.times[i] >= after.times[i - 1]) continue;
            wraps++;
            float share = Distance(Sample((keys - 1) * game::kKeySeconds, keys, 0.5f),
                                   Sample(game::kKeySeconds, keys, 0.5f)) * 30.0f / fps;
            Check(Distance(after.poses[i - 1], after.poses[i]) < normal + share + 1e-4f, "a wrap must not jump");
        }
        Check(wraps >= 3, "three seconds must loop the 0.8 s clip at least three times");
        std::printf("%d FPS: biggest change %.3f with a seam, %.3f smoothed, %.3f for a seamless loop\n", fps, jump,
                    smoothed, normal);
    }
    // the first gap after a wrap starts at the last key and reaches key 1 at its end
    {
        Joint end = Sample((keys - 1) * game::kKeySeconds, keys, 0.5f), first = Sample(game::kKeySeconds, keys, 0.5f);
        Joint out(game::kPoseFloats);
        game::SeamJoint(end.data(), first.data(), 0, out.data());
        Check(Distance(out, end) < 1e-6f, "the gap must start at the last key");
        game::SeamJoint(end.data(), first.data(), 1, out.data());
        Check(Distance(out, first) < 1e-6f, "the gap must end at key 1");
    }
    Check(game::SeamWeight(0.5f / 30) > 0.49f && game::SeamWeight(0.5f / 30) < 0.51f, "half a gap is weight 0.5");
    Check(game::SeamWeight(1.0f / 30) < 0, "a full key gap is past the window");
    Check(game::SeamWeight(-0.001f) < 0, "a negative time is never in the window");
    float q[game::kPoseFloats] = {0, 0, 0, 1, 0, 0, 0, 1}, r[game::kPoseFloats] = {0, 0, 0, 1, 0, 0, 0.7071068f, 0.7071068f};
    Check(std::fabs(game::JointAngle(q, r) - 90) < 0.01f, "a quarter turn is 90 degrees");
    std::printf("loop seam tests passed\n");
    return 0;
}

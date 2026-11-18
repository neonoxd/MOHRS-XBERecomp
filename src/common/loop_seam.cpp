#include "common/loop_seam.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {
namespace {

constexpr uint32_t kClip = 0x34, kElapsed = 0x38, kTime = 0x3C, kDuration = 0x40, kLength = 0x44;
constexpr uint32_t kHolderGraph = 0x54;
constexpr uint32_t kMaxJoints = 128, kMaxPlayers = 4, kMaxDepth = 32;
constexpr double kRestartWindow = 0.1;  // seconds between a channel finishing and the graph's next one starting

bool Pointer(uint32_t address) { return address >= 0x10000; }

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Channel {
    double seen = 0;
    bool window = false;  // in the first gap of a repeat, blending from end to first
    float last = 0;       // the clip time of the last update in the window
    std::vector<float> end, first;
};
// the last channel of a graph that ran out, and its pose then: the clip's last key (the time held at its length)
struct Finish {
    uint32_t clip = 0, channel = 0;
    double at = -1;
    std::vector<float> pose;
};
std::unordered_map<uint32_t, Channel> g_channels;
std::unordered_map<uint32_t, Finish> g_finishes;  // by graph
uint32_t g_traced = 0;

// a player's holder (the first-person weapon) or the player's own body, or null
const char* Owner(uint32_t graph, const LoopSeamLayout& l) {
    uint32_t count = std::min(R32(l.player_count), kMaxPlayers);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t player = R32(l.weapons.players + i * 4);
        if (!Pointer(player)) continue;
        uint32_t holder = R32(player + l.weapons.holder);
        if (Pointer(holder) && holder + kHolderGraph == graph) return "weapon";
        if (player + kHolderGraph == graph) return "player";
    }
    return nullptr;
}

// the skeleton's bind pose holds the joint count (+8), as 0x189850 reads it
uint32_t Joints(uint32_t skeleton) {
    uint32_t bones = Pointer(skeleton) ? R32(skeleton + 4) : 0;
    uint32_t bind = Pointer(bones) ? R32(bones + 0xC) : 0;
    uint32_t n = Pointer(bind) ? R32(bind + 8) : 0;
    return n <= kMaxJoints ? n : 0;
}

bool Masked(uint32_t mask, uint32_t joint) {
    return !mask || (R32(mask + (joint >> 5) * 4) & (1u << (joint & 31)));
}

void Read(uint32_t pose, uint32_t joints, std::vector<float>& out) {
    out.resize(size_t(joints) * kPoseFloats);
    for (size_t i = 0; i < out.size(); i++) out[i] = G<float>(pose + uint32_t(i) * 4);
}

uint32_t Bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void Prune(double now) {
    if (g_channels.size() <= 1024) return;
    for (auto it = g_channels.begin(); it != g_channels.end();)
        it = now - it->second.seen > 2 ? g_channels.erase(it) : std::next(it);
}

}  // namespace

void LoopSeam(X86Context& ctx, const LoopSeamLayout& l) {
    static const bool original = [] {
        const char* v = std::getenv("MOHRS_LOOP_SEAM_ORIGINAL");
        return v && *v && *v != '0';
    }();
    // MOHRS_LOOP_SEAM_TRACE=1: the first-person weapon's clips; =all: every graph's, tagged by owner;
    // =frames[:<seconds>]: every channel update for 3 s, from that long (default 10) after the first one
    static const char* trace_text = std::getenv("MOHRS_LOOP_SEAM_TRACE");
    static const int trace = [] {
        const char* v = trace_text;
        if (!v || !*v || *v == '0') return 0;
        if (std::strcmp(v, "all") == 0) return 2;
        if (std::strncmp(v, "frames", 6) == 0) return 3;
        return 1;
    }();
    bool smooth = !original && (Opt().unlocked_fps || Opt().fps60);
    if (!smooth && !trace) return;
    uint32_t channel = ctx.esi, skeleton = ctx.ebx, pose = ctx.ebp, mask = ctx.edi;
    uint32_t depth = R32(l.graph_depth);
    if (trace == 3) {
        static const double after = [] {
            const char* colon = std::strchr(trace_text, ':');
            double s = colon ? std::atof(colon + 1) : 10;
            return s > 0 ? s : 10;
        }();
        static double first = -1;
        static uint32_t lines = 0;
        double now = Now();
        if (first < 0) first = now;
        double at = now - first;
        if (at >= after && at < after + 3 && lines < 40000) {
            lines++;
            uint32_t top = depth <= kMaxDepth ? R32(l.graph_stack + depth * 4) : 0;
            const char* who = depth == 0 ? "none" : Pointer(top) && Owner(top, l) ? Owner(top, l) : "other";
            float length = G<float>(channel + kLength);
            LOGR("seam frame %.4f depth %u graph %08X %s channel %08X clip %08X keys %d before %.4f time %.4f "
                 "elapsed %.4f duration %.4f", at, depth, top, who, channel, R32(channel + kClip),
                 int(std::lround(length / kKeySeconds)) + 1, G<float>(channel + kTime), G<float>(ctx.esp + 0x10),
                 G<float>(channel + kElapsed), G<float>(channel + kDuration));
        }
    }
    if (!Pointer(pose) || depth == 0 || depth > kMaxDepth) return;
    uint32_t graph = R32(l.graph_stack + depth * 4);
    const char* owner = Owner(graph, l);
    if (!owner && trace < 2) return;
    if (!owner) owner = "other";
    bool weapon = owner[0] == 'w';
    if (!weapon && trace < 2) return;
    if (!weapon) smooth = false;

    float before = G<float>(channel + kTime), t = G<float>(ctx.esp + 0x10);
    float length = G<float>(channel + kLength), duration = G<float>(channel + kDuration);
    bool finished = duration != 0 && G<float>(channel + kElapsed) >= duration - 0.001f;
    uint32_t clip = R32(channel + kClip);
    uint32_t joints = Joints(skeleton);
    if (!joints) return;
    double now = Now();
    Prune(now);
    // a channel's first update: its time was 0 and its elapsed time is this update's (the graph reuses the memory of
    // a finished channel for the next, so the address says nothing)
    float elapsed = G<float>(channel + kElapsed);
    bool fresh = before == 0 && t > 0 && std::fabs(elapsed - t) < 1e-5f;
    Channel& c = g_channels[channel];
    if (fresh) c = {};
    c.seen = now;
    Finish& f = g_finishes[graph];

    // a wrap of a looping clip starts again from its own last key; a new channel the graph starts as soon as another
    // ran out (the next clip of a chain, or the same clip again) from the pose the finished one ended on
    const char* kind = nullptr;
    if (!fresh && t < before - 1e-6f && !finished) {
        kind = "wrap";
    } else if (fresh && now - f.at < kRestartWindow && f.pose.size() == size_t(joints) * kPoseFloats) {
        kind = f.clip == clip ? "restart" : "next clip";
    } else if (trace && fresh && g_traced < (trace == 2 ? 3000u : 400u)) {
        g_traced++;
        LOGR("loop seam: %s graph %08X: new channel, clip %08X, %d keys, from %.4f s, duration %.3f s; last finish "
             "in this graph: clip %08X %.3f s ago", owner, graph, clip, int(std::lround(length / kKeySeconds)) + 1,
             before, duration, f.clip, f.at < 0 ? -1.0 : now - f.at);
    }
    if (finished) {
        f.clip = clip;
        f.channel = channel;
        f.at = now;
        Read(pose, joints, f.pose);  // its last frame, at (or a hair before) its last key
    }

    if (kind && owner[0] == 'o') {
        if (g_traced < (trace == 2 ? 3000u : 400u)) {
            g_traced++;
            LOGR("loop seam: other graph %08X: %s, clip %08X, %d keys, time %.4f s", graph, kind, clip,
                 int(std::lround(length / kKeySeconds)) + 1, t);
        }
        kind = nullptr;
    }
    if (kind) {
        float w = SeamWeight(t);
        bool wrap = kind[0] == 'w';
        c.window = false;
        if ((w >= 0 || trace) && (!wrap || length >= 2 * kKeySeconds - 1e-6f)) {
            auto sample = [&](float seconds) {
                ctx.ecx = clip;
                kernel::CallGuest(ctx, l.sample, {skeleton, Bits(seconds), pose, mask});
            };
            std::vector<float> key0;
            if (wrap) {
                sample(length);
                Read(pose, joints, c.end);
            } else {
                c.end = f.pose;
            }
            sample(kKeySeconds);
            Read(pose, joints, c.first);
            if (trace) {
                sample(0);
                Read(pose, joints, key0);
            }
            sample(t);  // the pose and the decoders' place as the game left them
            if (trace && g_traced < (trace == 2 ? 3000u : 400u)) {
                g_traced++;
                float seam = 0, step = 0;
                uint32_t at = 0;
                for (uint32_t j = 1; j < joints; j++) {
                    if (!Masked(mask, j)) continue;
                    float a = JointAngle(&c.end[j * kPoseFloats], &key0[j * kPoseFloats]);
                    if (a > seam) seam = a, at = j;
                    step = std::max(step, JointAngle(&key0[j * kPoseFloats], &c.first[j * kPoseFloats]));
                }
                LOGR("loop seam: %s graph %08X: %s, clip %08X (from %08X), %d keys, time %.4f s (%s), end pose to "
                     "key 0 %.2f deg (joint %u), key 0 to 1 %.2f deg", owner, graph, kind, clip, wrap ? clip : f.clip,
                     int(std::lround(length / kKeySeconds)) + 1, t,
                     !smooth ? "left as is" : w >= 0 ? "smoothed" : "past the first gap", seam, at, step);
            }
            c.window = smooth && w >= 0;
        }
    } else if (c.window && (t < c.last || SeamWeight(t) < 0)) {
        c.window = false;
    }
    if (!c.window || c.end.size() != size_t(joints) * kPoseFloats || c.first.size() != c.end.size()) return;
    c.last = t;
    float w = SeamWeight(t);
    float out[kPoseFloats];
    for (uint32_t j = 1; j < joints; j++) {  // joint 0 is the root, which the root motion takes next
        if (!Masked(mask, j)) continue;
        SeamJoint(&c.end[j * kPoseFloats], &c.first[j * kPoseFloats], w, out);
        for (uint32_t k = 0; k < kPoseFloats; k++) G<float>(pose + (j * kPoseFloats + k) * 4) = out[k];
    }
}

}  // namespace game

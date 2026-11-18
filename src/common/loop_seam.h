#pragma once
// Smooth loop seams in the first-person weapon's animations (docs/frame-timing.md, "Animation loop seams").
//
// A clip of N keys is 30 a second and loops every (N - 1) / 30 s (length 0x68A60 / co-op 0x5B430): its time wraps by
// the length, so the last key and key 0 share a moment. At 30 FPS every frame lands on a key, and a repeat shows the
// last key and then key 1: key 0 is passed over. The decoders interpolate between keys (0x19B7E0, per component), so
// above 30 FPS the first gap after a wrap runs from key 0 to key 1, and a clip whose last key differs from key 0 jumps
// there; at 30 FPS that jump hid among the 30 Hz steps. LoopSeam makes the first gap of a repeat run from the last key
// to key 1, which is what the console shows at its frames and changes nothing for a clip whose last key is key 0.
// Most first-person clips do not loop in place: a channel plays its clip a random number of times (often once) and
// the graph starts the next channel as soon as it runs out, the next clip of a chain or the same one again. At 30 FPS
// the finished clip's last key is followed by the new clip's key 1 the same way, so a new channel the graph starts
// within 0.1 s of another finishing runs its first gap from the pose the finished channel ended on to its key 1.
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "common/weapon_select.h"
#include "cpu/x86_context.h"

namespace game {

struct LoopSeamLayout {
    uint32_t graph_stack, graph_depth;  // the graph update's stack (0x69BD0 pushes its graph): top at stack[depth]
    uint32_t sample;                    // thiscall clip (skeleton, seconds, pose, joint mask), ret 0x10
    uint32_t player_count;
    const WeaponLayout& weapons;        // the players and their holder (the first-person weapon, graph at +0x54)
};
inline constexpr LoopSeamLayout kDefaultLoopSeam{0x269CE4, 0x269D68, 0x68B40, 0x1FC658, kDefaultWeapons};
inline constexpr LoopSeamLayout kCoopLoopSeam{0x25EE4C, 0x25EED0, 0x5B510, 0x1F54B8, kCoopWeapons};

// a pose holds 12 floats a joint: scale (+0x00), quaternion (+0x10), translation (+0x20)
inline constexpr size_t kPoseFloats = 12;
inline constexpr float kKeySeconds = 1.0f / 30;

// The weight into the first gap after a wrap at a clip time of t seconds, or a negative number when t is past it
// (a frame of a key or longer, as at 30 FPS, leaves the clip as it is).
inline float SeamWeight(float t) {
    float w = t / kKeySeconds;
    return t >= 0 && w < 0.999f ? w : -1;
}

// One joint between the last key and key 1, linear per component as the decoders interpolate keys.
inline void SeamJoint(const float* end, const float* first, float w, float* out) {
    for (size_t k = 0; k < kPoseFloats; k++) out[k] = end[k] + (first[k] - end[k]) * w;
}

// The angle in degrees between two joints' rotations (quaternions at +4 floats), for MOHRS_LOOP_SEAM_TRACE.
inline float JointAngle(const float* a, const float* b) {
    double dot = 0, na = 0, nb = 0;
    for (int k = 4; k < 8; k++) {
        dot += double(a[k]) * b[k];
        na += double(a[k]) * a[k];
        nb += double(b[k]) * b[k];
    }
    if (na <= 0 || nb <= 0) return 0;
    double c = std::fabs(dot) / std::sqrt(na * nb);
    return float(2 * std::acos(c > 1 ? 1 : c) * 180 / 3.14159265358979);
}

// [[hook]] right after a clip channel's sample (default.xbe 0x6FDA9 in 0x6FCE0, moh3rdc.xbe 0x627E9 in 0x62720):
// ESI the channel (+0x34 clip, +0x38 elapsed, +0x3C time before this update, +0x40 duration or 0, +0x44 length),
// EBX the skeleton, EBP the pose, EDI the joint mask (null: all), [ESP+0x10] the time after it. In the 60 FPS and
// unlocked modes, for the players' first-person weapons; MOHRS_LOOP_SEAM_ORIGINAL=1 leaves the clips as they are,
// MOHRS_LOOP_SEAM_TRACE=1 logs each wrap and clip change with the size of the seam (=all: every graph's, by owner).
void LoopSeam(X86Context& ctx, const LoopSeamLayout& layout);

}  // namespace game

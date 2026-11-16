#pragma once
// Quake's player movement rules (QuakeWorld's pmove.c), for src/common/custom_movement.h. Host only, in Quake's units
// (inches, seconds): tests/movement_rules_test.cpp runs them on their own.
//
// One frame (Move): the jump, ground friction, acceleration toward the wished direction (on the ground) or air
// acceleration with the wished speed capped at 30, then gravity. Collision is the caller's.
#include <algorithm>
#include <cmath>

namespace game::movement {

struct Vec {
    float x = 0, y = 0, z = 0;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline float Length2D(Vec a) { return std::sqrt(a.x * a.x + a.y * a.y); }

struct Rules {
    float gravity;         // sv_gravity
    float stopspeed;       // sv_stopspeed
    float maxspeed;        // sv_maxspeed
    float accelerate;      // sv_accelerate, on the ground and (in QuakeWorld) in the air
    float friction;        // sv_friction
    float forwardspeed;    // cl_forwardspeed, running
    float backspeed;       // cl_backspeed, running
    float sidespeed;       // cl_sidespeed
    float jump;            // added to the z speed
    float ground_z_speed;  // above this z speed the player is off the ground
    float duck;            // the move's multiplier crouched (Quake has none: the game's own)
    float maxvelocity;     // sv_maxvelocity, each component
};

inline constexpr float kAirSpeedCap = 30;

// QuakeWorld's movevars and the client's run speeds (cl_forwardspeed 400 with always run, cl_sidespeed 350)
inline constexpr Rules kQuake{800, 100, 320, 10, 4, 400, 400, 350, 270, 180, 0.25f, 2000};

struct Input {
    float forward = 0, side = 0;  // -1 to 1, positive forward and right
    bool jump = false;
    bool crouched = false;
    bool can_jump = true;   // the game would let the player jump (not crouched, on foot)
    bool auto_hop = false;  // a held jump jumps again on landing, as if pressed anew
};

struct State {
    Vec velocity;           // inches per second
    bool old_jump = false;  // the jump held since it was last used
    bool on_ground = false;
};

struct Frame {
    Vec move;  // the velocity to move by this frame (half the frame's gravity taken, so a jump's height keeps at any rate)
    bool jumped = false;
};

// PM_Friction (no edge friction: the caller has no trace)
inline void Friction(State& s, const Rules& r, float dt) {
    float speed = Length(s.velocity);
    if (speed < 0.1f) return;
    float control = speed < r.stopspeed ? r.stopspeed : speed;
    float newspeed = std::max(speed - control * r.friction * dt, 0.0f);
    s.velocity = s.velocity * (newspeed / speed);
}

// PM_Accelerate
inline void Accelerate(State& s, Vec wishdir, float wishspeed, float accel, float dt) {
    float addspeed = wishspeed - Dot(s.velocity, wishdir);
    if (addspeed <= 0) return;
    float accelspeed = std::min(accel * dt * wishspeed, addspeed);
    s.velocity = s.velocity + wishdir * accelspeed;
}

// PM_AirAccelerate: the speed added along wishdir stops at 30, the rate is still the full wish
inline void AirAccelerate(State& s, Vec wishdir, float wishspeed, float accel, float dt) {
    float wishspd = std::min(wishspeed, kAirSpeedCap);
    float addspeed = wishspd - Dot(s.velocity, wishdir);
    if (addspeed <= 0) return;
    float accelspeed = std::min(accel * wishspeed * dt, addspeed);
    s.velocity = s.velocity + wishdir * accelspeed;
}

// A rocket's or grenade's push on the player who fired it (T_RadiusDamage with 120 and T_Damage's knockback): the
// damage is 120 less half the distance from the blast, halved for one's own, and pushes 8 inches a second a point
// away from the blast. Nothing past 240 inches.
inline constexpr float kBlastDamage = 120, kKnockback = 8;
inline Vec SelfKnockback(Vec blast, Vec origin) {
    Vec d = origin - blast;
    float dist = Length(d);
    float points = (kBlastDamage - 0.5f * dist) * 0.5f;
    if (points <= 0) return {};
    Vec dir = dist > 0.01f ? d * (1 / dist) : Vec{0, 0, 1};
    return dir * (points * kKnockback);
}

// A move across (its z ignored) against the contacts the player touches (normals toward the player): the rise along
// the steepest plane it goes into whose normal's z is at least kRiseNormal (a slope, a step's edge), in the move's
// units and at most kMaxRise times the move across; and whether it goes into one that faces up less (the collision
// lifts the player out of those itself). Floors it goes along or down, and walls, give neither.
// (1_2: a stair's edge 0.53, a 72 degree face beside the stair's top 0.31 that the move must not climb)
inline constexpr float kRiseNormal = 0.45f, kLiftNormal = 0.01f, kMaxRise = 2;
struct Climb {
    float rise = 0;
    bool lifted = false;
};
inline Climb ClimbAlong(Vec move, const Vec* normals, unsigned count) {
    Climb c;
    float across = Length2D(move);
    for (unsigned i = 0; i < count; i++) {
        const Vec& n = normals[i];
        float into = n.x * move.x + n.y * move.y;
        if (!(into < 0)) continue;
        if (n.z >= kRiseNormal)
            c.rise = std::max(c.rise, std::min(-into / n.z, kMaxRise * across));
        else if (n.z > kLiftNormal)
            c.lifted = true;
    }
    return c;
}

inline Vec Flat(Vec v) {
    v.z = 0;
    float l = Length(v);
    return l > 0 ? v * (1 / l) : Vec{};
}

// forward and right: the view's, flattened here (PM_AirMove)
inline Frame Move(State& s, const Rules& r, const Input& in, Vec forward, Vec right, float dt) {
    Frame f;
    forward = Flat(forward);
    right = Flat(right);
    float fmove = in.forward * (in.forward >= 0 ? r.forwardspeed : r.backspeed);
    float smove = in.side * r.sidespeed;
    if (in.crouched) fmove *= r.duck, smove *= r.duck;

    // JumpButton: no pogo stick, a press in the air waits for the landing
    if (in.auto_hop) s.old_jump = false;
    if (in.jump && in.can_jump) {
        if (s.on_ground && !s.old_jump) {
            s.on_ground = false;
            s.velocity.z += r.jump;
            s.old_jump = true;
            f.jumped = true;
        }
    } else {
        s.old_jump = false;
    }

    if (s.on_ground) {
        s.velocity.z = 0;
        Friction(s, r, dt);
    }
    Vec wishvel = forward * fmove + right * smove;
    wishvel.z = 0;
    float wishspeed = Length(wishvel);
    Vec wishdir = wishspeed > 0 ? wishvel * (1 / wishspeed) : Vec{};
    wishspeed = std::min(wishspeed, r.maxspeed);
    if (s.on_ground) {
        Accelerate(s, wishdir, wishspeed, r.accelerate, dt);
        s.velocity.z = 0;
    } else {
        AirAccelerate(s, wishdir, wishspeed, r.accelerate, dt);
    }
    float* c[] = {&s.velocity.x, &s.velocity.y, &s.velocity.z};
    for (float* v : c) *v = std::clamp(*v, -r.maxvelocity, r.maxvelocity);
    f.move = s.velocity;
    if (!s.on_ground) {
        f.move.z -= r.gravity * dt * 0.5f;
        s.velocity.z -= r.gravity * dt;
    }
    return f;
}

}  // namespace game::movement

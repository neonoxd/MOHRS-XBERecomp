// Custom Movement's Quake rules (src/common/movement_rules.h) on their own: speeds, jumps, bunny hops, air strafing.
// cmd //c "tools\\dev.bat clang++ -std=c++20 -Isrc tests/movement_rules_test.cpp -o re/movement_rules_test.exe" && ./re/movement_rules_test.exe
#include "common/movement_rules.h"

#include <cstdio>

using namespace game::movement;

namespace {

int failures = 0;

void Check(bool ok, const char* what, float value) {
    std::printf("%s %-60s %.2f\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) failures++;
}

const Vec kForward{1, 0, 0}, kRight{0, -1, 0};

struct Sim {
    State s;
    Vec pos;
    float floor = 0;
    // one frame: the move, then the floor as the game's collision would have it
    Frame Step(const Rules& r, const Input& in, float dt, Vec forward = kForward, Vec right = kRight) {
        s.on_ground = pos.z <= floor + 0.01f && s.velocity.z <= r.ground_z_speed;
        Frame f = Move(s, r, in, forward, right, dt);
        pos = pos + f.move * dt;
        if (pos.z < floor) pos.z = floor, s.velocity.z = 0;
        return f;
    }
};

float JumpHeight(const Rules& r, float fps) {
    Sim sim;
    Input in;
    float dt = 1 / fps, top = 0;
    sim.Step(r, in, dt);
    in.jump = true;
    for (int i = 0; i < int(fps * 2); i++) {
        sim.Step(r, in, dt);
        top = std::max(top, sim.pos.z);
    }
    return top;
}

float RunSpeed(const Rules& r, float seconds, Input in = {.forward = 1}) {
    Sim sim;
    float dt = 1 / 144.0f;
    for (int i = 0; i < int(seconds * 144); i++) sim.Step(r, in, dt);
    return Length2D(sim.s.velocity);
}

// bunny hops with air strafing: jump on landing, strafe right while turning right in the air, then left
float StrafeHops(const Rules& r, int hops, float fps = 144) {
    Sim sim;
    float dt = 1 / fps, yaw = 0, turn = 3.0f;  // radians a second
    Input in;
    // a run-up
    in.forward = 1;
    for (int i = 0; i < int(fps); i++) sim.Step(r, in, dt);
    int done = 0, side = 1;
    bool was_ground = true;
    for (int i = 0; i < int(fps * 30) && done < hops; i++) {
        bool ground = sim.pos.z <= sim.floor + 0.01f && sim.s.velocity.z <= r.ground_z_speed;
        if (ground && !was_ground) done++, side = -side;
        was_ground = ground;
        // a fresh press each landing, as a wheel jump would
        in.jump = ground;
        in.forward = 0;
        in.side = float(side);
        if (!ground) yaw += side * turn * dt;
        Vec f{std::cos(yaw), std::sin(yaw), 0}, rt{std::sin(yaw), -std::cos(yaw), 0};
        sim.Step(r, in, dt, f, rt);
    }
    return Length2D(sim.s.velocity);
}

}  // namespace

int main() {
    Check(std::fabs(RunSpeed(kQuake, 2) - 320) < 1, "running settles at 320", RunSpeed(kQuake, 2));
    Check(std::fabs(RunSpeed(kQuake, 2, {.forward = 0.7071f, .side = 0.7071f}) - 320) < 1, "a diagonal is no faster",
          RunSpeed(kQuake, 2, {.forward = 0.7071f, .side = 0.7071f}));
    {
        // full speed in a few tenths of a second: accel 10 * 320 per second, capped at 320
        float early = RunSpeed(kQuake, 0.05f);
        Check(early > 100 && early < 200, "0.05 s in, part of the way", early);
    }
    {
        // from 320, friction 4 with stopspeed 100 stops in well under a second
        Sim sim;
        sim.s.velocity = {320, 0, 0};
        Input none;
        float t = 0;
        while (Length2D(sim.s.velocity) > 0 && t < 2) sim.Step(kQuake, none, 1 / 144.0f), t += 1 / 144.0f;
        Check(t > 0.3f && t < 0.6f, "stops from 320 (seconds)", t);
    }
    // v^2 / 2g
    Check(std::fabs(JumpHeight(kQuake, 144) - 45.56f) < 1, "jump height 45.6", JumpHeight(kQuake, 144));
    for (float fps : {30.0f, 60.0f, 300.0f, 1000.0f})
        Check(std::fabs(JumpHeight(kQuake, fps) - JumpHeight(kQuake, 144)) < 1.5f, "same jump at other rates", fps);
    {
        // holding the jump: one jump (no pogo), none after landing
        Sim sim;
        Input in{.jump = true};
        int jumps = 0;
        for (int i = 0; i < 144 * 3; i++) jumps += sim.Step(kQuake, in, 1 / 144.0f).jumped;
        Check(jumps == 1, "a held jump jumps once", float(jumps));
    }
    {
        // with auto hop, a held jump hops on every landing (a 0.675 s hop: 4 in 3 s)
        Sim hop;
        Input held{.jump = true, .auto_hop = true};
        int hops = 0;
        for (int i = 0; i < 144 * 3; i++) hops += hop.Step(kQuake, held, 1 / 144.0f).jumped;
        Check(hops >= 4, "auto hop: a held jump hops on each landing", float(hops));
        // and holding forward with it keeps the speed (no ground frame, so no friction)
        Sim run;
        Input go{.forward = 1}, go_held{.forward = 1, .jump = true, .auto_hop = true};
        for (int i = 0; i < 144; i++) run.Step(kQuake, go, 1 / 144.0f);
        for (int i = 0; i < 144 * 3; i++) run.Step(kQuake, go_held, 1 / 144.0f);
        Check(Length2D(run.s.velocity) >= 319, "auto hop: the run speed keeps across hops", Length2D(run.s.velocity));
    }
    {
        // pressed in the air and held to the landing: jumps on landing
        Sim s2;
        Input j{.jump = true}, none;
        s2.Step(kQuake, j, 1 / 144.0f);
        for (int i = 0; i < 20; i++) s2.Step(kQuake, none, 1 / 144.0f);  // released, rising
        int again = 0;
        for (int i = 0; i < 144 * 2; i++) again += s2.Step(kQuake, j, 1 / 144.0f).jumped;  // pressed in the air
        Check(again >= 1, "a press in the air jumps on landing", float(again));
    }
    {
        // air strafing gains speed past the run speed
        float q = StrafeHops(kQuake, 8);
        Check(q > 400, "8 strafe hops gain speed", q);
        Check(std::fabs(StrafeHops(kQuake, 8, 60) - q) < q * 0.25f, "strafe hops similar at 60 FPS",
              StrafeHops(kQuake, 8, 60));
    }
    {
        // a rocket at the feet (28 inches below the origin): 4 * (120 - 14) = 424 up
        Vec up = SelfKnockback({0, 0, -28}, {});
        Check(std::fabs(up.z - 424) < 0.5f && std::fabs(up.x) < 0.01f, "rocket at the feet pushes 424 up", up.z);
        // with a jump (270) that is about 7 m high: (270 + 424)^2 / 1600
        Sim sim;
        sim.s.velocity = {0, 0, 270 + up.z};
        sim.pos = {0, 0, 0.02f};
        float top = 0;
        for (int i = 0; i < 144 * 3; i++) sim.Step(kQuake, {}, 1 / 144.0f), top = std::max(top, sim.pos.z);
        Check(std::fabs(top - 301) < 5, "rocket jump height (inches)", top);
        Vec side = SelfKnockback({100, 0, 0}, {});
        Check(side.x < 0 && std::fabs(side.x + 280) < 0.5f, "a blast 100 ahead pushes 280 back", side.x);
        Check(Length(SelfKnockback({0, 0, -250}, {})) == 0, "nothing past 240 inches", 0);
        Check(Length(SelfKnockback({}, {})) > 0, "a blast at the origin pushes up", SelfKnockback({}, {}).z);
    }
    {
        // 1_2's 45 degree slope: a move straight up it rises as far as it goes across, down it or on flat ground none
        Vec slope{0.465f, -0.54f, 0.70f}, flat{0, 0, 1};
        Vec up{-0.465f, 0.54f, 0}, down{0.465f, -0.54f, 0};
        float across = Length(up), rise = ClimbAlong(up, &slope, 1).rise;
        Check(std::fabs(rise / across - 1.02f) < 0.03f, "up a 45 degree slope rises as far as across", rise / across);
        Check(ClimbAlong(down, &slope, 1).rise == 0, "down it: no rise", ClimbAlong(down, &slope, 1).rise);
        Check(ClimbAlong(up, &flat, 1).rise == 0 && !ClimbAlong(up, &flat, 1).lifted, "flat ground: nothing", 0);
        Vec both[] = {flat, slope};
        Check(ClimbAlong(up, both, 2).rise == rise, "the steepest plane touched", ClimbAlong(up, both, 2).rise);
        // a stair's edge (a 1_2 stair, at 900 FPS): rises along it
        Vec edge{0.640f, 0.555f, 0.532f}, toward{-0.0011f, -0.0010f, 0};
        float r = ClimbAlong(toward, &edge, 1).rise;
        Check(r > Length2D(toward) && r <= 2 * Length2D(toward), "into a stair edge rises, at most twice across",
              r / Length2D(toward));
        // a wall leaning back a little: no rise (the collision lifts), an upright wall or one leaning over: nothing
        Vec lean{0.98f, 0, 0.2f}, wall{1, 0, 0}, over{0.98f, 0, -0.2f}, into{-1, 0, 0};
        Climb l = ClimbAlong(into, &lean, 1);
        Check(l.rise == 0 && l.lifted, "a wall leaning back: lifted by the collision, no rise", l.rise);
        Check(!ClimbAlong(into, &wall, 1).lifted && !ClimbAlong(into, &over, 1).lifted, "walls: nothing", 0);
        // a steep plane gives at most twice the move across
        Vec steep{0.89f, 0, 0.455f};
        Check(std::fabs(ClimbAlong(into, &steep, 1).rise - 1.956f) < 0.01f, "a steep plane: about twice across",
              ClimbAlong(into, &steep, 1).rise);
        // the 72 degree face beside that stair's top: not climbed
        Vec face{-0.376f, 0.874f, 0.308f}, at{0.376f, -0.874f, 0};
        Check(ClimbAlong(at, &face, 1).rise == 0 && ClimbAlong(at, &face, 1).lifted, "a 72 degree face: lifted only",
              ClimbAlong(at, &face, 1).rise);
    }
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}

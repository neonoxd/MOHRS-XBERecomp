#include "common/custom_movement.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <mutex>

#include "common/modern_aim.h"
#include "common/movement_rules.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

using movement::Vec;

constexpr float kTicksPerSecond = 59.94f;
constexpr float kInch = 0.0254f;  // Quake's unit in the game's (a meter)
// game units per tick from inches per second, and back
constexpr float kToGame = kInch / kTicksPerSecond, kFromGame = kTicksPerSecond / kInch;

// player fields, the same in both images
constexpr uint32_t kRight = 0x50, kForward = 0x60, kPosition = 0x80;
constexpr uint32_t kWorldVelocity = 0x230, kLocalVelocity = 0x240, kImpulse = 0x250;
constexpr uint32_t kContact = 0xD44, kState = 0xD45, kMode = 0xD46, kControl = 0xD4C, kCoopState = 0xD4E;
constexpr uint8_t kContactFloor = 2, kContactGravity = 8, kContactAiming = 0x10;
constexpr uint8_t kStateLadder = 1, kStateCrouched = 2, kStateCrouching = 4, kStateRaised = 8, kStateImpulse = 0x20;
constexpr uint8_t kModeVehicle = 1, kModeMounting = 0x10, kModeScripted = 0x40;
constexpr uint8_t kControlInput = 1, kControlFrozen = 4;
constexpr uint32_t kForwardAction = 6, kStrafeAction = 7, kJumpAction = 10;
constexpr float kActionScale = 1.0f / 127;
constexpr uint32_t kMaxPlayers = 4;
constexpr float kRise = 10;           // inches a second up more than asked: a step or a slope, not a wall
constexpr float kJumpBuffer = 0.15f;  // seconds a jump pressed in the air waits for the ground
// walking down a slope or stairs: pushed down at up to the speed across (45 degrees), at most this, inches a second
// (the collision resolve takes up to 0.3 units a tick back out of the floor)
constexpr float kStick = 300;
constexpr float kStepGrace = 0.06f;  // seconds off a step edge still on the ground, when walked off (not jumped)
// the upward faces touched (contact normals with z above movement::kLiftNormal) kept for a move up them
constexpr uint32_t kMaxFaces = 8;
// seconds a face touched is still gone up after the last touch: a move along a slope lifts the player just clear
// of it in some steps
constexpr float kFaceMemory = 0.1f;
// Quake's ground friction is doubled: at the game's scale (a player's height of 1.8 units) its 1.7 unit stop from a
// run felt like ice
constexpr float kFrictionScale = 2;

std::atomic<uint32_t> g_movement{0};
// the first player's speed across, inches a second, and when it was set (steady clock milliseconds)
std::atomic<float> g_speed{0};
std::atomic<int64_t> g_speed_at{0};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// game thread only
struct Player {
    uint32_t address = 0;
    bool active = false;   // the rules own its velocity (written by the last update)
    bool wrote = false;    // written this update, for the move step
    movement::State state;
    Vec start;             // the position the last velocity was written at, game units
    Vec asked;             // that velocity, inches per second
    float seconds = 0;     // and the update's time
    uint8_t index = 0;
    float ticks = 0;       // this update's
    float buffer = 0;      // seconds left of a jump press
    float off_ground = 0;  // seconds since the ground test last passed
    bool walked = false;   // on the ground since then, no jump: a step edge's grace applies
    bool ran = false;      // the rules ran in this update
    bool fresh = false;    // taken over this update
    bool jump = false;     // the jump the update is told
    Vec faces[kMaxFaces];  // the normals of the upward faces last touched
    uint32_t face_count = 0;
    float face_age = 0;    // seconds since
    Vec touched[kMaxFaces];  // touched in the move steps since the last update
    uint32_t touched_count = 0;
};
Player g_players[kMaxPlayers];
Movement g_was = Movement::Off;
// the player whose update is running, for the action query
Player* g_updating = nullptr;
// a player's own blast is being applied to them, for its view shake
bool g_own_blast = false;
// that shake's amplitude, of the game's (point blank its jolts reach 0.5 radians a step)
constexpr float kOwnBlastShake = 0.15f;

constexpr char kVariable[] = "MOHRS_MOVEMENT";

void Load() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char* v = std::getenv(kVariable);
        uint32_t m = v ? uint32_t(std::strtoul(v, nullptr, 10)) : 0;
        g_movement = m < uint32_t(Movement::Count) ? m : 0;
        if (g_movement) LOGR("cheats: %s movement from the previous instance", MovementName(Movement(g_movement.load())));
    });
}

bool Pointer(uint32_t address) { return address >= 0x10000; }

// MOHRS_MOVEMENT_TRACE (docs/debugging.md): each update's move and the rules' frame in mohrs.log
bool Trace() {
    static const bool on = std::getenv("MOHRS_MOVEMENT_TRACE") != nullptr;
    static uint32_t lines = 0;
    return on && lines++ < 200000;
}

Vec GetVec(uint32_t address) { return {G<float>(address), G<float>(address + 4), G<float>(address + 8)}; }
void SetVec(uint32_t address, Vec v) {
    G<float>(address) = v.x;
    G<float>(address + 4) = v.y;
    G<float>(address + 8) = v.z;
}

// on foot, walking: not on a ladder, a vehicle or a mounted gun, not scripted, not frozen or stunned, not aiming
// unless Modern Aiming walks the player then (src/common/modern_aim.h; not while the lean key has the game's lean)
bool OnFoot(uint32_t p, const MovementLayout& l) {
    uint8_t contact = G<uint8_t>(p + kContact), state = G<uint8_t>(p + kState), mode = G<uint8_t>(p + kMode);
    if ((contact & kContactAiming) && (!Opt().modern_aiming || ModernAimLeaning(p))) return false;
    if (state & kStateLadder) return false;
    if (mode & (kModeVehicle | kModeMounting | kModeScripted)) return false;
    if (G<uint8_t>(p + kControl) & kControlFrozen) return false;
    if (G<float>(p + l.stun) > 0) return false;
    if (l.coop && (G<uint8_t>(p + 0xE44) || G<uint8_t>(p + 0xE45) || (G<uint8_t>(p + kCoopState) & 0xC0)))
        return false;
    return true;
}

Player* Find(uint32_t p, const MovementLayout& l) {
    const CheatLayout& c = l.cheats;
    if (G<uint8_t>(c.multiplayer) || G<uint8_t>(c.front_end)) return nullptr;
    uint32_t count = std::min(R32(c.player_count), kMaxPlayers);
    for (uint32_t i = 0; i < count; i++) {
        if (R32(c.weapons.players + i * 4) != p) continue;
        Player& s = g_players[i];
        if (s.address != p) s = {}, s.address = p;
        return &s;
    }
    return nullptr;
}

// The input manager's entry for a player index (0x8F8 bytes each, none when its port byte is 0xFF) holds 12 bytes per
// action from +0x7C: a type byte, then for an edge type (bit 1) the held value at +4 and 1 at +8 only on the frame of
// the press; the others have their value at +8 (default.xbe's input update 0xB8820). The query (0xB83E0 / 0xACC80)
// reads +8.
struct Action {
    int32_t value = 0, held = 0;
};
Action ReadAction(const MovementLayout& l, uint8_t index, uint32_t action) {
    uint32_t entry = l.cheats.weapons.input_manager + uint32_t(index) * 0x8F8;
    if (index >= 4 || G<uint8_t>(entry) == 0xFF) return {};
    uint32_t a = entry + 0x7C + action * 0xC;
    int32_t value = int32_t(R32(a + 8));
    return {value, (G<uint8_t>(a) & 2) ? int32_t(R32(a + 4)) : value};
}

}  // namespace

Movement CustomMovement() {
    Load();
    return Movement(g_movement.load(std::memory_order_relaxed));
}

void SetCustomMovement(Movement m) {
    Load();
    if (uint32_t(m) >= uint32_t(Movement::Count)) m = Movement::Off;
    if (g_movement.exchange(uint32_t(m), std::memory_order_relaxed) == uint32_t(m)) return;
    LOGI("cheats: %s movement", MovementName(m));
    char text[8];
    std::snprintf(text, sizeof(text), "%u", unsigned(m));
    _putenv_s(kVariable, text);
}

const char* MovementName(Movement m) {
    return m == Movement::Quake ? "Quake" : "Off";
}

void MovementUpdateBegin(X86Context& ctx, const MovementLayout& l) {
    g_updating = nullptr;
    Movement m = CustomMovement();
    if (m != g_was) {
        for (Player& s : g_players) s = {};
        g_was = m;
    }
    if (m == Movement::Off) return;
    uint32_t p = ctx.ecx;
    float ticks = G<float>(ctx.esp + 4);
    Player* found = Find(p, l);
    if (!found) return;
    Player& s = *found;
    s.wrote = s.ran = false;
    if (!OnFoot(p, l) || !(ticks > 0)) {
        s.active = false;
        return;
    }
    Vec pos = GetVec(p + kPosition);
    if (!s.active) {
        // taking over: from the game's velocity
        s.state = {};
        s.state.velocity = GetVec(p + kWorldVelocity) * kFromGame;
        s.face_count = s.touched_count = 0;
        s.fresh = true;
        s.active = true;
    } else if (s.seconds > 0) {
        // what the move allowed: a wall takes away the part of the velocity into it, as Quake's clip against a
        // plane does. A stair step or a slope turns part of the move into a rise instead: the speed is kept.
        Vec moved = (pos - s.start) * (1 / (kInch * s.seconds));
        Vec& v = s.state.velocity;
        Vec lost{s.asked.x - moved.x, s.asked.y - moved.y, 0};
        float asked = movement::Length2D(s.asked), got = movement::Length2D(moved);
        bool rose = moved.z > std::max(s.asked.z, 0.0f) + kRise;
        bool clipped = !rose && asked > 1 && got < asked * 0.98f;
        if (clipped) {
            Vec n = movement::Flat(lost);
            float into = v.x * n.x + v.y * n.y;
            if (into > 0) v.x -= n.x * into, v.y -= n.y * into;
        }
        // a ceiling
        if (s.asked.z > 0 && moved.z < s.asked.z * 0.5f) v.z = std::min(v.z, std::max(moved.z, 0.0f));
        if (Trace())
            LOGR("movement: %08X asked %.0f %.0f %.0f moved %.0f %.0f %.0f%s%s", p, s.asked.x, s.asked.y, s.asked.z,
                 moved.x, moved.y, moved.z, rose ? " rose" : "", clipped ? " clipped" : "");
    }
    // a push the update adds to the velocity (an explosion's), the world's per tick
    if ((G<uint8_t>(p + kState) & kStateImpulse) && !(G<uint8_t>(p + kMode) & kModeScripted))
        s.state.velocity = s.state.velocity + GetVec(p + kImpulse) * kFromGame;
    s.ticks = ticks;
    g_updating = &s;
}

void MovementGround(X86Context& ctx, bool ground, const MovementLayout& l) {
    Player* s = g_updating;
    uint32_t p = ctx.esi;
    if (!s || s->address != p || s->ran) return;
    s->ran = true;
    movement::Rules r = movement::kQuake;
    r.friction *= kFrictionScale;
    s->index = G<uint8_t>(p + l.player_index);
    bool control = G<uint8_t>(p + kControl) & kControlInput;
    movement::Input in;
    float seconds = s->ticks / kTicksPerSecond;
    if (control) {
        in.forward = std::clamp(float(-ReadAction(l, s->index, kForwardAction).value) * kActionScale, -1.0f, 1.0f);
        in.side = std::clamp(float(ReadAction(l, s->index, kStrafeAction).value) * kActionScale, -1.0f, 1.0f);
        Action jump = ReadAction(l, s->index, kJumpAction);
        if (jump.value) s->buffer = kJumpBuffer;
        // held, or pressed a moment ago: a tap in the air between two steps still jumps on the next
        in.jump = jump.held != 0 || s->buffer > 0;
    }
    s->buffer = std::max(s->buffer - seconds, 0.0f);
    // a jump held when the rules took over waits for its release
    if (s->fresh) s->state.old_jump = in.jump;
    s->fresh = false;
    uint8_t state = G<uint8_t>(p + kState);
    in.crouched = state & (kStateCrouched | kStateCrouching);
    in.can_jump = !in.crouched;
    in.auto_hop = true;  // a held jump hops again on each landing
    // the game's ground test (its short ray down, or a floor touched), and Quake's rule that a player going up
    // fast is off the ground
    bool tested = ground && s->state.velocity.z <= r.ground_z_speed;
    s->off_ground = tested ? 0 : s->off_ground + seconds;
    // off a step edge walking down: still the ground for a moment, pushed down onto the next step (End)
    bool grace = !tested && s->walked && s->off_ground <= kStepGrace && s->state.velocity.z <= 0;
    s->state.on_ground = tested || grace;
    // Modern Aiming: slower on the ground while aiming; the air's speed is left alone
    if (s->state.on_ground && (G<uint8_t>(p + kContact) & kContactAiming)) r.maxspeed *= kAimWalk;
    movement::Frame f = movement::Move(s->state, r, in, GetVec(p + kForward), GetVec(p + kRight), seconds);
    s->asked = f.move;
    s->seconds = seconds;
    s->jump = f.jumped;
    if (s == &g_players[0]) {
        g_speed.store(movement::Length2D(s->state.velocity), std::memory_order_relaxed);
        g_speed_at.store(NowMs(), std::memory_order_relaxed);
    }
    s->walked = s->state.on_ground;  // a jump leaves the ground (Move clears it)
    if (s->state.on_ground) s->buffer = 0;
    if (Trace())
        LOGR("movement: %08X ground %d/%d%s in %.2f %.2f%s speed %.0f z %.0f%s", p, ground, s->state.on_ground ? 1 : 0, grace ? " grace" : "",
             in.forward, in.side, in.jump ? " jump" : "", movement::Length2D(s->state.velocity), s->state.velocity.z,
             f.jumped ? " jumped" : "");
}

void MovementUpdateEnd(uint32_t p, const MovementLayout& l) {
    Player* s = g_updating;
    g_updating = nullptr;
    if (!s || s->address != p || !s->ran) return;
    // the update may have put the player on a ladder, a gun or in a scene
    if (!OnFoot(p, l)) {
        s->active = false;
        return;
    }
    Vec world = s->asked * kToGame;
    // the upward faces touched since the last update, or those touched before for a moment
    if (s->touched_count) {
        std::copy_n(s->touched, s->touched_count, s->faces);
        s->face_count = s->touched_count;
        s->face_age = 0;
        s->touched_count = 0;
    } else {
        s->face_age += s->seconds;
    }
    if (!s->state.on_ground || s->face_age > kFaceMemory) s->face_count = 0;
    if (s->state.on_ground) {
        // up a slope or over a step's edge the move rises along it; pushed down into it instead, the collision would
        // push the player back out along its normal (on a 45 degree slope as far as it moved), and the short move
        // would be clipped (Begin). Into a face leaning back less, the move stays level and the collision lifts the
        // player, as the game's own walking does.
        movement::Climb climb = movement::ClimbAlong(world, s->faces, s->face_count);
        // otherwise the move goes down as fast as it goes across (to the limit), so the player follows a slope or
        // stairs down instead of leaving them; the floor pushes back a move into it, and stairs up rise (Begin)
        if (climb.rise > 0)
            world.z = climb.rise;
        else if (climb.lifted)
            world.z = 0;
        else
            world.z = -std::min(movement::Length2D(s->asked), kStick) * kToGame;
    }
    SetVec(p + kWorldVelocity, world);
    // the local velocity the collision resolve starts from: the world's in the orientation's rows
    Vec right = GetVec(p + kRight), forward = GetVec(p + kForward), up = GetVec(p + kForward + 0x10);
    SetVec(p + kLocalVelocity, {movement::Dot(world, right), movement::Dot(world, forward), movement::Dot(world, up)});
    s->start = GetVec(p + kPosition);
    s->wrote = true;
}

void MovementContact(const X86Context& ctx) {
    uint32_t p = ctx.ecx, pair = R32(ctx.esp + 4);
    Player* s = nullptr;
    for (Player& c : g_players)
        if (c.address == p && c.active) s = &c;
    if (!s || !Pointer(pair)) return;
    // the normal points away from the pair's first object
    float sign = R32(pair) == p ? -1.0f : 1.0f;
    for (uint32_t pt = R32(pair + 0x1C), n = 0; Pointer(pt) && n < 64; pt = R32(pt + 0x2C), n++) {
        if (!(G<float>(pt + 0x20) < 0)) continue;  // not touching
        Vec normal = GetVec(pt) * sign;
        if (normal.z > movement::kLiftNormal && s->touched_count < kMaxFaces) s->touched[s->touched_count++] = normal;
    }
}

void MovementStep(const X86Context& ctx, const MovementLayout& l) {
    uint32_t p = ctx.ecx;
    Player* s = nullptr;
    for (Player& c : g_players)
        if (c.address == p && c.active && c.wrote) s = &c;
    if (!s) return;
    // the step takes the game's gravity when no floor was touched. In the air the rules took their own, so it is given
    // back; on the ground (the update's short ray down hit) it stays, settling the player onto the floor as the
    // game's own walking does
    if (s->state.on_ground) return;
    uint8_t contact = G<uint8_t>(p + kContact);
    bool takes = (contact & kContactGravity) && !(contact & kContactFloor) && !(G<uint8_t>(p + kState) & kStateRaised);
    if (takes) G<float>(p + kWorldVelocity + 8) += G<float>(l.gravity) * G<float>(ctx.esp + 4);
}

bool MovementSpeed(float& speed) {
    if (CustomMovement() == Movement::Off) return false;
    // updates stop in the pause screens and the shell, and the counter with them
    if (NowMs() - g_speed_at.load(std::memory_order_relaxed) > 250) return false;
    speed = g_speed.load(std::memory_order_relaxed);
    return true;
}

void MovementBlast(X86Context& ctx, uint8_t* base, void (*original)(X86Context&, uint8_t*), const MovementLayout& l) {
    // explosion fields, the same in both images
    constexpr uint32_t kBlastCentre = 0x20, kBlastDamage = 0x34, kBlastAttacker = 0x3C;
    uint32_t blast = ctx.ecx, target = R32(ctx.esp + 4);
    Player* s = nullptr;
    if (CustomMovement() != Movement::Off && Pointer(target) && R32(blast + kBlastAttacker) == target)
        s = Find(target, l);
    if (!s) {
        original(ctx, base);
        return;
    }
    // the push, when the blast reaches the player by the apply's own test (rays from the centre) and the rules own
    // the player's velocity
    if (s->active && !(kernel::CallGuest(ctx, l.blast_blocked, {target}) & 0xFF)) {
        Vec push = movement::SelfKnockback(GetVec(blast + kBlastCentre) * (1 / kInch),
                                           GetVec(target + kPosition) * (1 / kInch));
        s->state.velocity = s->state.velocity + push;
        if (Trace())
            LOGR("movement: %08X blast %.0f %.0f %.0f, velocity %.0f %.0f %.0f", target, push.x, push.y, push.z,
                 s->state.velocity.x, s->state.velocity.y, s->state.velocity.z);
    }
    // no damage: the rest of the apply (the hit message, the rumble) as with God Mode, and a lighter shake
    ctx.ecx = blast;
    float& damage = G<float>(blast + kBlastDamage);
    float was = damage;
    damage = 0;
    g_own_blast = true;
    original(ctx, base);
    g_own_blast = false;
    G<float>(blast + kBlastDamage) = was;
}

void MovementShake(X86Context& ctx) {
    if (g_own_blast) G<float>(ctx.esp + 4) *= kOwnBlastShake;
}

void MovementQuery(X86Context& ctx, uint32_t index, uint32_t action) {
    if (!g_updating || !g_updating->ran || action != kJumpAction || (index & 0xFF) != g_updating->index) return;
    ctx.eax = g_updating->jump ? 1 : 0;
}

}  // namespace game

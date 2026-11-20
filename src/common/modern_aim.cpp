#include "common/modern_aim.h"

#include <algorithm>

#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

// player fields, the same in both images
constexpr uint32_t kLocalVelocity = 0x240, kPosition = 0x80, kState = 0xD45, kMode = 0xD46, kCoopState = 0xD4E;
constexpr uint8_t kStateLadder = 1, kStateLean = 8, kStateAir = 0x40;
constexpr uint8_t kModeVehicle = 1, kModeLean = 4, kModeMounting = 0x10, kModeScripted = 0x40;
// the eye raise and lower: its offset and the update's step for it, the aim bit, crouched, the standing lower allowed
constexpr uint32_t kEye = 0x270, kEyeStep = 0x28C, kAimState = 0xD44, kPose = 0xD4C;
constexpr uint8_t kAimOn = 0x10, kStateCrouched = 2, kPoseLower = 0x20;
constexpr uint32_t kForwardAction = 6;  // forward negative
// game thread: the player whose aim block passed this update, and the look rates it worked out
uint32_t g_held = 0;
float g_yaw = 0, g_pitch = 0;
// the player whose last aim block was left to the game's lean
uint32_t g_leaning = 0;

// the setting is on and the player on foot
bool Allowed(uint32_t p, const AimLayout& l) {
    if (!Opt().modern_aiming) return false;
    if (G<uint8_t>(p + kMode) & (kModeVehicle | kModeMounting | kModeScripted)) return false;
    if (G<uint8_t>(p + kState) & kStateLadder) return false;
    if (l.coop && (G<uint8_t>(p + 0xE44) || G<uint8_t>(p + 0xE45) || (G<uint8_t>(p + kCoopState) & 0xC0)))
        return false;
    return true;
}

// the input manager entry of the player KBM's port 0 is assigned to (0xB7E80: 0x8F8 bytes each, starting with the
// port), 0 for another player
uint32_t KeyboardEntry(uint32_t p, const AimLayout& l) {
    uint32_t count = std::min(uint32_t(G<uint8_t>(l.input_manager + 0x23E0)), 4u);
    for (uint32_t i = 0; i < count; i++)
        if (G<uint8_t>(l.input_manager + i * 0x8F8) == 0 && R32(l.players + i * 4) == p)
            return l.input_manager + i * 0x8F8;
    return 0;
}

bool KeyboardPlayer(uint32_t p, const AimLayout& l) { return KeyboardEntry(p, l) != 0; }

// an action's value from an input manager entry, as 0xB83E0 reads it (-127..127)
int32_t ActionValue(uint32_t entry, uint32_t action) { return G<int32_t>(entry + 0x7C + 8 + action * 0xC); }

// the lean key gives the player the game's aim rules: on the ground only
bool Leans(uint32_t p, const AimLayout& l) {
    return input::LeanHeld() && !(G<uint8_t>(p + kState) & kStateAir) && KeyboardPlayer(p, l);
}

void CopyVec(uint32_t to, uint32_t from) {
    for (uint32_t i = 0; i < 12; i += 4) G<float>(to + i) = G<float>(from + i);
}

}  // namespace

bool ModernAim(const X86Context& ctx, const AimLayout& l) {
    return Allowed(ctx.esi, l) && !Leans(ctx.esi, l);
}

bool ModernAimWalk(const X86Context& ctx, const AimLayout& l) {
    return g_leaning != ctx.esi && ModernAim(ctx, l);
}

bool ModernAimHold(X86Context& ctx, const AimLayout& l) {
    uint32_t p = ctx.esi;
    if (!Allowed(p, l)) {
        if (g_leaning == p) g_leaning = 0;
        return false;
    }
    if (Leans(p, l)) {
        // taken mid-aim: the lean starts from where the player stands, not where the aim started (the game's lean
        // pulls the player towards the anchor, the stick or not)
        if (g_leaning != p) CopyVec(p + l.anchor, p + kPosition);
        g_leaning = p;
        return false;
    }
    if (g_leaning == p) g_leaning = 0;
    g_held = p;
    g_yaw = G<float>(ctx.esp + l.yaw_rate);
    g_pitch = G<float>(ctx.esp + l.pitch_rate);
    // out on a lean (the lean key let go of, or the setting come on): on the ground put back at the anchor, as the
    // game's lean does when the stick centres; in the air it stays where it is
    if ((G<uint8_t>(p + kState) & kStateLean) && !(G<uint8_t>(p + kState) & kStateAir))
        CopyVec(p + kPosition, p + l.anchor);
    G<uint8_t>(p + kState) &= uint8_t(~kStateLean);
    G<uint8_t>(p + kMode) &= uint8_t(~kModeLean);
    // the anchor follows the walking player, as the aim block does in the air, so the lean key leans from here
    CopyVec(p + l.anchor, p + kPosition);
    return true;
}

void ModernAimWalked(X86Context& ctx, const AimLayout& l) {
    uint32_t p = ctx.esi;
    if (!g_held) return;
    bool mine = g_held == p;
    g_held = 0;
    if (!mine) return;
    G<float>(ctx.esp + l.yaw_rate) = g_yaw;
    G<float>(ctx.esp + l.pitch_rate) = g_pitch;
    // on the ground the walk code sets the velocity from the stick each update: slowed. In the air it adds to the
    // velocity the player has, which stays as it is when not aiming.
    if (!G<uint8_t>(ctx.esp + l.ground)) return;
    G<float>(p + kLocalVelocity) *= kAimWalk;
    G<float>(p + kLocalVelocity + 4) *= kAimWalk;
}

bool ModernAimLeaning(uint32_t player) { return player && g_leaning == player; }

void Lean(X86Context& ctx, uint8_t* base, GuestFunction original, const AimLayout& l) {
    uint32_t p = ctx.ecx;
    if (!Leans(p, l)) {
        original(ctx, base);
        return;
    }
    // the game's lean, with the strafe centred and a lean out, puts the player back at the anchor (0xC7490 writes
    // only the position) and clears +0xD45 bit 3; with the lean key the lean stays out, still
    bool out = G<uint8_t>(p + kState) & kStateLean;
    float pos[3];
    for (int i = 0; i < 3; i++) pos[i] = G<float>(p + kPosition + i * 4);
    original(ctx, base);
    if (out && !(G<uint8_t>(p + kState) & kStateLean)) {
        for (int i = 0; i < 3; i++) G<float>(p + kPosition + i * 4) = pos[i];
        G<uint8_t>(p + kState) |= kStateLean;
        G<float>(p + kLocalVelocity) = 0;
        G<float>(p + kLocalVelocity + 4) = 0;
        return;
    }
    G<float>(p + kLocalVelocity) *= kLeanSpeed;
    G<float>(p + kLocalVelocity + 4) *= kLeanSpeed;
}

void EyeHold(X86Context& ctx, const AimLayout& l) {
    uint32_t p = ctx.esi;
    if (!(G<uint8_t>(p + kAimState) & kAimOn)) return;
    uint32_t entry = KeyboardEntry(p, l);
    if (!entry || !input::LeanHeld() || (G<uint8_t>(p + kState) & kStateAir)) return;
    // the game's own tests: the raise is blocked while bl is set; the standing lower needs the ground byte
    if (ctx.ebx & 0xFF) return;
    bool crouched = G<uint8_t>(p + kState) & kStateCrouched;
    if (!crouched && !((G<uint8_t>(p + kPose) & kPoseLower) && G<uint8_t>(ctx.esp + l.ground))) return;
    // crouched the eye rests at 0 and forward raises it to the full height; standing it rests at the full height and
    // back lowers it to 0. Either way forward goes up, back down, neither stays.
    float stick = float(ActionValue(entry, kForwardAction)) / 127.0f;
    float height = G<float>(l.eye_height);
    float eye = G<float>(p + kEye);
    float dt = G<float>(ctx.ebp + 8) / 60.0f;  // the update's time is in ticks, 60 a second
    float step = 0;
    if (stick <= -kEyeDeadzone) step = std::min(kEyeSpeed * dt, std::max(height - eye, 0.0f));
    else if (stick >= kEyeDeadzone) step = -std::min(kEyeSpeed * dt, std::max(eye, 0.0f));
    G<float>(p + kEyeStep) = step;
}

void LeanStill(X86Context& ctx, const AimLayout& l) {
    uint32_t p = ctx.esi;
    // aiming on the ground with the lean key: the walk is stopped (or skipped), any speed is the lean's
    if ((G<uint8_t>(p + kAimState) & kAimOn) && Leans(p, l)) G<float>(ctx.esp + l.move_speed) = 0;
}

}  // namespace game

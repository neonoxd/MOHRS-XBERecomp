#include "common/modern_aim.h"

#include "common/options.h"
#include "kernel/kernel.h"

using kernel::G;

namespace game {

namespace {

// player fields, the same in both images
constexpr uint32_t kLocalVelocity = 0x240, kState = 0xD45, kMode = 0xD46, kCoopState = 0xD4E;
constexpr uint8_t kStateLadder = 1, kStateLean = 8;
constexpr uint8_t kModeVehicle = 1, kModeLean = 4, kModeMounting = 0x10, kModeScripted = 0x40;
// game thread: the player whose aim block passed this update, and the look rates it worked out
uint32_t g_held = 0;
float g_yaw = 0, g_pitch = 0;

}  // namespace

bool ModernAim(const X86Context& ctx, const AimLayout& l) {
    if (!Opt().modern_aiming) return false;
    uint32_t p = ctx.esi;
    if (G<uint8_t>(p + kMode) & (kModeVehicle | kModeMounting | kModeScripted)) return false;
    if (G<uint8_t>(p + kState) & kStateLadder) return false;
    if (l.coop && (G<uint8_t>(p + 0xE44) || G<uint8_t>(p + 0xE45) || (G<uint8_t>(p + kCoopState) & 0xC0)))
        return false;
    return true;
}

bool ModernAimHold(X86Context& ctx, const AimLayout& l) {
    if (!ModernAim(ctx, l)) return false;
    uint32_t p = ctx.esi;
    g_held = p;
    g_yaw = G<float>(ctx.esp + l.yaw_rate);
    g_pitch = G<float>(ctx.esp + l.pitch_rate);
    // out on a lean when the setting came on: it stays where it is, not put back when the aim ends
    G<uint8_t>(p + kState) &= uint8_t(~kStateLean);
    G<uint8_t>(p + kMode) &= uint8_t(~kModeLean);
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

}  // namespace game

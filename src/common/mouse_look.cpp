#include "common/mouse_look.h"
#include "common/frame_timing.h"

#include "common/mouse_keyboard.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

constexpr uint32_t kYaw = 0x274;
constexpr uint32_t kPitch = 0x278;
constexpr uint32_t kFov = 0xE30;
constexpr uint32_t kGunYaw = 0xE1C;
constexpr uint32_t kGunPitch = 0xE20;
constexpr float kHipFov = 35.0f;
// radians a count turns at sensitivity 1: 0.05 degrees
constexpr float kRadians = 0.05f * 3.14159265f / 180.0f;

// the mouse's turn this tick, less as the view zooms in; false for another player or no movement
bool Turn(const X86Context& ctx, uint32_t players, float& yaw, float& pitch) {
    uint32_t player = ctx.esi;
    if (!player || player != R32(players)) return false;
    float dx;
    float dy;
    if (!input::TakeMouse(dx, dy)) return false;
    float fov = G<float>(player + kFov);
    float zoom = fov > 0.0f && fov <= kHipFov ? fov / kHipFov : 1.0f;
    yaw = -dx * kRadians * zoom;  // positive turns left
    pitch = dy * kRadians * zoom;  // positive looks down
    return true;
}

}  // namespace

void MouseLook(X86Context& ctx, uint32_t players) {
    if (ctx.esi == R32(players)) PlayerTick(G<float>(ctx.ebp + 8));
    float yaw;
    float pitch;
    if (!Turn(ctx, players, yaw, pitch)) return;
    G<float>(ctx.esi + kYaw) += yaw;
    G<float>(ctx.esi + kPitch) += pitch;
}

void TurretLook(X86Context& ctx, uint32_t players) {
    float yaw;
    float pitch;
    if (!Turn(ctx, players, yaw, pitch)) return;
    G<float>(ctx.esi + kGunYaw) += yaw;
    G<float>(ctx.esi + kGunPitch) += pitch;
}

}  // namespace game

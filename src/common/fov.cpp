#include "common/fov.h"

#include <chrono>
#include <cstdlib>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace game {

namespace {

bool IsPlayerCamera(uint32_t camera, uint32_t players, uint32_t count_address, uint32_t camera_offset) {
    uint32_t count = std::min(kernel::R32(count_address), 4u);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t player = kernel::R32(players + i * 4);
        if (player && camera == player + camera_offset) return true;
    }
    return false;
}

}  // namespace

PlayerNearPlane::PlayerNearPlane(uint32_t camera, uint32_t players, uint32_t count_address, uint32_t camera_offset) {
    static const bool off = std::getenv("MOHRS_NEAR_PLANE_ORIGINAL") != nullptr;
    if (off || !camera || (!app::Widescreen() && ClampFov(Opt().fov) == kDefaultFov)) return;
    if (!IsPlayerCamera(camera, players, count_address, camera_offset)) return;
    float& near = kernel::G<float>(camera + 0x38);
    float scale = NearPlaneScale(kernel::G<float>(camera + 0x40), kernel::G<float>(camera + 0x44));
    if (!(scale < 1.0f) || !(near > 0.0f)) return;
    camera_ = camera;
    near_ = near;
    scaled_ = near * scale;
    near = scaled_;
}

PlayerNearPlane::~PlayerNearPlane() {
    if (!camera_) return;
    float& near = kernel::G<float>(camera_ + 0x38);
    if (near == scaled_) near = near_;
}

void AdjustPlayerFov(uint32_t camera, uint32_t players, uint32_t count_address, uint32_t camera_offset) {
    if (!camera) return;
    static const bool trace = std::getenv("MOHRS_FOV_TRACE") != nullptr;
    const uint32_t setting = ClampFov(Opt().fov);
    if (setting == kDefaultFov && !trace) return;
    uint32_t count = std::min(kernel::R32(count_address), 4u);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t player = kernel::R32(players + i * 4);
        if (!player || camera != player + camera_offset) continue;
        float& across = kernel::G<float>(camera + 0x40);
        float& down = kernel::G<float>(camera + 0x44);
        float before_across = across, before_down = down;
        float scale = FovTangentScale(setting);
        across *= scale;
        down *= scale;
        if (trace) {
            using Clock = std::chrono::steady_clock;
            static thread_local Clock::time_point next{};
            auto now = Clock::now();
            if (now >= next) {
                next = now + std::chrono::seconds(1);
                LOGR("fov: player %u camera %08X setting %u; horizontal %.2f -> %.2f, vertical %.2f -> %.2f degrees",
                     i, camera, setting, std::atan(before_across) / kHalfDegrees, std::atan(across) / kHalfDegrees,
                     std::atan(before_down) / kHalfDegrees, std::atan(down) / kHalfDegrees);
            }
        }
        break;
    }
}

}  // namespace game

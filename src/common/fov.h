#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game {

inline constexpr uint32_t kDefaultFov = 70;
inline constexpr uint32_t kMinFov = 45;
inline constexpr uint32_t kMaxFov = 140;
inline constexpr float kHalfDegrees = 3.14159265358979323846f / 360.0f;

inline uint32_t ClampFov(uint32_t degrees) { return std::clamp(degrees, kMinFov, kMaxFov); }

// Horizontal degrees on the original 4:3 frame. Scaling both projection tangents preserves zoom and aspect.
inline float FovTangentScale(uint32_t degrees) {
    degrees = ClampFov(degrees);
    if (degrees == kDefaultFov) return 1.0f;
    return std::tan(float(degrees) * kHalfDegrees) / std::tan(float(kDefaultFov) * kHalfDegrees);
}

// After SetFov and the widescreen correction. Only the registered players' world cameras are adjusted.
void AdjustPlayerFov(uint32_t camera, uint32_t players, uint32_t count_address, uint32_t camera_offset);

// The game's own player view: SetFov(35, 0.75 * 35 degrees) on the 4:3 frame (0xA2C29), near plane 0.4 at camera +0x38.
inline const float kPlayerTanAcross = std::tan(35.0f * 2.0f * kHalfDegrees);
inline const float kPlayerTanDown = std::tan(0.75f * 35.0f * 2.0f * kHalfDegrees);

// What the player's near plane distance is multiplied by so that its half width and height stay within the original
// view's: widened by widescreen or the FoV setting, its sides would reach into walls the player stands against.
inline float NearPlaneScale(float tan_across, float tan_down) {
    float scale = 1.0f;
    if (tan_across > kPlayerTanAcross) scale = kPlayerTanAcross / tan_across;
    if (tan_down > kPlayerTanDown) scale = std::min(scale, kPlayerTanDown / tan_down);
    return scale;
}

// For as long as it lives: a registered player world camera's near plane (+0x38) scaled by NearPlaneScale, for the
// game's frustum (default.xbe 0x82E50) and projection (0x84290) builds, which read it. Restored afterwards, so the
// game's own value is what it keeps and saves.
class PlayerNearPlane {
public:
    PlayerNearPlane(uint32_t camera, uint32_t players, uint32_t count_address, uint32_t camera_offset);
    ~PlayerNearPlane();
    PlayerNearPlane(const PlayerNearPlane&) = delete;
    PlayerNearPlane& operator=(const PlayerNearPlane&) = delete;

private:
    uint32_t camera_ = 0;
    float near_ = 0, scaled_ = 0;
};

}  // namespace game

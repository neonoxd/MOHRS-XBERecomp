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

}  // namespace game

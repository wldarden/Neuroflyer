#pragma once

#include <neuroflyer/game.h>

#include <cmath>
#include <vector>

namespace neuroflyer {

/// Variable range multiplier: center rays see 2x further, adjacent 1.5x.
[[nodiscard]] inline float ray_range_multiplier(int i, int num_rays) {
    int center = num_rays / 2;
    int dist_from_center = std::abs(i - center);
    if (dist_from_center == 0) return 2.0f;
    if (dist_from_center == 1) return 2.0f;
    if (dist_from_center == 2) return 1.5f;
    return 1.0f;
}

/// What a ray hit.
enum class HitType : int { Nothing = 0, Tower = -1, Token = 1 };

/// Result per ray: distance (0-1 normalized) and what it hit.
struct RayResult {
    float distance = 1.0f;  // 0.0 = touching, 1.0 = nothing in range
    HitType hit = HitType::Nothing;
};

/// Endpoint for visualization.
struct RayEndpoint {
    float x, y;
    float distance;
    HitType hit = HitType::Nothing;
};

/// Cast rays from the triangle. Returns num_rays RayResults.
/// Each ray returns distance + type of first object hit.
[[nodiscard]] std::vector<RayResult> cast_rays(
    const Triangle& tri,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    float max_range,
    int num_rays);

/// Cast rays with endpoint positions for visualization.
[[nodiscard]] std::vector<RayEndpoint> cast_rays_with_endpoints(
    const Triangle& tri,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    float max_range,
    int num_rays);

} // namespace neuroflyer

#include <neuroflyer/ray.h>
#include <neuroflyer/collision.h>

#include <cmath>
#include <numbers>

namespace neuroflyer {

std::vector<RayResult> cast_rays(
    const Triangle& tri,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    float max_range,
    int num_rays) {

    std::vector<RayResult> results(static_cast<std::size_t>(num_rays));

    for (int i = 0; i < num_rays; ++i) {
        float angle = -std::numbers::pi_v<float> / 2.0f + std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(num_rays - 1);
        float dx = std::sin(angle);
        float dy = -std::cos(angle);

        float ray_max = max_range * ray_range_multiplier(i, num_rays);
        float closest = ray_max;
        HitType closest_type = HitType::Nothing;

        // Check towers
        for (const auto& tower : towers) {
            if (!tower.alive) continue;
            float t = ray_circle_intersect(tri.x, tri.y, dx, dy,
                                            tower.x, tower.y, tower.radius);
            if (t >= 0.0f && t < closest) {
                closest = t;
                closest_type = HitType::Tower;
            }
        }

        // Check tokens
        for (const auto& token : tokens) {
            if (!token.alive) continue;
            float t = ray_circle_intersect(tri.x, tri.y, dx, dy,
                                            token.x, token.y, token.radius);
            if (t >= 0.0f && t < closest) {
                closest = t;
                closest_type = HitType::Token;
            }
        }

        auto idx = static_cast<std::size_t>(i);
        results[idx].distance = closest / ray_max;
        results[idx].hit = closest_type;
    }

    return results;
}

std::vector<RayEndpoint> cast_rays_with_endpoints(
    const Triangle& tri,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    float max_range,
    int num_rays) {

    std::vector<RayEndpoint> endpoints(static_cast<std::size_t>(num_rays));

    for (int i = 0; i < num_rays; ++i) {
        float angle = -std::numbers::pi_v<float> / 2.0f + std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(num_rays - 1);
        float dx = std::sin(angle);
        float dy = -std::cos(angle);

        float ray_max = max_range * ray_range_multiplier(i, num_rays);
        float closest = ray_max;
        HitType closest_type = HitType::Nothing;

        for (const auto& tower : towers) {
            if (!tower.alive) continue;
            float t = ray_circle_intersect(tri.x, tri.y, dx, dy,
                                            tower.x, tower.y, tower.radius);
            if (t >= 0.0f && t < closest) {
                closest = t;
                closest_type = HitType::Tower;
            }
        }

        for (const auto& token : tokens) {
            if (!token.alive) continue;
            float t = ray_circle_intersect(tri.x, tri.y, dx, dy,
                                            token.x, token.y, token.radius);
            if (t >= 0.0f && t < closest) {
                closest = t;
                closest_type = HitType::Token;
            }
        }

        auto idx = static_cast<std::size_t>(i);
        endpoints[idx].x = tri.x + dx * closest;
        endpoints[idx].y = tri.y + dy * closest;
        endpoints[idx].distance = closest / ray_max;
        endpoints[idx].hit = closest_type;
    }

    return endpoints;
}

} // namespace neuroflyer

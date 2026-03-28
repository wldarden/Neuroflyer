#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroflyer {

struct ArenaConfig {
    // World dimensions in pixels
    float world_width = 1280.0f * 64.0f;
    float world_height = 800.0f * 64.0f;

    // Teams
    std::size_t num_teams = 2;
    std::size_t team_size = 50;

    // Round timing
    uint32_t time_limit_ticks = 60 * 60;  // 60 seconds at 60fps
    std::size_t rounds_per_generation = 1;

    // Obstacles
    std::size_t tower_count = 200;
    std::size_t token_count = 100;

    // Bullets
    float bullet_max_range = 1000.0f;

    // Boundaries
    bool wrap_ns = true;
    bool wrap_ew = true;

    // Ship
    float rotation_speed = 0.05f;   // radians per tick

    // Derived
    [[nodiscard]] std::size_t population_size() const noexcept {
        return num_teams * team_size;
    }
};

} // namespace neuroflyer

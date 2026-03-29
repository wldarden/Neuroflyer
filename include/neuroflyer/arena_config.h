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

    // Bases
    float base_hp = 1000.0f;
    float base_radius = 100.0f;
    float base_bullet_damage = 10.0f;  // HP removed per bullet hit

    // Squads
    std::size_t num_squads = 1;
    std::size_t fighters_per_squad = 8;
    std::size_t squad_broadcast_signals = 4;

    // Fitness weights
    float fitness_weight_base_damage = 1.0f;
    float fitness_weight_survival = 0.5f;
    float fitness_weight_ships_alive = 0.2f;
    float fitness_weight_tokens = 0.3f;

    // Derived
    [[nodiscard]] std::size_t population_size() const noexcept {
        return num_teams * num_squads * fighters_per_squad;
    }
};

} // namespace neuroflyer

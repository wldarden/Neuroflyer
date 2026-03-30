#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

    // Sector grid (for NTM spatial indexing)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;  // Manhattan distance for "near"

    // Near Threat Matrix (NTM) sub-net topology
    std::size_t ntm_input_size = 6;
    std::vector<std::size_t> ntm_hidden_sizes = {4};
    std::size_t ntm_output_size = 1;  // just threat_score

    // Squad leader net topology
    std::size_t squad_leader_input_size = 11;
    std::vector<std::size_t> squad_leader_hidden_sizes = {8};
    std::size_t squad_leader_output_size = 5;  // 2 spacing + 3 tactical

    // Squad leader fighter inputs (replaces broadcast signals)
    static constexpr std::size_t squad_leader_fighter_inputs = 6;

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

#pragma once

#include <neuroflyer/arena_world.h>

#include <cstddef>
#include <cstdint>

namespace neuroflyer {

struct ArenaConfig {
    /// Physics/world parameters — delegated to ArenaWorldConfig.
    ArenaWorldConfig world{
        .world_width = 1280.0f * 64.0f,
        .world_height = 800.0f * 64.0f,
        .num_teams = 2,
        .num_squads = 1,
        .fighters_per_squad = 8,
        .tower_count = 200,
        .token_count = 100,
        .bullet_max_range = 1000.0f,
        .wrap_ns = true,
        .wrap_ew = true,
        .rotation_speed = 0.05f,
        .ship_hp = 3.0f,
        .bullet_ship_damage = 1.0f,
        .base_hp = 1000.0f,
        .base_radius = 100.0f,
        .base_bullet_damage = 10.0f,
        .friendly_fire = false,
    };

    // Round timing
    uint32_t time_limit_ticks = 60 * 60;  // 60 seconds at 60fps
    std::size_t rounds_per_generation = 10;  // multiple rounds for matchmaking coverage

    // Sector grid (for NTM spatial indexing)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;  // Manhattan distance for "near"

    // NTM and squad leader net topologies are configured via NtmNetConfig and
    // SquadLeaderNetConfig in team_evolution.h (the authoritative source).

    // Squad leader fighter inputs (replaces broadcast signals)
    static constexpr std::size_t squad_leader_fighter_inputs = 6;

    // Fitness weights
    float fitness_weight_base_damage = 1.0f;
    float fitness_weight_survival = 0.5f;
    float fitness_weight_ships_alive = 0.2f;
    float fitness_weight_tokens = 0.3f;

    // Derived — delegate to world
    [[nodiscard]] std::size_t population_size() const noexcept {
        return world.population_size();
    }

    [[nodiscard]] float world_diagonal() const noexcept {
        return world.world_diagonal();
    }
};

} // namespace neuroflyer

#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_world.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neuroflyer {

struct SkirmishConfig {
    /// Physics/world parameters — delegated to ArenaWorldConfig.
    ArenaWorldConfig world{
        .world_width = 4000.0f,
        .world_height = 4000.0f,
        .num_teams = 2,
        .num_squads = 1,
        .fighters_per_squad = 8,
        .tower_count = 50,
        .token_count = 30,
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

    // Tournament parameters
    std::size_t population_size = 20;
    std::size_t seeds_per_match = 3;
    uint32_t time_limit_ticks = 60 * 60;

    // Sector grid (for NTM spatial indexing)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;

    // Scoring
    float kill_points = 100.0f;
    float death_points = 100.0f;
    float base_hit_points = 10.0f;  // per bullet hit on enemy base (kill_points / 10)

    /// Points awarded for destroying an enemy base: kill_points * total fighters per team.
    [[nodiscard]] float base_kill_points() const noexcept {
        return kill_points
            * static_cast<float>(world.fighters_per_squad)
            * static_cast<float>(world.num_squads);
    }

    [[nodiscard]] float world_diagonal() const noexcept {
        return world.world_diagonal();
    }
};

struct SkirmishMatchResult {
    std::vector<float> team_scores;
    uint32_t ticks_elapsed = 0;
    bool completed = false;
};

/// Run a complete skirmish match with kill-based scoring.
/// teams.size() determines num_teams (overrides SkirmishConfig default of 2).
[[nodiscard]] SkirmishMatchResult run_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed);

} // namespace neuroflyer

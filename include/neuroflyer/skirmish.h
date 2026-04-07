#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neuroflyer {

struct SkirmishConfig {
    std::size_t population_size = 20;
    std::size_t seeds_per_match = 3;
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    std::size_t num_squads_per_team = 1;
    std::size_t fighters_per_squad = 8;
    std::size_t tower_count = 50;
    std::size_t token_count = 30;
    uint32_t time_limit_ticks = 60 * 60;
    float base_hp = 1000.0f;
    float base_radius = 100.0f;
    float base_bullet_damage = 10.0f;
    float rotation_speed = 0.05f;
    float bullet_max_range = 1000.0f;
    bool wrap_ns = true;
    bool wrap_ew = true;
    bool friendly_fire = false;  // when false, bullets pass through teammate ships
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;
    float kill_points = 100.0f;
    float death_points = 100.0f;
    float base_hit_points = 10.0f;  // per bullet hit on enemy base (kill_points / 10)

    /// Points awarded for destroying an enemy base: kill_points * total fighters per team.
    [[nodiscard]] float base_kill_points() const noexcept {
        return kill_points
            * static_cast<float>(fighters_per_squad)
            * static_cast<float>(num_squads_per_team);
    }

    /// Convert to ArenaConfig for use with ArenaSession.
    [[nodiscard]] ArenaConfig to_arena_config() const noexcept {
        ArenaConfig ac;
        ac.world.world_width = world_width;
        ac.world.world_height = world_height;
        ac.world.num_teams = 2;  // caller overrides from teams.size()
        ac.world.num_squads = num_squads_per_team;
        ac.world.fighters_per_squad = fighters_per_squad;
        ac.world.tower_count = tower_count;
        ac.world.token_count = token_count;
        ac.time_limit_ticks = time_limit_ticks;
        ac.world.base_hp = base_hp;
        ac.world.base_radius = base_radius;
        ac.world.base_bullet_damage = base_bullet_damage;
        ac.world.rotation_speed = rotation_speed;
        ac.world.bullet_max_range = bullet_max_range;
        ac.world.wrap_ns = wrap_ns;
        ac.world.wrap_ew = wrap_ew;
        ac.world.friendly_fire = friendly_fire;
        ac.sector_size = sector_size;
        ac.ntm_sector_radius = ntm_sector_radius;
        ac.rounds_per_generation = 1;
        return ac;
    }

    [[nodiscard]] float world_diagonal() const noexcept {
        return std::sqrt(world_width * world_width + world_height * world_height);
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

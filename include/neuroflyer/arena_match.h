#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>

#include <cstdint>
#include <vector>

namespace neuroflyer {

struct ArenaMatchResult {
    std::vector<float> team_scores;
    bool match_completed = false;
    uint32_t ticks_elapsed = 0;
};

/// Run a complete arena match. Returns per-team scores.
/// teams.size() must equal arena_config.num_teams.
[[nodiscard]] ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const SquadNetConfig& squad_config,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed);

} // namespace neuroflyer

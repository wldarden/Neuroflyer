#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neuroflyer {

struct ArenaInfoState {
    std::size_t generation = 0;
    uint32_t current_tick = 0;
    uint32_t time_limit_ticks = 0;
    std::size_t alive_count = 0;
    std::size_t total_count = 0;
    std::size_t teams_alive = 0;
    std::size_t num_teams = 0;
    std::vector<int> team_enemy_kills;   // per-team enemy kill totals
    std::vector<int> team_ally_kills;    // per-team ally kill totals
    std::vector<float> team_scores;      // per-team scores
};

void draw_arena_game_info_view(const ArenaInfoState& info);

} // namespace neuroflyer

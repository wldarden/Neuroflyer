#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroflyer {

struct ArenaInfoState {
    std::size_t generation = 0;
    uint32_t current_tick = 0;
    uint32_t time_limit_ticks = 0;
    std::size_t alive_count = 0;
    std::size_t total_count = 0;
    std::size_t teams_alive = 0;
    std::size_t num_teams = 0;
};

void draw_arena_game_info_view(const ArenaInfoState& info);

} // namespace neuroflyer

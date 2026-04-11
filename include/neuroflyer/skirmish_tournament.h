#pragma once

#include <neuroflyer/arena_session.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/team_evolution.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace neuroflyer {

struct TournamentRound {
    std::vector<std::pair<std::size_t, std::size_t>> matchups;  // variant indices
    std::size_t bye_index = SIZE_MAX;
};

class SkirmishTournament {
public:
    SkirmishTournament(const SkirmishConfig& config,
                       const ShipDesign& fighter_design,
                       const std::vector<TeamIndividual>& variants,
                       uint32_t seed);

    bool step();  // advance featured match one tick; returns true when tournament complete

    /// Run background seeds within time budget (milliseconds).
    /// Call after step() each frame to fill remaining frame time.
    void run_background_work(int budget_ms);

    [[nodiscard]] std::size_t current_round() const noexcept;
    [[nodiscard]] std::size_t current_match() const noexcept;
    [[nodiscard]] std::size_t current_seed() const noexcept;
    [[nodiscard]] std::size_t total_rounds() const noexcept;
    [[nodiscard]] std::size_t matches_in_round() const noexcept;
    [[nodiscard]] std::size_t seeds_for_current_round() const noexcept;
    [[nodiscard]] bool is_complete() const noexcept;

    [[nodiscard]] const ArenaSession* current_arena() const noexcept;
    [[nodiscard]] const std::vector<float>& variant_scores() const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> current_matchup() const noexcept;
    [[nodiscard]] const std::vector<int>& current_ship_teams() const noexcept;

    [[nodiscard]] neuralnet::Network* fighter_net(std::size_t team) noexcept;
    [[nodiscard]] neuralnet::Network* leader_net(std::size_t team) noexcept;

    /// Per-ship squad leader fighter inputs from last tick (for visualization).
    [[nodiscard]] const std::vector<SquadLeaderFighterInputs>& last_squad_inputs() const noexcept;

    /// Per-team squad leader net input values from last tick (14 floats).
    [[nodiscard]] const std::vector<float>& last_leader_input(std::size_t team) const noexcept;

    /// Per-ship fighter net input values from last tick.
    [[nodiscard]] const std::vector<float>& last_fighter_input(std::size_t ship) const noexcept;

private:
    void build_rounds();
    void start_match();
    void finish_match();
    void run_tick();

    // Background match lifecycle (tick-level granularity)
    void start_bg_match();
    void finish_bg_match();
    void drain_background();

    SkirmishConfig config_;
    ShipDesign fighter_design_;
    std::vector<TeamIndividual> variants_;

    std::vector<TournamentRound> rounds_;
    std::vector<float> scores_;
    std::vector<std::size_t> active_;

    std::size_t round_idx_ = 0;
    std::size_t match_idx_ = 0;
    std::size_t seed_idx_ = 0;

    std::unique_ptr<ArenaSession> arena_;
    ArenaConfig arena_config_;
    std::vector<neuralnet::Network> ntm_nets_;
    std::vector<neuralnet::Network> leader_nets_;
    std::vector<neuralnet::Network> fighter_nets_;
    std::vector<std::vector<float>> recurrent_states_;
    std::vector<int> ship_teams_;
    std::vector<SquadLeaderFighterInputs> last_sl_inputs_;  // per ship, updated each tick
    std::vector<std::vector<float>> last_leader_inputs_;   // per team (2), 14 floats each
    std::vector<std::vector<float>> last_fighter_inputs_;   // per ship, full input vector

    // Background match state (tick-level granularity)
    bool bg_match_active_ = false;
    std::unique_ptr<ArenaSession> bg_arena_;
    ArenaConfig bg_arena_config_;
    std::vector<neuralnet::Network> bg_ntm_nets_;
    std::vector<neuralnet::Network> bg_leader_nets_;
    std::vector<neuralnet::Network> bg_fighter_nets_;
    std::vector<std::vector<float>> bg_recurrent_states_;
    std::vector<int> bg_ship_teams_;

    std::mt19937 rng_;
    bool complete_ = false;
    bool match_in_progress_ = false;
    bool background_matches_done_ = false;
    std::size_t bg_match_cursor_ = 1;  // next background matchup (0 = featured, skip)
    std::size_t bg_seed_cursor_ = 0;   // next seed within current background matchup
};

} // namespace neuroflyer

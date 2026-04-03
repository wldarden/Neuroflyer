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

    bool step();  // returns true when tournament is complete

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

private:
    void build_rounds();
    void start_match();
    void finish_match();
    void run_tick();

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

    std::mt19937 rng_;
    bool complete_ = false;
    bool match_in_progress_ = false;
};

} // namespace neuroflyer

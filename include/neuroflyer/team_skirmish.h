#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/team_evolution.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace neuroflyer {

enum class CompetitionMode : uint8_t { RoundRobin, FreeForAll };

struct TeamSeed {
    Snapshot squad_snapshot;
    Snapshot fighter_snapshot;
    std::string squad_genome_dir;
    std::string fighter_genome_dir;
};

struct TeamSkirmishConfig {
    SkirmishConfig arena;
    CompetitionMode competition_mode = CompetitionMode::RoundRobin;
    std::vector<TeamSeed> team_seeds;
};

/// Per-ship mapping back to the team's population pools.
struct ShipAssignment {
    std::size_t team_id = 0;
    std::size_t squad_index = 0;    // index into squad_population
    std::size_t fighter_index = 0;  // index into fighter_population
};

struct TeamPool {
    std::vector<TeamIndividual> squad_population;  // NTM + squad leader pairs
    std::vector<Individual> fighter_population;

    std::vector<float> squad_scores;
    std::vector<float> fighter_scores;

    TeamSeed seed;
};

/// Per-ship scoring result from a single match.
struct TeamSkirmishMatchResult {
    std::vector<float> ship_scores;  // indexed by global ship index
    uint32_t ticks_elapsed = 0;
    bool completed = false;
};

/// Tick one frame of a team skirmish match.
/// Unlike tick_arena_match(), uses per-squad NTM/leader nets and per-ship fighter nets.
void tick_team_arena_match(
    ArenaSession& arena,
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<ShipAssignment>& assignments,
    std::vector<std::vector<neuralnet::Network>>& team_ntm_nets,
    std::vector<std::vector<neuralnet::Network>>& team_leader_nets,
    std::vector<std::vector<neuralnet::Network>>& team_fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs = nullptr,
    std::vector<std::vector<float>>* out_leader_inputs = nullptr,
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr);

/// Run a complete team skirmish match headlessly.
/// Each team has per-squad NTM/leader nets and per-ship fighter nets.
/// match_teams specifies which global team indices participate in this match.
/// Returns per-ship scores for accumulation into team pools.
[[nodiscard]] TeamSkirmishMatchResult run_team_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamPool>& team_pools,
    const std::vector<std::size_t>& match_teams,
    const std::vector<ShipAssignment>& assignments,
    uint32_t seed);

/// Build ship assignments for a match given team pools and arena config.
[[nodiscard]] std::vector<ShipAssignment> build_ship_assignments(
    const std::vector<TeamPool>& pools,
    const std::vector<std::size_t>& match_teams,
    std::size_t num_squads_per_team,
    std::size_t fighters_per_squad);

/// Manages one generation of team skirmish: match scheduling, featured + background execution.
class TeamSkirmishSession {
public:
    TeamSkirmishSession(const TeamSkirmishConfig& config,
                        const ShipDesign& fighter_design,
                        std::vector<TeamPool> team_pools,
                        uint32_t seed);

    /// Advance featured match one tick. Returns true when all matches complete.
    bool step();

    /// Run background matches within time budget (milliseconds).
    void run_background_work(int budget_ms);

    [[nodiscard]] bool is_complete() const noexcept { return complete_; }
    [[nodiscard]] const ArenaSession* current_arena() const noexcept { return arena_.get(); }
    [[nodiscard]] const std::vector<TeamPool>& team_pools() const noexcept { return team_pools_; }
    [[nodiscard]] std::vector<TeamPool>& team_pools() noexcept { return team_pools_; }

    [[nodiscard]] const std::vector<SquadLeaderFighterInputs>& last_squad_inputs() const noexcept {
        return last_sl_inputs_;
    }
    [[nodiscard]] const std::vector<std::vector<float>>& last_fighter_inputs() const noexcept {
        return last_fighter_inputs_;
    }
    [[nodiscard]] const std::vector<ShipAssignment>& ship_assignments() const noexcept {
        return assignments_;
    }
    [[nodiscard]] const std::vector<int>& ship_teams() const noexcept { return ship_teams_; }

    [[nodiscard]] neuralnet::Network* fighter_net(std::size_t ship_index) noexcept;
    [[nodiscard]] neuralnet::Network* leader_net(std::size_t ship_index) noexcept;

    [[nodiscard]] std::size_t current_match_index() const noexcept { return match_idx_; }
    [[nodiscard]] std::size_t total_matches() const noexcept { return match_schedule_.size(); }

private:
    void build_schedule();
    void start_match(const std::vector<std::size_t>& match_teams);
    void finish_match();
    void score_featured_match();
    void run_tick();

    TeamSkirmishConfig config_;
    ShipDesign fighter_design_;
    std::vector<TeamPool> team_pools_;

    // Match schedule: each entry is the set of team indices in that match
    // Round-robin: pairs, Free-for-all: one entry with all teams
    std::vector<std::vector<std::size_t>> match_schedule_;
    std::size_t match_idx_ = 0;

    // Ship assignments for current match
    std::vector<ShipAssignment> assignments_;

    // Featured match state
    std::unique_ptr<ArenaSession> arena_;
    ArenaConfig arena_config_;
    std::vector<std::vector<neuralnet::Network>> team_ntm_nets_;
    std::vector<std::vector<neuralnet::Network>> team_leader_nets_;
    std::vector<std::vector<neuralnet::Network>> team_fighter_nets_;
    std::vector<std::vector<float>> recurrent_states_;
    std::vector<int> ship_teams_;
    std::vector<SquadLeaderFighterInputs> last_sl_inputs_;
    std::vector<std::vector<float>> last_leader_inputs_;
    std::vector<std::vector<float>> last_fighter_inputs_;

    // Background match state
    bool bg_active_ = false;
    std::size_t bg_match_idx_ = 1;  // background starts at match 1

    std::mt19937 rng_;
    bool complete_ = false;
    bool match_in_progress_ = false;
    bool background_done_ = false;
};

} // namespace neuroflyer

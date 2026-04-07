# Team Skirmish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Team Skirmish mode where multiple teams co-evolve complete team brains (fighters + squad leaders + NTMs) independently against each other.

**Architecture:** New engine class `TeamSkirmishSession` manages per-team population pools and match scheduling. A new `tick_team_arena_match()` function handles per-ship/per-squad net execution (unlike existing `tick_arena_match()` which shares one net per team). Three new UI screens: config, game, and pause.

**Tech Stack:** C++20, ImGui, SDL2, GoogleTest. Reuses existing `ArenaSession`, `evolve_population()`, `evolve_squad_only()`, sensor/squad leader execution.

**Spec:** `docs/superpowers/specs/2026-04-06-team-skirmish-design.md`

---

### Task 1: Engine — TeamSkirmishConfig and TeamPool structs

**Files:**
- Create: `include/neuroflyer/team_skirmish.h`

This task creates the config structs and data types. No logic yet.

- [ ] **Step 1: Create the header with config structs**

```cpp
// include/neuroflyer/team_skirmish.h
#pragma once

#include <neuroflyer/evolution.h>
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

} // namespace neuroflyer
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -5`

Expected: Build succeeds (header-only, no new .cpp yet).

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/team_skirmish.h
git commit -m "feat(engine): add TeamSkirmishConfig and TeamPool structs"
```

---

### Task 2: Engine — tick_team_arena_match() helper

**Files:**
- Create: `src/engine/team_skirmish.cpp`
- Modify: `include/neuroflyer/team_skirmish.h` (add function declarations)
- Modify: `CMakeLists.txt:86` (add new source file)
- Modify: `tests/CMakeLists.txt:24` (add new source file)

The key difference from existing `tick_arena_match()` is per-squad NTM/leader nets and per-ship fighter nets instead of one shared net per team.

- [ ] **Step 1: Add `tick_team_arena_match` declaration to header**

Add at the bottom of `team_skirmish.h`, before the closing `}`:

```cpp
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
```

- [ ] **Step 2: Create the implementation file**

```cpp
// src/engine/team_skirmish.cpp
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>

#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

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
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs,
    std::vector<std::vector<float>>* out_leader_inputs,
    std::vector<std::vector<float>>* out_fighter_inputs) {

    const std::size_t total_ships = arena.ships().size();
    const std::size_t num_teams = team_ntm_nets.size();

    // Build sector grid
    SectorGrid grid(arena_config.world_width, arena_config.world_height,
                    arena_config.sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena.ships()[i].alive) {
            grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
        }
    }
    for (std::size_t b = 0; b < arena.bases().size(); ++b) {
        if (arena.bases()[b].alive()) {
            grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
        }
    }

    // Per-squad: run NTM + squad leader -> orders
    // Key: team_orders[team][squad] = order for that squad
    // squad_centers[team][squad] = (cx, cy)
    std::vector<std::vector<SquadLeaderOrder>> team_squad_orders(num_teams);
    std::vector<std::vector<float>> squad_center_xs(num_teams);
    std::vector<std::vector<float>> squad_center_ys(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const int team = static_cast<int>(t);
        const std::size_t num_squads = team_ntm_nets[t].size();
        team_squad_orders[t].resize(num_squads);
        squad_center_xs[t].resize(num_squads, 0.0f);
        squad_center_ys[t].resize(num_squads, 0.0f);

        for (std::size_t sq = 0; sq < num_squads; ++sq) {
            auto stats = arena.compute_squad_stats(team, static_cast<int>(sq));
            squad_center_xs[t][sq] = stats.centroid_x;
            squad_center_ys[t][sq] = stats.centroid_y;

            auto threats = gather_near_threats(
                grid, stats.centroid_x, stats.centroid_y,
                arena_config.ntm_sector_radius, team,
                arena.ships(), ship_teams, arena.bases());

            auto ntm = run_ntm_threat_selection(
                team_ntm_nets[t][sq], stats.centroid_x, stats.centroid_y,
                stats.alive_fraction, threats,
                arena_config.world_width, arena_config.world_height);

            // Find bases
            const float own_base_x = arena.bases()[t].x;
            const float own_base_y = arena.bases()[t].y;
            const float own_base_hp = arena.bases()[t].hp_normalized();
            float enemy_base_x = 0, enemy_base_y = 0;
            float min_dist_sq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                const float dx = stats.centroid_x - base.x;
                const float dy = stats.centroid_y - base.y;
                const float dsq = dx * dx + dy * dy;
                if (dsq < min_dist_sq) {
                    min_dist_sq = dsq;
                    enemy_base_x = base.x;
                    enemy_base_y = base.y;
                }
            }

            const float world_diag = std::sqrt(
                arena_config.world_width * arena_config.world_width +
                arena_config.world_height * arena_config.world_height);
            const float home_dx = own_base_x - stats.centroid_x;
            const float home_dy = own_base_y - stats.centroid_y;
            const float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
            const float home_distance = home_dist_raw / world_diag;
            const float home_heading_sin = (home_dist_raw > 1e-6f) ? home_dx / home_dist_raw : 0.0f;
            const float home_heading_cos = (home_dist_raw > 1e-6f) ? home_dy / home_dist_raw : 0.0f;
            const float cmd_dx = enemy_base_x - stats.centroid_x;
            const float cmd_dy = enemy_base_y - stats.centroid_y;
            const float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
            const float cmd_heading_sin = (cmd_dist_raw > 1e-6f) ? cmd_dx / cmd_dist_raw : 0.0f;
            const float cmd_heading_cos = (cmd_dist_raw > 1e-6f) ? cmd_dy / cmd_dist_raw : 0.0f;
            const float cmd_target_distance = cmd_dist_raw / world_diag;

            float enemy_alive_frac = 0.0f;
            std::size_t enemy_total = 0, enemy_alive = 0;
            for (std::size_t si = 0; si < total_ships; ++si) {
                if (ship_teams[si] != team) {
                    ++enemy_total;
                    if (arena.ships()[si].alive) ++enemy_alive;
                }
            }
            if (enemy_total > 0) {
                enemy_alive_frac = static_cast<float>(enemy_alive) /
                                   static_cast<float>(enemy_total);
            }

            float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
                static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

            team_squad_orders[t][sq] = run_squad_leader(
                team_leader_nets[t][sq], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world_width,
                stats.centroid_y / arena_config.world_height);

            // Store leader inputs for visualization
            if (out_leader_inputs) {
                std::size_t leader_viz_idx = t * num_squads + sq;
                if (leader_viz_idx < out_leader_inputs->size()) {
                    (*out_leader_inputs)[leader_viz_idx] = {
                        stats.alive_fraction,
                        home_heading_sin, home_heading_cos, home_distance,
                        own_base_hp,
                        cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                        ntm.active ? 1.0f : 0.0f,
                        ntm.heading_sin, ntm.heading_cos,
                        ntm.distance, ntm.threat_score,
                        enemy_alive_frac, time_remaining,
                        stats.centroid_x / arena_config.world_width,
                        stats.centroid_y / arena_config.world_height
                    };
                }
            }
        }
    }

    // Per-ship: run fighter net using per-ship assignment
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena.ships()[i].alive) continue;

        const auto& assign = assignments[i];
        const int team = static_cast<int>(assign.team_id);
        const auto sq = assign.squad_index;
        const auto fi = assign.fighter_index;

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena.ships()[i].x, arena.ships()[i].y,
            arena.ships()[i].rotation,
            team_squad_orders[assign.team_id][sq],
            squad_center_xs[assign.team_id][sq],
            squad_center_ys[assign.team_id][sq],
            arena_config.world_width, arena_config.world_height);

        if (out_sl_inputs) {
            (*out_sl_inputs)[i] = sl_inputs;
        }

        auto ctx = ArenaQueryContext::for_ship(
            arena.ships()[i], i, team,
            arena_config.world_width, arena_config.world_height,
            arena.towers(), arena.tokens(),
            arena.ships(), ship_teams, arena.bullets());

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states[i]);

        if (out_fighter_inputs && i < out_fighter_inputs->size()) {
            (*out_fighter_inputs)[i] = input;
        }

        auto output = team_fighter_nets[assign.team_id][fi].forward(
            std::span<const float>(input));

        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design.memory_slots);

        arena.set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        recurrent_states[i] = decoded.memory;
    }

    arena.tick();
}

} // namespace neuroflyer
```

- [ ] **Step 3: Register in CMakeLists.txt**

Add `src/engine/team_skirmish.cpp` after `src/engine/skirmish_tournament.cpp` (line 86) in `CMakeLists.txt`:

```
    src/engine/skirmish_tournament.cpp
    src/engine/team_skirmish.cpp
    src/engine/paths.cpp
```

Add `../src/engine/team_skirmish.cpp` after `../src/engine/skirmish_tournament.cpp` (line 43) in `tests/CMakeLists.txt`:

```
    ../src/engine/skirmish_tournament.cpp
    ../src/engine/team_skirmish.cpp
)
```

- [ ] **Step 4: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -5`

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/engine/team_skirmish.cpp include/neuroflyer/team_skirmish.h CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(engine): add tick_team_arena_match() for per-ship net execution"
```

---

### Task 3: Engine — run_team_skirmish_match() blocking match runner

**Files:**
- Modify: `include/neuroflyer/team_skirmish.h` (add declaration)
- Modify: `src/engine/team_skirmish.cpp` (add implementation)

This runs a complete match headlessly. Fork of `run_skirmish_match()` but uses per-ship nets and returns per-ship scores.

- [ ] **Step 1: Add declaration to header**

Add to `team_skirmish.h` before the closing `}`:

```cpp
/// Run a complete team skirmish match headlessly.
/// Each team has per-squad NTM/leader nets and per-ship fighter nets.
/// Returns per-ship scores for accumulation into team pools.
[[nodiscard]] TeamSkirmishMatchResult run_team_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamPool>& team_pools,
    const std::vector<ShipAssignment>& assignments,
    uint32_t seed);
```

- [ ] **Step 2: Implement the function**

Add to `src/engine/team_skirmish.cpp`:

```cpp
TeamSkirmishMatchResult run_team_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamPool>& team_pools,
    const std::vector<ShipAssignment>& assignments,
    uint32_t seed) {

    const std::size_t num_teams = team_pools.size();

    ArenaConfig arena_config = config.to_arena_config();
    arena_config.num_teams = num_teams;

    ArenaSession arena(arena_config, seed);

    // Build per-team, per-squad/per-fighter networks
    std::vector<std::vector<neuralnet::Network>> team_ntm_nets(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_leader_nets(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_fighter_nets(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const auto& pool = team_pools[t];
        team_ntm_nets[t].reserve(pool.squad_population.size());
        team_leader_nets[t].reserve(pool.squad_population.size());
        for (const auto& sq : pool.squad_population) {
            team_ntm_nets[t].push_back(sq.build_ntm_network());
            team_leader_nets[t].push_back(sq.build_squad_network());
        }
        team_fighter_nets[t].reserve(pool.fighter_population.size());
        for (const auto& fi : pool.fighter_population) {
            team_fighter_nets[t].push_back(fi.build_network());
        }
    }

    // Recurrent states and ship teams
    const std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent_states(
        total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));
    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams[i] = arena.team_of(i);
    }

    // Main loop
    while (!arena.is_over()) {
        tick_team_arena_match(arena, arena_config, fighter_design,
                              assignments, team_ntm_nets, team_leader_nets,
                              team_fighter_nets, recurrent_states, ship_teams);
    }

    // Compute per-ship scores (kill-based, same formula as skirmish)
    TeamSkirmishMatchResult result;
    result.ship_scores.resize(total_ships, 0.0f);
    result.ticks_elapsed = arena.current_tick();
    result.completed = true;

    const auto& kills = arena.enemy_kills();

    for (std::size_t i = 0; i < total_ships; ++i) {
        const int team = ship_teams[i];
        float score = 0.0f;

        // Per-ship kills
        score += config.kill_points * static_cast<float>(kills[i]);

        // Death penalty
        if (!arena.ships()[i].alive) {
            score -= config.death_points;
        }

        // Base damage: split evenly across alive fighters on the team
        // (ArenaSession tracks base damage per-base, not per-shooter)
        std::size_t alive_on_team = 0;
        for (std::size_t j = 0; j < total_ships; ++j) {
            if (ship_teams[j] == team && arena.ships()[j].alive) {
                ++alive_on_team;
            }
        }
        // Also count dead ships for fair distribution
        std::size_t total_on_team = 0;
        for (std::size_t j = 0; j < total_ships; ++j) {
            if (ship_teams[j] == team) ++total_on_team;
        }

        if (total_on_team > 0) {
            float team_base_score = 0.0f;
            for (const auto& base : arena.bases()) {
                float damage_dealt = base.max_hp - base.hp;
                if (base.team_id == team) {
                    if (damage_dealt > 0.0f && config.base_bullet_damage > 0.0f) {
                        float hits = damage_dealt / config.base_bullet_damage;
                        team_base_score -= config.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score -= config.base_kill_points();
                    }
                } else {
                    if (damage_dealt > 0.0f && config.base_bullet_damage > 0.0f) {
                        float hits = damage_dealt / config.base_bullet_damage;
                        team_base_score += config.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score += config.base_kill_points();
                    }
                }
            }
            score += team_base_score / static_cast<float>(total_on_team);
        }

        result.ship_scores[i] = score;
    }

    return result;
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -5`

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/neuroflyer/team_skirmish.h src/engine/team_skirmish.cpp
git commit -m "feat(engine): add run_team_skirmish_match() blocking match runner"
```

---

### Task 4: Engine — TeamSkirmishSession class

**Files:**
- Modify: `include/neuroflyer/team_skirmish.h` (add class declaration)
- Modify: `src/engine/team_skirmish.cpp` (add class implementation)

The session orchestrates featured + background matches for one generation.

- [ ] **Step 1: Add class declaration to header**

Add to `team_skirmish.h` before the function declarations:

```cpp
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
    void build_assignments();
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

/// Build ship assignments for a match given team pools and arena config.
[[nodiscard]] std::vector<ShipAssignment> build_ship_assignments(
    const std::vector<TeamPool>& pools,
    const std::vector<std::size_t>& match_teams,
    std::size_t num_squads_per_team,
    std::size_t fighters_per_squad);
```

- [ ] **Step 2: Implement the session class**

Add to `src/engine/team_skirmish.cpp`:

```cpp
std::vector<ShipAssignment> build_ship_assignments(
    const std::vector<TeamPool>& pools,
    const std::vector<std::size_t>& match_teams,
    std::size_t num_squads_per_team,
    std::size_t fighters_per_squad) {

    const std::size_t ships_per_team = num_squads_per_team * fighters_per_squad;
    const std::size_t total_ships = match_teams.size() * ships_per_team;
    std::vector<ShipAssignment> assignments(total_ships);

    for (std::size_t t = 0; t < match_teams.size(); ++t) {
        const std::size_t team_id = match_teams[t];
        for (std::size_t sq = 0; sq < num_squads_per_team; ++sq) {
            for (std::size_t f = 0; f < fighters_per_squad; ++f) {
                std::size_t ship_idx = t * ships_per_team + sq * fighters_per_squad + f;
                assignments[ship_idx].team_id = team_id;
                assignments[ship_idx].squad_index = sq;
                assignments[ship_idx].fighter_index = sq * fighters_per_squad + f;
            }
        }
    }
    return assignments;
}

TeamSkirmishSession::TeamSkirmishSession(
    const TeamSkirmishConfig& config,
    const ShipDesign& fighter_design,
    std::vector<TeamPool> team_pools,
    uint32_t seed)
    : config_(config)
    , fighter_design_(fighter_design)
    , team_pools_(std::move(team_pools))
    , rng_(seed) {

    build_schedule();

    if (match_schedule_.empty()) {
        complete_ = true;
        return;
    }

    // Start featured match (index 0)
    start_match(match_schedule_[0]);
}

void TeamSkirmishSession::build_schedule() {
    const std::size_t num_teams = team_pools_.size();

    if (config_.competition_mode == CompetitionMode::FreeForAll) {
        // One match with all teams
        std::vector<std::size_t> all_teams;
        for (std::size_t t = 0; t < num_teams; ++t) all_teams.push_back(t);
        match_schedule_.push_back(std::move(all_teams));
    } else {
        // Round-robin: all unique pairs
        for (std::size_t a = 0; a < num_teams; ++a) {
            for (std::size_t b = a + 1; b < num_teams; ++b) {
                match_schedule_.push_back({a, b});
            }
        }
    }

    bg_match_idx_ = 1;
    background_done_ = (match_schedule_.size() <= 1);
}

void TeamSkirmishSession::start_match(const std::vector<std::size_t>& match_teams) {
    arena_config_ = config_.arena.to_arena_config();
    arena_config_.num_teams = match_teams.size();

    arena_ = std::make_unique<ArenaSession>(arena_config_,
                                             static_cast<uint32_t>(rng_()));

    // Build assignments
    assignments_ = build_ship_assignments(
        team_pools_, match_teams,
        config_.arena.num_squads_per_team,
        config_.arena.fighters_per_squad);

    // Build per-team nets (only for teams in this match)
    const std::size_t teams_in_match = match_teams.size();
    team_ntm_nets_.resize(teams_in_match);
    team_leader_nets_.resize(teams_in_match);
    team_fighter_nets_.resize(teams_in_match);

    for (std::size_t mt = 0; mt < teams_in_match; ++mt) {
        const auto& pool = team_pools_[match_teams[mt]];

        team_ntm_nets_[mt].clear();
        team_leader_nets_[mt].clear();
        for (const auto& sq : pool.squad_population) {
            team_ntm_nets_[mt].push_back(sq.build_ntm_network());
            team_leader_nets_[mt].push_back(sq.build_squad_network());
        }

        team_fighter_nets_[mt].clear();
        for (const auto& fi : pool.fighter_population) {
            team_fighter_nets_[mt].push_back(fi.build_network());
        }
    }

    // Init recurrent states and ship teams
    const std::size_t total_ships = arena_->ships().size();
    recurrent_states_.assign(total_ships,
        std::vector<float>(fighter_design_.memory_slots, 0.0f));
    ship_teams_.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams_[i] = arena_->team_of(i);
    }

    last_sl_inputs_.assign(total_ships, SquadLeaderFighterInputs{});
    const std::size_t total_squads = teams_in_match * config_.arena.num_squads_per_team;
    last_leader_inputs_.assign(total_squads, std::vector<float>(17, 0.0f));
    last_fighter_inputs_.assign(total_ships, std::vector<float>{});

    match_in_progress_ = true;
}

void TeamSkirmishSession::run_tick() {
    assert(arena_);
    tick_team_arena_match(*arena_, arena_config_, fighter_design_,
                          assignments_, team_ntm_nets_, team_leader_nets_,
                          team_fighter_nets_, recurrent_states_, ship_teams_,
                          &last_sl_inputs_, &last_leader_inputs_, &last_fighter_inputs_);
}

void TeamSkirmishSession::score_featured_match() {
    assert(arena_ && arena_->is_over());

    const auto& match_teams = match_schedule_[match_idx_];
    const std::size_t total_ships = arena_->ships().size();
    const auto& kills = arena_->enemy_kills();

    // Compute per-ship scores (same formula as run_team_skirmish_match)
    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto& assign = assignments_[i];
        const int team = ship_teams_[i];
        float score = 0.0f;

        score += config_.arena.kill_points * static_cast<float>(kills[i]);

        if (!arena_->ships()[i].alive) {
            score -= config_.arena.death_points;
        }

        // Base damage split across team
        std::size_t total_on_team = 0;
        for (std::size_t j = 0; j < total_ships; ++j) {
            if (ship_teams_[j] == team) ++total_on_team;
        }

        if (total_on_team > 0) {
            float team_base_score = 0.0f;
            for (const auto& base : arena_->bases()) {
                float damage_dealt = base.max_hp - base.hp;
                if (base.team_id == team) {
                    if (damage_dealt > 0.0f && config_.arena.base_bullet_damage > 0.0f) {
                        float hits = damage_dealt / config_.arena.base_bullet_damage;
                        team_base_score -= config_.arena.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score -= config_.arena.base_kill_points();
                    }
                } else {
                    if (damage_dealt > 0.0f && config_.arena.base_bullet_damage > 0.0f) {
                        float hits = damage_dealt / config_.arena.base_bullet_damage;
                        team_base_score += config_.arena.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score += config_.arena.base_kill_points();
                    }
                }
            }
            score += team_base_score / static_cast<float>(total_on_team);
        }

        // Accumulate to the correct team pool
        team_pools_[assign.team_id].fighter_scores[assign.fighter_index] += score;
    }

    // Accumulate squad scores (sum of fighters' match scores per squad)
    for (std::size_t t_idx = 0; t_idx < match_teams.size(); ++t_idx) {
        const std::size_t team_id = match_teams[t_idx];
        auto& pool = team_pools_[team_id];
        const std::size_t num_squads = pool.squad_population.size();
        const std::size_t fpq = config_.arena.fighters_per_squad;

        for (std::size_t sq = 0; sq < num_squads; ++sq) {
            float squad_sum = 0.0f;
            for (std::size_t f = 0; f < fpq; ++f) {
                std::size_t fi = sq * fpq + f;
                // Find the ship with this assignment
                for (std::size_t i = 0; i < total_ships; ++i) {
                    if (assignments_[i].team_id == team_id &&
                        assignments_[i].fighter_index == fi) {
                        const auto& kills_vec = arena_->enemy_kills();
                        float ship_score = config_.arena.kill_points *
                            static_cast<float>(kills_vec[i]);
                        if (!arena_->ships()[i].alive) {
                            ship_score -= config_.arena.death_points;
                        }
                        squad_sum += ship_score;
                        break;
                    }
                }
            }
            pool.squad_scores[sq] += squad_sum;
        }
    }
}

void TeamSkirmishSession::finish_match() {
    score_featured_match();

    arena_.reset();
    team_ntm_nets_.clear();
    team_leader_nets_.clear();
    team_fighter_nets_.clear();
    recurrent_states_.clear();
    ship_teams_.clear();
    match_in_progress_ = false;

    ++match_idx_;
    if (match_idx_ >= match_schedule_.size()) {
        complete_ = true;
    }
}

bool TeamSkirmishSession::step() {
    if (complete_) return true;

    if (!match_in_progress_) {
        if (match_idx_ < match_schedule_.size()) {
            start_match(match_schedule_[match_idx_]);
            return false;
        }
        complete_ = true;
        return true;
    }

    if (arena_ && !arena_->is_over()) {
        run_tick();
        return false;
    }

    if (arena_ && arena_->is_over()) {
        finish_match();
        return complete_;
    }

    return complete_;
}

void TeamSkirmishSession::run_background_work(int budget_ms) {
    if (background_done_) return;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(budget_ms);

    while (!background_done_) {
        if (bg_match_idx_ >= match_schedule_.size()) {
            background_done_ = true;
            break;
        }

        // Run one background match to completion
        auto result = run_team_skirmish_match(
            config_.arena, fighter_design_, team_pools_,
            build_ship_assignments(team_pools_, match_schedule_[bg_match_idx_],
                                   config_.arena.num_squads_per_team,
                                   config_.arena.fighters_per_squad),
            static_cast<uint32_t>(rng_()));

        // Accumulate scores from background match
        const auto& match_teams = match_schedule_[bg_match_idx_];
        const std::size_t ships_per_team =
            config_.arena.num_squads_per_team * config_.arena.fighters_per_squad;
        auto bg_assignments = build_ship_assignments(
            team_pools_, match_teams,
            config_.arena.num_squads_per_team,
            config_.arena.fighters_per_squad);

        for (std::size_t i = 0; i < result.ship_scores.size(); ++i) {
            const auto& assign = bg_assignments[i];
            team_pools_[assign.team_id].fighter_scores[assign.fighter_index] +=
                result.ship_scores[i];
        }

        // Squad scores from background match
        for (std::size_t t_idx = 0; t_idx < match_teams.size(); ++t_idx) {
            const std::size_t team_id = match_teams[t_idx];
            auto& pool = team_pools_[team_id];
            const std::size_t num_squads = pool.squad_population.size();
            const std::size_t fpq = config_.arena.fighters_per_squad;

            for (std::size_t sq = 0; sq < num_squads; ++sq) {
                float squad_sum = 0.0f;
                for (std::size_t f = 0; f < fpq; ++f) {
                    std::size_t fi = sq * fpq + f;
                    for (std::size_t i = 0; i < bg_assignments.size(); ++i) {
                        if (bg_assignments[i].team_id == team_id &&
                            bg_assignments[i].fighter_index == fi) {
                            squad_sum += result.ship_scores[i];
                            break;
                        }
                    }
                }
                pool.squad_scores[sq] += squad_sum;
            }
        }

        ++bg_match_idx_;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }

    if (bg_match_idx_ >= match_schedule_.size()) {
        background_done_ = true;
    }
}

neuralnet::Network* TeamSkirmishSession::fighter_net(std::size_t ship_index) noexcept {
    if (ship_index >= assignments_.size()) return nullptr;
    const auto& a = assignments_[ship_index];
    // a.team_id is the global team index but team_fighter_nets_ is indexed by
    // match-local team index. Find the match-local index.
    if (match_idx_ >= match_schedule_.size()) return nullptr;
    const auto& match_teams = match_schedule_[match_idx_];
    for (std::size_t mt = 0; mt < match_teams.size(); ++mt) {
        if (match_teams[mt] == a.team_id) {
            if (a.fighter_index < team_fighter_nets_[mt].size()) {
                return &team_fighter_nets_[mt][a.fighter_index];
            }
        }
    }
    return nullptr;
}

neuralnet::Network* TeamSkirmishSession::leader_net(std::size_t ship_index) noexcept {
    if (ship_index >= assignments_.size()) return nullptr;
    const auto& a = assignments_[ship_index];
    if (match_idx_ >= match_schedule_.size()) return nullptr;
    const auto& match_teams = match_schedule_[match_idx_];
    for (std::size_t mt = 0; mt < match_teams.size(); ++mt) {
        if (match_teams[mt] == a.team_id) {
            if (a.squad_index < team_leader_nets_[mt].size()) {
                return &team_leader_nets_[mt][a.squad_index];
            }
        }
    }
    return nullptr;
}
```

Add `#include <chrono>` to the includes at the top of `team_skirmish.cpp`.

- [ ] **Step 3: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -5`

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/neuroflyer/team_skirmish.h src/engine/team_skirmish.cpp
git commit -m "feat(engine): add TeamSkirmishSession class with match scheduling"
```

---

### Task 5: Tests — team_skirmish_test.cpp

**Files:**
- Create: `tests/team_skirmish_test.cpp`
- Modify: `tests/CMakeLists.txt` (register test)

- [ ] **Step 1: Write the test file**

```cpp
// tests/team_skirmish_test.cpp
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/team_evolution.h>

#include <gtest/gtest.h>

#include <random>

namespace neuroflyer {
namespace {

class TeamSkirmishTest : public ::testing::Test {
protected:
    void SetUp() override {
        rng_.seed(42);
        design_.sensors.clear();
        design_.memory_slots = 0;
    }

    ShipDesign design_;
    std::mt19937 rng_;

    TeamPool make_pool(std::size_t num_squads, std::size_t num_fighters) {
        TeamPool pool;
        pool.squad_population.reserve(num_squads);
        for (std::size_t i = 0; i < num_squads; ++i) {
            pool.squad_population.push_back(
                TeamIndividual::create(design_, {8},
                    NtmNetConfig{}, SquadLeaderNetConfig{}, rng_));
        }
        pool.fighter_population.reserve(num_fighters);
        for (std::size_t i = 0; i < num_fighters; ++i) {
            pool.fighter_population.push_back(
                Individual::from_design(design_, {8}, rng_));
        }
        pool.squad_scores.assign(num_squads, 0.0f);
        pool.fighter_scores.assign(num_fighters, 0.0f);
        return pool;
    }
};

TEST_F(TeamSkirmishTest, BuildShipAssignments_CorrectSize) {
    auto pool = make_pool(2, 8);
    std::vector<TeamPool> pools = {pool, pool};
    std::vector<std::size_t> match_teams = {0, 1};

    auto assignments = build_ship_assignments(pools, match_teams, 2, 4);

    // 2 teams * 2 squads * 4 fighters = 16 ships
    ASSERT_EQ(assignments.size(), 16u);
}

TEST_F(TeamSkirmishTest, BuildShipAssignments_CorrectMapping) {
    auto pool = make_pool(2, 8);
    std::vector<TeamPool> pools = {pool, pool};
    std::vector<std::size_t> match_teams = {0, 1};

    auto assignments = build_ship_assignments(pools, match_teams, 2, 4);

    // First 8 ships belong to team 0
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(assignments[i].team_id, 0u);
    }
    // Last 8 ships belong to team 1
    for (std::size_t i = 8; i < 16; ++i) {
        EXPECT_EQ(assignments[i].team_id, 1u);
    }

    // Squad assignments
    EXPECT_EQ(assignments[0].squad_index, 0u);  // team 0, squad 0
    EXPECT_EQ(assignments[4].squad_index, 1u);  // team 0, squad 1
    EXPECT_EQ(assignments[8].squad_index, 0u);  // team 1, squad 0
}

TEST_F(TeamSkirmishTest, RunTeamSkirmishMatch_Completes) {
    auto pool = make_pool(1, 4);
    std::vector<TeamPool> pools = {pool, pool};

    SkirmishConfig config;
    config.num_squads_per_team = 1;
    config.fighters_per_squad = 4;
    config.time_limit_ticks = 120;
    config.world_width = 2000.0f;
    config.world_height = 2000.0f;
    config.tower_count = 0;
    config.token_count = 0;

    auto assignments = build_ship_assignments(
        pools, {0, 1}, config.num_squads_per_team, config.fighters_per_squad);

    auto result = run_team_skirmish_match(config, design_, pools, assignments, 42);

    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.ship_scores.size(), 8u);  // 2 teams * 1 squad * 4 fighters
}

TEST_F(TeamSkirmishTest, SessionCompletes_RoundRobin) {
    auto pool = make_pool(1, 4);

    TeamSkirmishConfig config;
    config.arena.num_squads_per_team = 1;
    config.arena.fighters_per_squad = 4;
    config.arena.time_limit_ticks = 60;
    config.arena.world_width = 2000.0f;
    config.arena.world_height = 2000.0f;
    config.arena.tower_count = 0;
    config.arena.token_count = 0;
    config.competition_mode = CompetitionMode::RoundRobin;
    // 3 teams = 3 matchups
    config.team_seeds.resize(3);

    std::vector<TeamPool> pools = {pool, pool, pool};

    TeamSkirmishSession session(config, design_, std::move(pools), 42);

    EXPECT_EQ(session.total_matches(), 3u);

    int max_ticks = 100000;
    while (!session.is_complete() && max_ticks-- > 0) {
        session.step();
        session.run_background_work(5);
    }

    EXPECT_TRUE(session.is_complete());
}

TEST_F(TeamSkirmishTest, SessionCompletes_FreeForAll) {
    auto pool = make_pool(1, 4);

    TeamSkirmishConfig config;
    config.arena.num_squads_per_team = 1;
    config.arena.fighters_per_squad = 4;
    config.arena.time_limit_ticks = 60;
    config.arena.world_width = 2000.0f;
    config.arena.world_height = 2000.0f;
    config.arena.tower_count = 0;
    config.arena.token_count = 0;
    config.competition_mode = CompetitionMode::FreeForAll;
    config.team_seeds.resize(3);

    std::vector<TeamPool> pools = {pool, pool, pool};

    TeamSkirmishSession session(config, design_, std::move(pools), 42);

    EXPECT_EQ(session.total_matches(), 1u);

    int max_ticks = 100000;
    while (!session.is_complete() && max_ticks-- > 0) {
        session.step();
    }

    EXPECT_TRUE(session.is_complete());
}

} // namespace
} // namespace neuroflyer
```

- [ ] **Step 2: Register the test in CMakeLists.txt**

Add `team_skirmish_test.cpp` after `skirmish_tournament_test.cpp` (line 25) in `tests/CMakeLists.txt`:

```
    skirmish_tournament_test.cpp
    team_skirmish_test.cpp
```

- [ ] **Step 3: Build and run tests**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer_tests 2>&1 | tail -10`

Then: `./build/tests/neuroflyer_tests --gtest_filter="TeamSkirmish*" 2>&1`

Expected: All 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/team_skirmish_test.cpp tests/CMakeLists.txt
git commit -m "test(engine): add team skirmish session and match tests"
```

---

### Task 6: UI — TeamSkirmishConfigScreen

**Files:**
- Create: `include/neuroflyer/ui/screens/team_skirmish_config_screen.h`
- Create: `src/ui/screens/game/team_skirmish_config_screen.cpp`
- Modify: `CMakeLists.txt` (add source file)

Config screen with per-team net picker + arena/scoring/evolution params + competition mode toggle.

- [ ] **Step 1: Create the header**

```cpp
// include/neuroflyer/ui/screens/team_skirmish_config_screen.h
#pragma once

#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/ui/ui_screen.h>

#include <string>
#include <vector>

namespace neuroflyer {

class TeamSkirmishConfigScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmishConfig"; }

private:
    void refresh_genome_list(AppState& state);
    void load_variants_for_genome(std::size_t genome_idx);

    TeamSkirmishConfig config_;
    EvolutionConfig evo_config_;
    int num_teams_ = 2;

    // Per-team selection state
    struct TeamSelection {
        int squad_genome_idx = -1;
        int squad_variant_idx = -1;
        int fighter_genome_idx = -1;
        int fighter_variant_idx = -1;
    };
    std::vector<TeamSelection> team_selections_;

    // Genome/variant lists (shared across all team selectors)
    std::vector<GenomeInfo> genomes_;
    std::vector<std::string> genome_names_;
    // Per-genome variant lists loaded on demand
    struct GenomeVariants {
        std::vector<SnapshotHeader> regular;  // solo/fighter variants
        std::vector<SnapshotHeader> squad;    // squad leader variants
        bool loaded = false;
    };
    std::vector<GenomeVariants> genome_variants_;
    bool genomes_loaded_ = false;

    std::string genomes_dir_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create the implementation**

```cpp
// src/ui/screens/game/team_skirmish_config_screen.cpp
#include <neuroflyer/ui/screens/team_skirmish_config_screen.h>
#include <neuroflyer/ui/screens/team_skirmish_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <algorithm>
#include <memory>

namespace neuroflyer {

void TeamSkirmishConfigScreen::refresh_genome_list(AppState& state) {
    genomes_dir_ = state.data_dir + "/genomes";
    genomes_ = list_genomes(genomes_dir_);
    genome_names_.clear();
    genome_variants_.clear();
    for (const auto& g : genomes_) {
        genome_names_.push_back(g.name);
        genome_variants_.push_back({});
    }
    genomes_loaded_ = true;
}

void TeamSkirmishConfigScreen::load_variants_for_genome(std::size_t genome_idx) {
    if (genome_idx >= genome_variants_.size()) return;
    auto& gv = genome_variants_[genome_idx];
    if (gv.loaded) return;

    std::string genome_dir = genomes_dir_ + "/" + genomes_[genome_idx].name;
    auto variants = list_variants(genome_dir);
    auto squad_variants = list_squad_variants(genome_dir);

    gv.regular.clear();
    for (const auto& v : variants) {
        if (v.net_type == NetType::Solo || v.net_type == NetType::Fighter) {
            gv.regular.push_back(v);
        }
    }
    gv.squad.clear();
    for (const auto& v : squad_variants) {
        if (v.net_type == NetType::SquadLeader) {
            gv.squad.push_back(v);
        }
    }
    gv.loaded = true;
}

void TeamSkirmishConfigScreen::on_draw(AppState& state, Renderer& /*renderer*/,
                                        UIManager& ui) {
    if (!genomes_loaded_) {
        refresh_genome_list(state);
        team_selections_.resize(static_cast<std::size_t>(num_teams_));
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::Begin("##TeamSkirmishConfig", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("Team Skirmish \xe2\x80\x94 Configuration");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Teams ---
    ui::section_header("Teams");

    int prev_teams = num_teams_;
    if (ui::input_int("Number of Teams", &num_teams_, 2, 8)) {
        num_teams_ = std::clamp(num_teams_, 2, 8);
        team_selections_.resize(static_cast<std::size_t>(num_teams_));
    }

    // Competition mode
    int mode = static_cast<int>(config_.competition_mode);
    ImGui::RadioButton("Round Robin", &mode, 0); ImGui::SameLine();
    ImGui::RadioButton("Free-for-All", &mode, 1);
    config_.competition_mode = static_cast<CompetitionMode>(mode);
    ImGui::Spacing();

    // Per-team net selection
    for (int t = 0; t < num_teams_; ++t) {
        ImGui::PushID(t);
        char team_label[32];
        std::snprintf(team_label, sizeof(team_label), "Team %d", t + 1);
        if (ImGui::TreeNodeEx(team_label, ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sel = team_selections_[static_cast<std::size_t>(t)];

            // Squad genome/variant picker
            ImGui::Text("Squad Net:");
            ImGui::SameLine();
            if (!genome_names_.empty()) {
                // Genome combo for squad
                const char* squad_genome_preview = (sel.squad_genome_idx >= 0 &&
                    sel.squad_genome_idx < static_cast<int>(genome_names_.size()))
                    ? genome_names_[static_cast<std::size_t>(sel.squad_genome_idx)].c_str()
                    : "<select genome>";
                if (ImGui::BeginCombo("##squad_genome", squad_genome_preview)) {
                    for (int g = 0; g < static_cast<int>(genome_names_.size()); ++g) {
                        load_variants_for_genome(static_cast<std::size_t>(g));
                        if (!genome_variants_[static_cast<std::size_t>(g)].squad.empty()) {
                            if (ImGui::Selectable(genome_names_[static_cast<std::size_t>(g)].c_str(),
                                                  g == sel.squad_genome_idx)) {
                                sel.squad_genome_idx = g;
                                sel.squad_variant_idx = 0;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // Squad variant combo
                if (sel.squad_genome_idx >= 0) {
                    load_variants_for_genome(static_cast<std::size_t>(sel.squad_genome_idx));
                    const auto& squad_vars = genome_variants_[
                        static_cast<std::size_t>(sel.squad_genome_idx)].squad;
                    if (!squad_vars.empty()) {
                        ImGui::SameLine();
                        const char* squad_var_preview = (sel.squad_variant_idx >= 0 &&
                            sel.squad_variant_idx < static_cast<int>(squad_vars.size()))
                            ? squad_vars[static_cast<std::size_t>(sel.squad_variant_idx)].name.c_str()
                            : "<select variant>";
                        if (ImGui::BeginCombo("##squad_var", squad_var_preview)) {
                            for (int v = 0; v < static_cast<int>(squad_vars.size()); ++v) {
                                if (ImGui::Selectable(squad_vars[static_cast<std::size_t>(v)].name.c_str(),
                                                      v == sel.squad_variant_idx)) {
                                    sel.squad_variant_idx = v;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
            }

            // Fighter genome/variant picker
            ImGui::Text("Fighter Net:");
            ImGui::SameLine();
            if (!genome_names_.empty()) {
                const char* fighter_genome_preview = (sel.fighter_genome_idx >= 0 &&
                    sel.fighter_genome_idx < static_cast<int>(genome_names_.size()))
                    ? genome_names_[static_cast<std::size_t>(sel.fighter_genome_idx)].c_str()
                    : "<select genome>";
                if (ImGui::BeginCombo("##fighter_genome", fighter_genome_preview)) {
                    for (int g = 0; g < static_cast<int>(genome_names_.size()); ++g) {
                        load_variants_for_genome(static_cast<std::size_t>(g));
                        if (!genome_variants_[static_cast<std::size_t>(g)].regular.empty()) {
                            if (ImGui::Selectable(genome_names_[static_cast<std::size_t>(g)].c_str(),
                                                  g == sel.fighter_genome_idx)) {
                                sel.fighter_genome_idx = g;
                                sel.fighter_variant_idx = 0;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                if (sel.fighter_genome_idx >= 0) {
                    load_variants_for_genome(static_cast<std::size_t>(sel.fighter_genome_idx));
                    const auto& fighter_vars = genome_variants_[
                        static_cast<std::size_t>(sel.fighter_genome_idx)].regular;
                    if (!fighter_vars.empty()) {
                        ImGui::SameLine();
                        const char* fighter_var_preview = (sel.fighter_variant_idx >= 0 &&
                            sel.fighter_variant_idx < static_cast<int>(fighter_vars.size()))
                            ? fighter_vars[static_cast<std::size_t>(sel.fighter_variant_idx)].name.c_str()
                            : "<select variant>";
                        if (ImGui::BeginCombo("##fighter_var", fighter_var_preview)) {
                            for (int v = 0; v < static_cast<int>(fighter_vars.size()); ++v) {
                                if (ImGui::Selectable(fighter_vars[static_cast<std::size_t>(v)].name.c_str(),
                                                      v == sel.fighter_variant_idx)) {
                                    sel.fighter_variant_idx = v;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();

    // --- Arena (reuse from SkirmishConfigScreen) ---
    ui::section_header("Arena");
    ui::slider_float("World Width", &config_.arena.world_width, 1000.0f, 10000.0f);
    ui::slider_float("World Height", &config_.arena.world_height, 1000.0f, 10000.0f);

    int squads = static_cast<int>(config_.arena.num_squads_per_team);
    if (ui::input_int("Squads per Team", &squads, 1, 4)) {
        config_.arena.num_squads_per_team = static_cast<std::size_t>(squads);
    }
    int fighters = static_cast<int>(config_.arena.fighters_per_squad);
    if (ui::input_int("Fighters per Squad", &fighters, 2, 20)) {
        config_.arena.fighters_per_squad = static_cast<std::size_t>(fighters);
    }
    int towers = static_cast<int>(config_.arena.tower_count);
    if (ui::input_int("Towers", &towers, 0, 200)) {
        config_.arena.tower_count = static_cast<std::size_t>(towers);
    }
    int tokens = static_cast<int>(config_.arena.token_count);
    if (ui::input_int("Tokens", &tokens, 0, 200)) {
        config_.arena.token_count = static_cast<std::size_t>(tokens);
    }
    int time_seconds = static_cast<int>(config_.arena.time_limit_ticks / 60);
    if (ui::input_int("Time Limit (seconds)", &time_seconds, 10, 300)) {
        config_.arena.time_limit_ticks = static_cast<uint32_t>(time_seconds) * 60;
    }
    ImGui::Spacing();

    // --- Bases ---
    ui::section_header("Bases");
    ui::slider_float("Base HP", &config_.arena.base_hp, 100.0f, 10000.0f);
    ui::slider_float("Base Radius", &config_.arena.base_radius, 20.0f, 300.0f);
    ui::slider_float("Base Bullet Damage", &config_.arena.base_bullet_damage, 1.0f, 100.0f);
    ImGui::Spacing();

    // --- Physics ---
    ui::section_header("Physics");
    ui::slider_float("Rotation Speed", &config_.arena.rotation_speed, 0.01f, 0.2f);
    ui::slider_float("Bullet Max Range", &config_.arena.bullet_max_range, 200.0f, 3000.0f);
    ui::checkbox("Wrap N/S", &config_.arena.wrap_ns);
    ui::checkbox("Wrap E/W", &config_.arena.wrap_ew);
    ui::checkbox("Friendly Fire", &config_.arena.friendly_fire);
    ImGui::Spacing();

    // --- Scoring ---
    ui::section_header("Scoring");
    ui::slider_float("Kill Points", &config_.arena.kill_points, 10.0f, 1000.0f);
    ui::slider_float("Death Points", &config_.arena.death_points, 0.0f, 500.0f);
    ui::slider_float("Base Hit Points", &config_.arena.base_hit_points, 0.0f, 500.0f);
    ImGui::TextDisabled("Base Kill Points: %.0f", config_.arena.base_kill_points());
    ImGui::Spacing();

    // --- Buttons ---
    ImGui::Separator();
    ImGui::Spacing();

    // Validate: all teams must have both nets selected
    bool valid = true;
    for (int t = 0; t < num_teams_; ++t) {
        const auto& sel = team_selections_[static_cast<std::size_t>(t)];
        if (sel.squad_genome_idx < 0 || sel.squad_variant_idx < 0 ||
            sel.fighter_genome_idx < 0 || sel.fighter_variant_idx < 0) {
            valid = false;
            break;
        }
    }

    if (!valid) ImGui::BeginDisabled();
    if (ui::button("Start", ui::ButtonStyle::Primary, 200.0f)) {
        // Build team seeds by loading snapshots
        config_.team_seeds.clear();
        for (int t = 0; t < num_teams_; ++t) {
            const auto& sel = team_selections_[static_cast<std::size_t>(t)];

            TeamSeed seed;
            std::string squad_genome_dir = genomes_dir_ + "/" +
                genomes_[static_cast<std::size_t>(sel.squad_genome_idx)].name;
            std::string fighter_genome_dir = genomes_dir_ + "/" +
                genomes_[static_cast<std::size_t>(sel.fighter_genome_idx)].name;

            const auto& squad_vars = genome_variants_[
                static_cast<std::size_t>(sel.squad_genome_idx)].squad;
            const auto& fighter_vars = genome_variants_[
                static_cast<std::size_t>(sel.fighter_genome_idx)].regular;

            std::string squad_path = squad_genome_dir + "/squad/" +
                squad_vars[static_cast<std::size_t>(sel.squad_variant_idx)].name + ".bin";
            std::string fighter_path = fighter_genome_dir + "/" +
                fighter_vars[static_cast<std::size_t>(sel.fighter_variant_idx)].name + ".bin";

            seed.squad_snapshot = load_snapshot(squad_path);
            seed.fighter_snapshot = load_snapshot(fighter_path);
            seed.squad_genome_dir = squad_genome_dir;
            seed.fighter_genome_dir = fighter_genome_dir;

            config_.team_seeds.push_back(std::move(seed));
        }

        ui.push_screen(std::make_unique<TeamSkirmishScreen>(config_, evo_config_));
    }
    if (!valid) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ui::button("Back", ui::ButtonStyle::Secondary, 120.0f)) {
        ui.pop_screen();
    }

    ImGui::End();

    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

} // namespace neuroflyer
```

- [ ] **Step 3: Register in CMakeLists.txt**

Add after `src/ui/screens/game/skirmish_pause_screen.cpp` (line 125):

```
    src/ui/screens/game/team_skirmish_config_screen.cpp
```

- [ ] **Step 4: Add load_snapshot declaration if missing**

Check that `load_snapshot(path)` is available. It's in `snapshot_io.h`:

```cpp
[[nodiscard]] Snapshot load_snapshot(const std::string& path);
```

If the config screen can't find it, add `#include <neuroflyer/snapshot_io.h>` — which is already in the header.

- [ ] **Step 5: Verify it compiles** (will fail until TeamSkirmishScreen exists — that's Task 7)

Note: This will not compile yet because `TeamSkirmishScreen` doesn't exist. Create a minimal stub in the next task, then come back to verify.

- [ ] **Step 6: Commit** (combine with Task 7)

---

### Task 7: UI — TeamSkirmishScreen (game screen)

**Files:**
- Create: `include/neuroflyer/ui/screens/team_skirmish_screen.h`
- Create: `src/ui/screens/game/team_skirmish_screen.cpp`
- Modify: `CMakeLists.txt` (add source file)

The main game screen. Drives the generation loop, renders the featured match, handles evolution between generations.

- [ ] **Step 1: Create the header**

```cpp
// include/neuroflyer/ui/screens/team_skirmish_screen.h
#pragma once

#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/ui_screen.h>

#include <neuroflyer/renderers/variant_net_render.h>
#include <neuralnet-ui/net_viewer_view.h>

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

namespace neuroflyer {

class TeamSkirmishScreen : public UIScreen {
public:
    TeamSkirmishScreen(TeamSkirmishConfig config, EvolutionConfig evo_config);
    ~TeamSkirmishScreen() override;

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmish"; }

private:
    void initialize(AppState& state);
    bool handle_input(UIManager& ui);
    void evolve_generation();
    void render_world(Renderer& renderer);
    void render_hud();
    void update_net_viewer(Renderer& renderer);

    TeamSkirmishConfig config_;
    EvolutionConfig evo_config_;
    ShipDesign ship_design_;

    std::unique_ptr<TeamSkirmishSession> session_;
    std::vector<TeamPool> team_pools_;

    Camera camera_;
    enum class CameraMode { Swarm, Follow };
    CameraMode camera_mode_ = CameraMode::Swarm;

    bool initialized_ = false;
    bool paused_ = false;
    std::size_t generation_ = 1;
    int ticks_per_frame_ = 1;
    int selected_ship_ = -1;

    enum class FollowNetView { Fighter, SquadLeader };
    FollowNetView follow_net_view_ = FollowNetView::Fighter;
    NetViewerViewState net_viewer_state_{};
    std::vector<float> last_fighter_input_;
    std::vector<float> last_leader_input_;

    std::mt19937 rng_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create the implementation**

This is a large file. Model it closely on `SkirmishScreen` but with `TeamSkirmishSession` instead of `SkirmishTournament`, and per-team pool evolution. The screen will reuse the SDL drawing helpers (duplicated, matching existing pattern). Create the file at `src/ui/screens/game/team_skirmish_screen.cpp`.

Key differences from `SkirmishScreen`:
- `initialize()` builds `TeamPool` per team from config seeds (mutating from seed snapshots)
- `evolve_generation()` calls `evolve_population()` on each team's fighter pool AND `evolve_squad_only()` on each team's squad pool
- `render_hud()` shows per-team stats
- `handle_input()` opens `TeamSkirmishPauseScreen` on Space

The full implementation should follow the pattern from `skirmish_screen.cpp` (lines 210-900+). The initialize method converts snapshots to Individuals (with adaptation), builds pools, creates session. The evolve method iterates team pools. The render methods use the same SDL drawing helpers.

Key initialization logic:
```cpp
void TeamSkirmishScreen::initialize(AppState& state) {
    rng_.seed(static_cast<uint32_t>(state.rng()));

    // Build team pools from config seeds
    team_pools_.clear();
    team_pools_.reserve(config_.team_seeds.size());

    for (const auto& seed : config_.team_seeds) {
        TeamPool pool;
        pool.seed = seed;

        // Convert snapshots to individuals
        auto squad_ind = snapshot_to_individual(seed.squad_snapshot);
        auto fighter_ind = snapshot_to_individual(seed.fighter_snapshot);

        // Adapt squad net if needed
        auto target_squad_ids = build_squad_leader_input_labels();
        if (squad_ind.topology.input_size != target_squad_ids.size() ||
            (!squad_ind.topology.input_ids.empty() &&
             squad_ind.topology.input_ids != target_squad_ids)) {
            if (squad_ind.topology.input_ids.empty()) {
                squad_ind.topology.input_ids = {
                    "Sqd HP", "Home Sin", "Home Cos", "Home Dst", "Home HP",
                    "Spacing", "Cmd Sin", "Cmd Cos", "Cmd Dst",
                    "Threat?", "Thr Sin", "Thr Cos", "Thr Dst", "Thr Scr"
                };
                squad_ind.topology.output_ids = build_squad_leader_output_labels();
            }
            auto [adapted, report] = adapt_individual_inputs(
                squad_ind, target_squad_ids, ship_design_, rng_);
            if (report.needed()) {
                std::cout << "[TeamSkirmish] Adapted squad net: " << report.message() << "\n";
            }
            squad_ind = adapted;
        }

        // Convert fighter if needed
        if (seed.fighter_snapshot.net_type != NetType::Fighter) {
            fighter_ind = convert_variant_to_fighter(fighter_ind, ship_design_);
        }

        // Build squad population (num_squads_per_team entries)
        const std::size_t num_squads = config_.arena.num_squads_per_team;
        pool.squad_population.reserve(num_squads);
        for (std::size_t i = 0; i < num_squads; ++i) {
            auto team = TeamIndividual::create(
                ship_design_, {8}, NtmNetConfig{}, SquadLeaderNetConfig{},
                rng_, &fighter_ind, &squad_ind);
            if (i > 0) {
                apply_mutations(team.ntm_individual, evo_config_, rng_);
                apply_mutations(team.squad_individual, evo_config_, rng_);
            }
            pool.squad_population.push_back(std::move(team));
        }

        // Build fighter population
        const std::size_t num_fighters =
            config_.arena.num_squads_per_team * config_.arena.fighters_per_squad;
        pool.fighter_population.reserve(num_fighters);
        for (std::size_t i = 0; i < num_fighters; ++i) {
            Individual fi = fighter_ind;  // copy from seed
            if (i > 0) {
                apply_mutations(fi, evo_config_, rng_);
            }
            pool.fighter_population.push_back(std::move(fi));
        }

        pool.squad_scores.assign(num_squads, 0.0f);
        pool.fighter_scores.assign(num_fighters, 0.0f);

        team_pools_.push_back(std::move(pool));
    }

    ship_design_ = config_.team_seeds[0].fighter_snapshot.ship_design;

    // Create first session
    session_ = std::make_unique<TeamSkirmishSession>(
        config_, ship_design_, team_pools_,
        static_cast<uint32_t>(rng_()));

    camera_.x = config_.arena.world_width / 2.0f;
    camera_.y = config_.arena.world_height / 2.0f;
    camera_.zoom = 0.15f;
    generation_ = 1;
    initialized_ = true;
}
```

Key evolution logic:
```cpp
void TeamSkirmishScreen::evolve_generation() {
    // Get pools back from session (scores accumulated)
    team_pools_ = std::move(session_->team_pools());

    for (auto& pool : team_pools_) {
        // Assign fighter fitness
        for (std::size_t i = 0; i < pool.fighter_population.size(); ++i) {
            pool.fighter_population[i].fitness = pool.fighter_scores[i];
        }

        // Assign squad fitness
        for (std::size_t i = 0; i < pool.squad_population.size(); ++i) {
            pool.squad_population[i].fitness = pool.squad_scores[i];
        }

        // Evolve fighters
        pool.fighter_population = evolve_population(
            pool.fighter_population, evo_config_, rng_);

        // Evolve squad leaders + NTM
        pool.squad_population = evolve_squad_only(
            pool.squad_population, evo_config_, rng_);

        // Reset scores
        std::fill(pool.fighter_scores.begin(), pool.fighter_scores.end(), 0.0f);
        std::fill(pool.squad_scores.begin(), pool.squad_scores.end(), 0.0f);
    }

    ++generation_;

    // Clear stale pointers
    net_viewer_state_.individual = nullptr;
    net_viewer_state_.network = nullptr;

    // Create new session
    session_ = std::make_unique<TeamSkirmishSession>(
        config_, ship_design_, team_pools_,
        static_cast<uint32_t>(rng_()));
}
```

The `render_world()` method duplicates the SDL drawing helpers from `skirmish_screen.cpp` (rotated triangles, circles, bases, bullets, etc.) — follow the exact same pattern. Use a team color palette:

```cpp
constexpr std::array<std::array<uint8_t, 3>, 8> TEAM_COLORS = {{
    {100, 150, 255}, // blue
    {255, 100, 100}, // red
    {100, 255, 100}, // green
    {255, 255, 100}, // yellow
    {100, 255, 255}, // cyan
    {255, 100, 255}, // magenta
    {255, 180, 100}, // orange
    {220, 220, 220}, // white
}};
```

The `handle_input()` method follows the same pattern as `SkirmishScreen::handle_input()` — Tab to cycle ships, F to toggle net view, Space to push pause screen, 1-4 speed, Escape to exit.

- [ ] **Step 3: Register in CMakeLists.txt**

Add after `src/ui/screens/game/team_skirmish_config_screen.cpp`:

```
    src/ui/screens/game/team_skirmish_screen.cpp
```

- [ ] **Step 4: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -20`

Fix any compilation errors.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/ui/screens/team_skirmish_config_screen.h \
        include/neuroflyer/ui/screens/team_skirmish_screen.h \
        src/ui/screens/game/team_skirmish_config_screen.cpp \
        src/ui/screens/game/team_skirmish_screen.cpp \
        CMakeLists.txt
git commit -m "feat(ui): add TeamSkirmishConfigScreen and TeamSkirmishScreen"
```

---

### Task 8: UI — TeamSkirmishPauseScreen

**Files:**
- Create: `include/neuroflyer/ui/screens/team_skirmish_pause_screen.h`
- Create: `src/ui/screens/game/team_skirmish_pause_screen.cpp`
- Modify: `CMakeLists.txt` (add source file)

Pause screen with Evolution tab + per-team Save Fighters tab + per-team Save Squad Leaders tab.

- [ ] **Step 1: Create the header**

```cpp
// include/neuroflyer/ui/screens/team_skirmish_pause_screen.h
#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class TeamSkirmishPauseScreen : public UIScreen {
public:
    TeamSkirmishPauseScreen(
        std::vector<TeamPool> team_pools,
        std::size_t generation,
        ShipDesign ship_design,
        EvolutionConfig evo_config,
        std::function<void(const EvolutionConfig&)> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "TeamSkirmishPause"; }

private:
    std::vector<TeamPool> team_pools_;
    std::size_t generation_;
    ShipDesign ship_design_;
    EvolutionConfig evo_config_;
    std::function<void(const EvolutionConfig&)> on_resume_;

    enum class Tab { Evolution, SaveFighters, SaveSquadLeaders };
    Tab active_tab_ = Tab::Evolution;
    int selected_team_ = 0;

    // Per-team sorted indices and selection state for fighters
    std::vector<std::vector<std::size_t>> fighter_sorted_;
    std::vector<std::vector<bool>> fighter_selected_;

    // Per-team sorted indices and selection state for squad leaders
    std::vector<std::vector<std::size_t>> squad_sorted_;
    std::vector<std::vector<bool>> squad_selected_;

    bool indices_built_ = false;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create the implementation**

Model on `SkirmishPauseScreen` but with:
- Team selector dropdown at top of save tabs
- Fighter save tab: lists `fighter_population` sorted by `fighter_scores`, saves as `NetType::Fighter` to `seed.fighter_genome_dir`
- Squad save tab: lists `squad_population` sorted by `squad_scores`, saves paired squad leader + NTM to `seed.squad_genome_dir/squad/`

The save logic for squad leaders is identical to `SkirmishPauseScreen` (lines 262-306). The save logic for fighters creates `Snapshot` with `NetType::Fighter` and calls `save_squad_variant(fighter_genome_dir, snap)` (fighters from team skirmish save to the squad/ subdirectory since they were evolved in a squad context).

Create at `src/ui/screens/game/team_skirmish_pause_screen.cpp`.

- [ ] **Step 3: Register in CMakeLists.txt**

Add after `src/ui/screens/game/team_skirmish_screen.cpp`:

```
    src/ui/screens/game/team_skirmish_pause_screen.cpp
```

- [ ] **Step 4: Wire pause screen into TeamSkirmishScreen**

In `team_skirmish_screen.cpp`'s `handle_input()`, when Space is pressed:

```cpp
if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
    paused_ = true;
    ui.push_screen(std::make_unique<TeamSkirmishPauseScreen>(
        team_pools_, generation_, ship_design_, evo_config_,
        [this](const EvolutionConfig& updated_config) {
            evo_config_ = updated_config;
            paused_ = false;
        }));
}
```

- [ ] **Step 5: Verify it compiles**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -10`

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/ui/screens/team_skirmish_pause_screen.h \
        src/ui/screens/game/team_skirmish_pause_screen.cpp \
        src/ui/screens/game/team_skirmish_screen.cpp \
        CMakeLists.txt
git commit -m "feat(ui): add TeamSkirmishPauseScreen with per-team save"
```

---

### Task 9: Main Menu — Add Team Skirmish entry point

**Files:**
- Modify: `src/ui/screens/menu/main_menu_screen.cpp` (add button)
- Modify: `include/neuroflyer/ui/screens/main_menu_screen.h` (add include if needed)

- [ ] **Step 1: Add the Team Skirmish button**

In `main_menu_screen.cpp`, add after the "Hangar" button (line 71) and before "Settings":

```cpp
    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Team Skirmish", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<TeamSkirmishConfigScreen>());
    }
```

Add `#include <neuroflyer/ui/screens/team_skirmish_config_screen.h>` to the includes.

- [ ] **Step 2: Verify it compiles and runs**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build --target neuroflyer 2>&1 | tail -5`

- [ ] **Step 3: Commit**

```bash
git add src/ui/screens/menu/main_menu_screen.cpp
git commit -m "feat(ui): add Team Skirmish button to main menu"
```

---

### Task 10: Build, run tests, fix issues

**Files:** Any files with compilation or test errors.

- [ ] **Step 1: Build everything**

Run: `cd /Users/wldarden/repos/Neuroflyer && cmake --build build 2>&1 | tail -30`

Fix any compilation errors.

- [ ] **Step 2: Run all tests**

Run: `./build/tests/neuroflyer_tests 2>&1 | tail -30`

Fix any test failures.

- [ ] **Step 3: Run the team skirmish tests specifically**

Run: `./build/tests/neuroflyer_tests --gtest_filter="TeamSkirmish*" -v 2>&1`

All should pass.

- [ ] **Step 4: Commit any fixes**

```bash
git add -u
git commit -m "fix: resolve build and test issues for team skirmish"
```

---

### Task 11: Update CLAUDE.md and backlog

**Files:**
- Modify: `CLAUDE.md` (add Team Skirmish to screen flow, architecture sections)
- Modify: `docs/backlog.md` (mark co-evolution as done if listed)

- [ ] **Step 1: Update CLAUDE.md**

Add Team Skirmish to the Screen Flow section:

```
               → "Team Skirmish" → push(TeamSkirmishConfigScreen)
                                    → push(TeamSkirmishScreen)
                                       → Space: push(TeamSkirmishPauseScreen)
```

Add a Team Skirmish Mode section to the architecture documentation describing the mode.

- [ ] **Step 2: Update backlog**

Check `docs/backlog.md` for any related items and mark as done.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md docs/backlog.md
git commit -m "docs: add Team Skirmish mode to CLAUDE.md and update backlog"
```

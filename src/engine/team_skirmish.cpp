// src/engine/team_skirmish.cpp
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>

namespace neuroflyer {

// ── Task 2: tick_team_arena_match ──────────────────────────────────────────

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
    SectorGrid grid(arena_config.world.world_width, arena_config.world.world_height,
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
    // team_squad_orders[match_local_team][squad] = order for that squad
    // squad_centers[match_local_team][squad] = (cx, cy)
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
                arena_config.world.world_width, arena_config.world.world_height);

            // Find own base and nearest enemy base
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
                arena_config.world.world_width * arena_config.world.world_width +
                arena_config.world.world_height * arena_config.world.world_height);
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

            const float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
                static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

            team_squad_orders[t][sq] = run_squad_leader(
                team_leader_nets[t][sq], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world.world_width,
                stats.centroid_y / arena_config.world.world_height);

            // Store leader inputs for visualization
            if (out_leader_inputs) {
                const std::size_t leader_viz_idx = t * num_squads + sq;
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
                        stats.centroid_x / arena_config.world.world_width,
                        stats.centroid_y / arena_config.world.world_height
                    };
                }
            }
        }
    }

    // Per-ship: run fighter net using per-ship assignment
    // assignments[i].team_id is GLOBAL team index; the nets are indexed by MATCH-LOCAL team index.
    // We build a reverse lookup from global team_id to match-local index using ship_teams (which
    // ArenaSession fills as 0..N for teams in this match). Since ArenaSession assigns match-local
    // team indices, we need to map assignments[i].team_id -> match-local index. The match was
    // started with start_match(match_teams), where match_teams[mt] is the global team id.
    // However, in tick_team_arena_match we don't receive match_teams directly. Instead we rely
    // on the fact that team_ntm_nets is sized by match-local team count, and the assignments
    // were created by build_ship_assignments which sets team_id = match_teams[t] (global id)
    // while t is the match-local index. We must map back from global to local.
    //
    // Strategy: build a mapping global_team_id -> match_local index by scanning assignments.
    // Since ship_teams[i] is the match-local team index (set by arena.team_of(i)), and
    // assignments[i].team_id is the global index, we build the map on first encounter.
    std::vector<std::size_t> global_to_local(256, SIZE_MAX);  // enough for any num_teams
    for (std::size_t i = 0; i < total_ships; ++i) {
        const std::size_t global_id = assignments[i].team_id;
        const std::size_t local_id = static_cast<std::size_t>(ship_teams[i]);
        if (global_id < global_to_local.size()) {
            global_to_local[global_id] = local_id;
        }
    }

    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena.ships()[i].alive) continue;

        const auto& assign = assignments[i];
        const std::size_t local_team = (assign.team_id < global_to_local.size())
            ? global_to_local[assign.team_id]
            : static_cast<std::size_t>(ship_teams[i]);
        const int team = ship_teams[i];  // match-local team for arena queries
        const auto sq = assign.squad_index;
        const auto fi = assign.fighter_index;

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena.ships()[i].x, arena.ships()[i].y,
            arena.ships()[i].rotation,
            team_squad_orders[local_team][sq],
            squad_center_xs[local_team][sq],
            squad_center_ys[local_team][sq],
            arena_config.world.world_width, arena_config.world.world_height);

        if (out_sl_inputs) {
            (*out_sl_inputs)[i] = sl_inputs;
        }

        auto ctx = ArenaQueryContext::for_ship(
            arena.ships()[i], i, team,
            arena_config.world.world_width, arena_config.world.world_height,
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

        assert(local_team < team_fighter_nets.size());
        assert(fi < team_fighter_nets[local_team].size());

        auto output = team_fighter_nets[local_team][fi].forward(
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

// ── Task 3: run_team_skirmish_match ──────────────────────────────────────────

TeamSkirmishMatchResult run_team_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamPool>& team_pools,
    const std::vector<std::size_t>& match_teams,
    const std::vector<ShipAssignment>& assignments,
    uint32_t seed) {

    const std::size_t num_teams = match_teams.size();

    ArenaConfig arena_config;
    arena_config.world = config.world;
    arena_config.world.num_teams = num_teams;
    arena_config.time_limit_ticks = config.time_limit_ticks;
    arena_config.sector_size = config.sector_size;
    arena_config.ntm_sector_radius = config.ntm_sector_radius;
    arena_config.rounds_per_generation = 1;

    ArenaSession arena(arena_config, seed);

    // Build per-team, per-squad/per-fighter networks (match-local indexing)
    std::vector<std::vector<neuralnet::Network>> team_ntm_nets(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_leader_nets(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_fighter_nets(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const auto& pool = team_pools[match_teams[t]];
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

        // Base damage: split evenly across all ships on the team
        std::size_t total_on_team = 0;
        for (std::size_t j = 0; j < total_ships; ++j) {
            if (ship_teams[j] == team) ++total_on_team;
        }

        if (total_on_team > 0) {
            float team_base_score = 0.0f;
            for (const auto& base : arena.bases()) {
                const float damage_dealt = base.max_hp - base.hp;
                if (base.team_id == team) {
                    if (damage_dealt > 0.0f && config.world.base_bullet_damage > 0.0f) {
                        const float hits = damage_dealt / config.world.base_bullet_damage;
                        team_base_score -= config.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score -= config.base_kill_points();
                    }
                } else {
                    if (damage_dealt > 0.0f && config.world.base_bullet_damage > 0.0f) {
                        const float hits = damage_dealt / config.world.base_bullet_damage;
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

// ── Task 4: build_ship_assignments + TeamSkirmishSession ───────────────────

std::vector<ShipAssignment> build_ship_assignments(
    const std::vector<TeamPool>& /*pools*/,
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
                const std::size_t ship_idx = t * ships_per_team + sq * fighters_per_squad + f;
                assignments[ship_idx].team_id = team_id;
                assignments[ship_idx].squad_index = sq;
                assignments[ship_idx].fighter_index = sq * fighters_per_squad + f;
            }
        }
    }
    return assignments;
}

// ── TeamSkirmishSession ────────────────────────────────────────────────────

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
        all_teams.reserve(num_teams);
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
    arena_config_.world = config_.arena.world;
    arena_config_.world.num_teams = match_teams.size();
    arena_config_.time_limit_ticks = config_.arena.time_limit_ticks;
    arena_config_.sector_size = config_.arena.sector_size;
    arena_config_.ntm_sector_radius = config_.arena.ntm_sector_radius;
    arena_config_.rounds_per_generation = 1;

    arena_ = std::make_unique<ArenaSession>(arena_config_,
                                             static_cast<uint32_t>(rng_()));

    // Build assignments: match-local team index t maps to match_teams[t] (global id)
    assignments_ = build_ship_assignments(
        team_pools_, match_teams,
        config_.arena.world.num_squads,
        config_.arena.world.fighters_per_squad);

    // Build per-team nets (indexed by match-local team index)
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
    const std::size_t total_squads = teams_in_match * config_.arena.world.num_squads;
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
                const float damage_dealt = base.max_hp - base.hp;
                if (base.team_id == team) {
                    if (damage_dealt > 0.0f && config_.arena.world.base_bullet_damage > 0.0f) {
                        const float hits = damage_dealt / config_.arena.world.base_bullet_damage;
                        team_base_score -= config_.arena.base_hit_points * hits;
                    }
                    if (!base.alive()) {
                        team_base_score -= config_.arena.base_kill_points();
                    }
                } else {
                    if (damage_dealt > 0.0f && config_.arena.world.base_bullet_damage > 0.0f) {
                        const float hits = damage_dealt / config_.arena.world.base_bullet_damage;
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

    // Accumulate squad scores (sum of fighter scores per squad)
    for (std::size_t t_idx = 0; t_idx < match_teams.size(); ++t_idx) {
        const std::size_t team_id = match_teams[t_idx];
        auto& pool = team_pools_[team_id];
        const std::size_t num_squads = pool.squad_population.size();
        const std::size_t fpq = config_.arena.world.fighters_per_squad;

        for (std::size_t sq = 0; sq < num_squads; ++sq) {
            float squad_sum = 0.0f;
            for (std::size_t f = 0; f < fpq; ++f) {
                const std::size_t fi = sq * fpq + f;
                for (std::size_t i = 0; i < total_ships; ++i) {
                    if (assignments_[i].team_id == team_id &&
                        assignments_[i].fighter_index == fi) {
                        squad_sum += config_.arena.kill_points *
                            static_cast<float>(kills[i]);
                        if (!arena_->ships()[i].alive) {
                            squad_sum -= config_.arena.death_points;
                        }
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

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(budget_ms);

    while (!background_done_) {
        if (bg_match_idx_ >= match_schedule_.size()) {
            background_done_ = true;
            break;
        }

        // Build assignments for this background match
        auto bg_assignments = build_ship_assignments(
            team_pools_, match_schedule_[bg_match_idx_],
            config_.arena.world.num_squads,
            config_.arena.world.fighters_per_squad);

        // Run the background match to completion
        auto result = run_team_skirmish_match(
            config_.arena, fighter_design_, team_pools_,
            match_schedule_[bg_match_idx_], bg_assignments,
            static_cast<uint32_t>(rng_()));

        // Accumulate fighter scores
        for (std::size_t i = 0; i < result.ship_scores.size(); ++i) {
            const auto& assign = bg_assignments[i];
            team_pools_[assign.team_id].fighter_scores[assign.fighter_index] +=
                result.ship_scores[i];
        }

        // Accumulate squad scores from background match
        const auto& match_teams = match_schedule_[bg_match_idx_];
        for (std::size_t t_idx = 0; t_idx < match_teams.size(); ++t_idx) {
            const std::size_t team_id = match_teams[t_idx];
            auto& pool = team_pools_[team_id];
            const std::size_t num_squads = pool.squad_population.size();
            const std::size_t fpq = config_.arena.world.fighters_per_squad;

            for (std::size_t sq = 0; sq < num_squads; ++sq) {
                float squad_sum = 0.0f;
                for (std::size_t f = 0; f < fpq; ++f) {
                    const std::size_t fi = sq * fpq + f;
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

} // namespace neuroflyer

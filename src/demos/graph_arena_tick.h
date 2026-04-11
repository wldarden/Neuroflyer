// src/demos/graph_arena_tick.h
//
// Duplicates of arena tick functions that accept GraphNetwork instead of Network.
// This avoids modifying the existing engine code for the performance demo.
// If GraphNetwork integration proceeds, these should be replaced by templating
// the originals on network type.
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_world.h>
#include <neuroflyer/entity_grid.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/team_skirmish.h>

#include <neuralnet/graph_network.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace graph_demo {

using namespace neuroflyer;

// --- NTM with GraphNetwork ---------------------------------------------------

[[nodiscard]] inline NtmResult graph_run_ntm_threat_selection(
    neuralnet::GraphNetwork& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h) {

    NtmResult result;
    if (threats.empty()) return result;

    float best_score = -std::numeric_limits<float>::max();

    for (const auto& threat : threats) {
        auto dr = compute_dir_range(
            squad_center_x, squad_center_y,
            threat.x, threat.y,
            world_w, world_h);

        std::vector<float> ntm_input = {
            dr.dir_sin, dr.dir_cos, dr.range,
            threat.health, squad_alive_fraction,
            threat.is_ship ? 1.0f : 0.0f,
            threat.is_starbase ? 1.0f : 0.0f
        };

        auto output = ntm_net.forward(std::span<const float>(ntm_input));
        float threat_score = output[0];

        if (threat_score > best_score) {
            best_score = threat_score;
            result.active = true;
            result.threat_score = threat_score;
            result.target_x = threat.x;
            result.target_y = threat.y;
            result.heading_sin = dr.dir_sin;
            result.heading_cos = dr.dir_cos;
            result.distance = dr.range;
        }
    }

    return result;
}

// --- Squad leader with GraphNetwork ------------------------------------------

[[nodiscard]] inline SquadLeaderOrder graph_run_squad_leader(
    neuralnet::GraphNetwork& leader_net,
    float squad_health,
    float home_heading_sin, float home_heading_cos, float home_distance,
    float home_health,
    float cmd_heading_sin, float cmd_heading_cos, float cmd_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y,
    float enemy_alive_fraction,
    float time_remaining,
    float squad_center_x_norm,
    float squad_center_y_norm) {

    std::vector<float> input = {
        squad_health,
        home_heading_sin, home_heading_cos, home_distance,
        home_health,
        cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
        ntm.active ? 1.0f : 0.0f,
        ntm.active ? ntm.heading_sin : 0.0f,
        ntm.active ? ntm.heading_cos : 0.0f,
        ntm.active ? ntm.distance : 0.0f,
        ntm.active ? ntm.threat_score : 0.0f,
        enemy_alive_fraction, time_remaining,
        squad_center_x_norm, squad_center_y_norm
    };

    auto output = leader_net.forward(std::span<const float>(input));

    SpacingOrder spacing = (output[0] >= output[1])
        ? SpacingOrder::Expand : SpacingOrder::Contract;

    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    float max_tactical = output[2];
    if (output[3] > max_tactical) { max_tactical = output[3]; tactical = TacticalOrder::AttackShip; }
    if (output[4] > max_tactical) { tactical = TacticalOrder::DefendHome; }

    SquadLeaderOrder order;
    order.tactical = tactical;
    order.spacing = spacing;

    switch (tactical) {
        case TacticalOrder::AttackStarbase:
            order.target_x = enemy_base_x; order.target_y = enemy_base_y; break;
        case TacticalOrder::AttackShip:
            if (ntm.active) { order.target_x = ntm.target_x; order.target_y = ntm.target_y; }
            else { order.target_x = enemy_base_x; order.target_y = enemy_base_y; }
            break;
        case TacticalOrder::DefendHome:
            order.target_x = own_base_x; order.target_y = own_base_y; break;
    }

    return order;
}

// --- Full team arena tick with GraphNetwork -----------------------------------

inline void graph_tick_team_arena_match(
    ArenaSession& arena,
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<ShipAssignment>& assignments,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_ntm_nets,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_leader_nets,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams) {

    const std::size_t total_ships = arena.ships().size();
    const std::size_t num_teams = team_ntm_nets.size();

    // Build sector grid
    SectorGrid grid(arena_config.world.world_width, arena_config.world.world_height,
                    arena_config.sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena.ships()[i].alive)
            grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
    }
    for (std::size_t b = 0; b < arena.bases().size(); ++b) {
        if (arena.bases()[b].alive())
            grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
    }

    // Per-squad: NTM + squad leader -> orders
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

            auto ntm = graph_run_ntm_threat_selection(
                team_ntm_nets[t][sq], stats.centroid_x, stats.centroid_y,
                stats.alive_fraction, threats,
                arena_config.world.world_width, arena_config.world.world_height);

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
                if (dsq < min_dist_sq) { min_dist_sq = dsq; enemy_base_x = base.x; enemy_base_y = base.y; }
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
                if (ship_teams[si] != team) { ++enemy_total; if (arena.ships()[si].alive) ++enemy_alive; }
            }
            if (enemy_total > 0) enemy_alive_frac = static_cast<float>(enemy_alive) / static_cast<float>(enemy_total);

            const float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
                static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

            team_squad_orders[t][sq] = graph_run_squad_leader(
                team_leader_nets[t][sq], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance, own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world.world_width,
                stats.centroid_y / arena_config.world.world_height);
        }
    }

    // Per-ship: run fighter net
    auto sensor_grid = build_sensor_grid(fighter_design,
        arena_config.world.world_width, arena_config.world.world_height,
        arena.ships(), arena.towers(), arena.tokens(), arena.bullets());

    std::vector<std::size_t> global_to_local(256, SIZE_MAX);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const std::size_t global_id = assignments[i].team_id;
        const std::size_t local_id = static_cast<std::size_t>(ship_teams[i]);
        if (global_id < global_to_local.size()) global_to_local[global_id] = local_id;
    }

    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena.ships()[i].alive) continue;

        const auto& assign = assignments[i];
        const std::size_t local_team = (assign.team_id < global_to_local.size())
            ? global_to_local[assign.team_id]
            : static_cast<std::size_t>(ship_teams[i]);
        const int team = ship_teams[i];
        const auto sq = assign.squad_index;
        const auto fi = assign.fighter_index;

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena.ships()[i].x, arena.ships()[i].y,
            arena.ships()[i].rotation,
            team_squad_orders[local_team][sq],
            squad_center_xs[local_team][sq], squad_center_ys[local_team][sq],
            arena_config.world.world_width, arena_config.world.world_height);

        auto ctx = ArenaQueryContext::for_ship(
            arena.ships()[i], i, team,
            arena_config.world.world_width, arena_config.world.world_height,
            arena.towers(), arena.tokens(),
            arena.ships(), ship_teams, arena.bullets());
        ctx.grid = &sensor_grid;

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states[i]);

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

} // namespace graph_demo

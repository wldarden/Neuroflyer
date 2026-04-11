#include <neuroflyer/arena_tick.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/entity_grid.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>

#include <cmath>
#include <limits>

namespace neuroflyer {

TickEvents tick_fighters_scripted(
    ArenaWorld& world,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> fighter_nets,
    const std::vector<SquadLeaderFighterInputs>& sl_inputs,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    std::vector<std::vector<float>>* out_fighter_inputs) {

    const auto& ships = world.ships();
    const auto& wc = world.config();

    auto sensor_grid = build_sensor_grid(fighter_design,
        wc.world_width, wc.world_height,
        world.ships(), world.towers(), world.tokens(), world.bullets());

    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        const auto& sl = sl_inputs[i];

        auto ctx = ArenaQueryContext::for_ship(
            ships[i], i, ship_teams[i],
            wc.world_width, wc.world_height,
            world.towers(), world.tokens(),
            world.ships(), ship_teams, world.bullets());
        ctx.grid = &sensor_grid;

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl.squad_target_heading, sl.squad_target_distance,
            sl.squad_center_heading, sl.squad_center_distance,
            sl.aggression, sl.spacing,
            recurrent_states[i]);

        if (out_fighter_inputs && i < out_fighter_inputs->size()) {
            (*out_fighter_inputs)[i] = input;
        }

        auto output = fighter_nets[i].forward(
            std::span<const float>(input));

        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design.memory_slots);

        world.set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        recurrent_states[i] = decoded.memory;
    }

    return world.tick();
}

TickEvents tick_arena_with_leader(
    ArenaWorld& world,
    const ArenaWorldConfig& config,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> ntm_nets,
    std::span<neuralnet::Network> leader_nets,
    std::span<neuralnet::Network> fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    uint32_t time_limit_ticks,
    int ntm_sector_radius,
    float sector_size,
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs,
    std::vector<std::vector<float>>* out_leader_inputs,
    std::vector<std::vector<float>>* out_fighter_inputs) {

    const auto& ships = world.ships();
    const std::size_t total_ships = ships.size();
    const std::size_t num_teams = config.num_teams;

    // Build sector grid and insert alive entities
    SectorGrid grid(config.world_width, config.world_height, sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (ships[i].alive) {
            grid.insert(i, ships[i].x, ships[i].y);
        }
    }
    for (std::size_t b = 0; b < world.bases().size(); ++b) {
        if (world.bases()[b].alive()) {
            grid.insert(total_ships + b, world.bases()[b].x, world.bases()[b].y);
        }
    }

    // Per-team: run NTM + squad leader pipeline
    std::vector<SquadLeaderOrder> team_orders(num_teams);
    std::vector<float> squad_center_xs(num_teams, 0.0f);
    std::vector<float> squad_center_ys(num_teams, 0.0f);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const int team = static_cast<int>(t);
        auto stats = world.compute_squad_stats(team, 0);
        squad_center_xs[t] = stats.centroid_x;
        squad_center_ys[t] = stats.centroid_y;

        auto threats = gather_near_threats(
            grid, stats.centroid_x, stats.centroid_y,
            ntm_sector_radius, team,
            ships, ship_teams, world.bases());

        auto ntm = run_ntm_threat_selection(
            ntm_nets[t], stats.centroid_x, stats.centroid_y,
            stats.alive_fraction, threats,
            config.world_width, config.world_height);

        // Find own and nearest enemy base
        const float own_base_x = world.bases()[t].x;
        const float own_base_y = world.bases()[t].y;
        const float own_base_hp = world.bases()[t].hp_normalized();
        float enemy_base_x = 0, enemy_base_y = 0;
        float min_dist_sq = std::numeric_limits<float>::max();
        for (const auto& base : world.bases()) {
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

        // Compute squad leader inputs
        const float world_diag = std::sqrt(
            config.world_width * config.world_width +
            config.world_height * config.world_height);
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

        // Enemy alive fraction
        float enemy_alive_frac = 0.0f;
        std::size_t enemy_total = 0, enemy_alive = 0;
        for (std::size_t si = 0; si < total_ships; ++si) {
            if (ship_teams[si] != team) {
                ++enemy_total;
                if (ships[si].alive) ++enemy_alive;
            }
        }
        if (enemy_total > 0) {
            enemy_alive_frac = static_cast<float>(enemy_alive) / static_cast<float>(enemy_total);
        }

        const float time_remaining = 1.0f - static_cast<float>(world.current_tick()) /
            static_cast<float>(std::max(time_limit_ticks, 1u));

        team_orders[t] = run_squad_leader(
            leader_nets[t], stats.alive_fraction,
            home_heading_sin, home_heading_cos, home_distance,
            own_base_hp,
            cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
            ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
            enemy_alive_frac, time_remaining,
            stats.centroid_x / config.world_width,
            stats.centroid_y / config.world_height);

        // Store squad leader net inputs for visualization
        if (out_leader_inputs && t < out_leader_inputs->size()) {
            (*out_leader_inputs)[t] = {
                stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm.active ? 1.0f : 0.0f,
                ntm.heading_sin, ntm.heading_cos,
                ntm.distance, ntm.threat_score,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / config.world_width,
                stats.centroid_y / config.world_height
            };
        }
    }

    // Per fighter: compute SL inputs, build sensor input, forward net
    auto sensor_grid = build_sensor_grid(fighter_design,
        config.world_width, config.world_height,
        world.ships(), world.towers(), world.tokens(), world.bullets());

    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!ships[i].alive) continue;

        const int team = ship_teams[i];
        const auto t = static_cast<std::size_t>(team);

        auto sl = compute_squad_leader_fighter_inputs(
            ships[i].x, ships[i].y,
            ships[i].rotation,
            team_orders[t],
            squad_center_xs[t], squad_center_ys[t],
            config.world_width, config.world_height);

        if (out_sl_inputs) {
            (*out_sl_inputs)[i] = sl;
        }

        auto ctx = ArenaQueryContext::for_ship(
            ships[i], i, team,
            config.world_width, config.world_height,
            world.towers(), world.tokens(),
            world.ships(), ship_teams, world.bullets());
        ctx.grid = &sensor_grid;

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl.squad_target_heading, sl.squad_target_distance,
            sl.squad_center_heading, sl.squad_center_distance,
            sl.aggression, sl.spacing,
            recurrent_states[i]);

        if (out_fighter_inputs && i < out_fighter_inputs->size()) {
            (*out_fighter_inputs)[i] = input;
        }

        auto output = fighter_nets[t].forward(
            std::span<const float>(input));

        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design.memory_slots);

        world.set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        recurrent_states[i] = decoded.memory;
    }

    return world.tick();
}

} // namespace neuroflyer

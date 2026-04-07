#include <neuroflyer/arena_match.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/squad_leader.h>

#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed) {

    assert(teams.size() == arena_config.world.num_teams);

    ArenaMatchResult result;
    result.team_scores.resize(arena_config.world.num_teams, 0.0f);

    // (a) Create arena session
    ArenaSession arena(arena_config, seed);

    // (b) Compile networks: NTM + squad leader + fighter per team
    std::vector<neuralnet::Network> ntm_nets;
    std::vector<neuralnet::Network> leader_nets;
    std::vector<neuralnet::Network> fighter_nets;
    ntm_nets.reserve(arena_config.world.num_teams);
    leader_nets.reserve(arena_config.world.num_teams);
    fighter_nets.reserve(arena_config.world.num_teams);

    for (std::size_t t = 0; t < arena_config.world.num_teams; ++t) {
        ntm_nets.push_back(teams[t].build_ntm_network());
        leader_nets.push_back(teams[t].build_squad_network());
        fighter_nets.push_back(teams[t].build_fighter_network());
    }

    // (c) Initialize recurrent states: one per fighter, sized to memory_slots
    std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent_states(
        total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));

    // (d) Build ship-to-team lookup
    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams[i] = arena.team_of(i);
    }

    // (e) Main loop
    while (!arena.is_over()) {
        // Build sector grid for this tick
        SectorGrid grid(arena_config.world.world_width, arena_config.world.world_height,
                        arena_config.sector_size);
        // Insert alive ships (IDs 0..total_ships-1)
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (arena.ships()[i].alive) {
                grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
            }
        }
        // Insert bases (IDs total_ships..total_ships+bases.size()-1)
        for (std::size_t b = 0; b < arena.bases().size(); ++b) {
            if (arena.bases()[b].alive()) {
                grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
            }
        }

        // Per team: run NTM + squad leader -> SquadLeaderOrder
        std::vector<SquadLeaderOrder> team_orders(arena_config.world.num_teams);
        std::vector<float> squad_center_xs(arena_config.world.num_teams, 0.0f);
        std::vector<float> squad_center_ys(arena_config.world.num_teams, 0.0f);

        for (std::size_t t = 0; t < arena_config.world.num_teams; ++t) {
            int team = static_cast<int>(t);
            auto stats = arena.compute_squad_stats(team, 0);
            squad_center_xs[t] = stats.centroid_x;
            squad_center_ys[t] = stats.centroid_y;

            auto threats = gather_near_threats(
                grid, stats.centroid_x, stats.centroid_y,
                arena_config.ntm_sector_radius, team,
                arena.ships(), ship_teams, arena.bases());

            auto ntm = run_ntm_threat_selection(
                ntm_nets[t], stats.centroid_x, stats.centroid_y,
                stats.alive_fraction, threats,
                arena_config.world.world_width, arena_config.world.world_height);

            // Find bases
            float own_base_x = arena.bases()[t].x, own_base_y = arena.bases()[t].y;
            float own_base_hp = arena.bases()[t].hp_normalized();
            float enemy_base_x = 0, enemy_base_y = 0;
            float min_dist_sq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                float dx = stats.centroid_x - base.x, dy = stats.centroid_y - base.y;
                float dsq = dx * dx + dy * dy;
                if (dsq < min_dist_sq) { min_dist_sq = dsq; enemy_base_x = base.x; enemy_base_y = base.y; }
            }

            // Compute squad leader inputs
            float world_diag = std::sqrt(arena_config.world.world_width * arena_config.world.world_width +
                                          arena_config.world.world_height * arena_config.world.world_height);
            float home_dx = own_base_x - stats.centroid_x, home_dy = own_base_y - stats.centroid_y;
            float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
            float home_distance = home_dist_raw / world_diag;
            float home_heading_sin = (home_dist_raw > 1e-6f) ? home_dx / home_dist_raw : 0.0f;
            float home_heading_cos = (home_dist_raw > 1e-6f) ? home_dy / home_dist_raw : 0.0f;
            float cmd_dx = enemy_base_x - stats.centroid_x, cmd_dy = enemy_base_y - stats.centroid_y;
            float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
            float cmd_heading_sin = (cmd_dist_raw > 1e-6f) ? cmd_dx / cmd_dist_raw : 0.0f;
            float cmd_heading_cos = (cmd_dist_raw > 1e-6f) ? cmd_dy / cmd_dist_raw : 0.0f;
            float cmd_target_distance = cmd_dist_raw / world_diag;

            // Compute enemy alive fraction
            float enemy_alive_frac = 0.0f;
            std::size_t enemy_total = 0, enemy_alive = 0;
            for (std::size_t si = 0; si < total_ships; ++si) {
                if (ship_teams[si] != team) {
                    ++enemy_total;
                    if (arena.ships()[si].alive) ++enemy_alive;
                }
            }
            if (enemy_total > 0) enemy_alive_frac = static_cast<float>(enemy_alive) / static_cast<float>(enemy_total);

            float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
                static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

            team_orders[t] = run_squad_leader(
                leader_nets[t], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world.world_width,
                stats.centroid_y / arena_config.world.world_height);
        }

        // For each alive ship: build input, run fighter net, decode output, set actions
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (!arena.ships()[i].alive) continue;

            int team = ship_teams[i];
            auto t = static_cast<std::size_t>(team);

            // Compute squad leader fighter inputs
            auto sl_inputs = compute_squad_leader_fighter_inputs(
                arena.ships()[i].x, arena.ships()[i].y,
                arena.ships()[i].rotation,
                team_orders[t],
                squad_center_xs[t], squad_center_ys[t],
                arena_config.world.world_width, arena_config.world.world_height);

            // Build ArenaQueryContext
            auto ctx = ArenaQueryContext::for_ship(
                arena.ships()[i], i, team,
                arena_config.world.world_width, arena_config.world.world_height,
                arena.towers(), arena.tokens(),
                arena.ships(), ship_teams, arena.bullets());

            // Build fighter input
            auto input = build_arena_ship_input(
                fighter_design, ctx,
                sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
                sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
                sl_inputs.aggression, sl_inputs.spacing,
                recurrent_states[i]);

            // Forward fighter net
            auto output = fighter_nets[t].forward(
                std::span<const float>(input));

            // Decode output into actions + memory
            auto decoded = decode_output(
                std::span<const float>(output),
                fighter_design.memory_slots);

            // Set ship actions
            arena.set_ship_actions(i,
                decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

            // Save recurrent state
            recurrent_states[i] = decoded.memory;
        }

        // Advance arena one tick
        arena.tick();
    }

    result.ticks_elapsed = arena.current_tick();
    result.match_completed = true;

    // (f) Compute team fitness scores
    auto num_teams = arena_config.world.num_teams;

    for (std::size_t t = 0; t < num_teams; ++t) {
        int team = static_cast<int>(t);

        // Damage dealt: average of (max_hp - hp) / max_hp for each enemy base
        float damage_dealt = 0.0f;
        for (const auto& base : arena.bases()) {
            if (base.team_id == team) continue;
            damage_dealt += (base.max_hp - base.hp) / base.max_hp;
        }
        if (num_teams > 1) {
            damage_dealt /= static_cast<float>(num_teams - 1);
        }

        // Own survival: own base HP normalized
        float own_survival = arena.bases()[t].hp_normalized();

        // Alive fraction: alive ships on team / total ships on team
        std::size_t ships_per_team = arena_config.world.num_squads * arena_config.world.fighters_per_squad;
        std::size_t alive_on_team = 0;
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team && arena.ships()[i].alive) {
                ++alive_on_team;
            }
        }
        float alive_frac = (ships_per_team > 0)
            ? static_cast<float>(alive_on_team) / static_cast<float>(ships_per_team)
            : 0.0f;

        // Token fraction: tokens collected by team / total token count
        float token_frac = 0.0f;
        if (arena_config.world.token_count > 0) {
            int team_tokens = 0;
            const auto& tc = arena.tokens_collected();
            for (std::size_t i = 0; i < total_ships; ++i) {
                if (ship_teams[i] == team) {
                    team_tokens += tc[i];
                }
            }
            token_frac = static_cast<float>(team_tokens) /
                         static_cast<float>(arena_config.world.token_count);
        }

        // Weighted score
        result.team_scores[t] =
            arena_config.fitness_weight_base_damage * damage_dealt +
            arena_config.fitness_weight_survival * own_survival +
            arena_config.fitness_weight_ships_alive * alive_frac +
            arena_config.fitness_weight_tokens * token_frac;
    }

    return result;
}

} // namespace neuroflyer

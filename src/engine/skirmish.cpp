#include <neuroflyer/skirmish.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/squad_leader.h>

#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

SkirmishMatchResult run_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed) {

    // Build ArenaConfig from SkirmishConfig, override num_teams from teams.size()
    ArenaConfig arena_config = config.to_arena_config();
    arena_config.num_teams = teams.size();

    assert(teams.size() >= 2);

    SkirmishMatchResult result;
    result.team_scores.resize(arena_config.num_teams, 0.0f);

    // (a) Create arena session
    ArenaSession arena(arena_config, seed);

    // (b) Compile networks: NTM + squad leader + fighter per team
    std::vector<neuralnet::Network> ntm_nets;
    std::vector<neuralnet::Network> leader_nets;
    std::vector<neuralnet::Network> fighter_nets;
    ntm_nets.reserve(arena_config.num_teams);
    leader_nets.reserve(arena_config.num_teams);
    fighter_nets.reserve(arena_config.num_teams);

    for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
        ntm_nets.push_back(teams[t].build_ntm_network());
        leader_nets.push_back(teams[t].build_squad_network());
        fighter_nets.push_back(teams[t].build_fighter_network());
    }

    // (c) Initialize recurrent states: one per fighter, sized to memory_slots
    const std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent_states(
        total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));

    // (d) Build ship-to-team lookup
    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams[i] = arena.team_of(i);
    }

    // (e) Main loop — identical to run_arena_match()
    while (!arena.is_over()) {
        // Build sector grid for this tick
        SectorGrid grid(arena_config.world_width, arena_config.world_height,
                        arena_config.sector_size);
        // Insert alive ships
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (arena.ships()[i].alive) {
                grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
            }
        }
        // Insert bases
        for (std::size_t b = 0; b < arena.bases().size(); ++b) {
            if (arena.bases()[b].alive()) {
                grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
            }
        }

        // Per team: run NTM + squad leader -> SquadLeaderOrder
        std::vector<SquadLeaderOrder> team_orders(arena_config.num_teams);
        std::vector<float> squad_center_xs(arena_config.num_teams, 0.0f);
        std::vector<float> squad_center_ys(arena_config.num_teams, 0.0f);

        for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
            const int team = static_cast<int>(t);
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

            // Compute squad leader inputs
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

            team_orders[t] = run_squad_leader(
                leader_nets[t], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp, stats.squad_spacing,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y);
        }

        // For each alive ship: build input, run fighter net, decode output, set actions
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (!arena.ships()[i].alive) continue;

            const int team = ship_teams[i];
            const auto t = static_cast<std::size_t>(team);

            // Compute squad leader fighter inputs
            auto sl_inputs = compute_squad_leader_fighter_inputs(
                arena.ships()[i].x, arena.ships()[i].y,
                arena.ships()[i].rotation,
                team_orders[t],
                squad_center_xs[t], squad_center_ys[t],
                arena_config.world_width, arena_config.world_height);

            // Build ArenaQueryContext
            auto ctx = ArenaQueryContext::for_ship(
                arena.ships()[i], i, team,
                arena_config.world_width, arena_config.world_height,
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
    result.completed = true;

    // (f) Compute kill-based team scores
    const auto num_teams = arena_config.num_teams;
    const auto& kills = arena.enemy_kills();

    for (std::size_t t = 0; t < num_teams; ++t) {
        const int team = static_cast<int>(t);
        float score = 0.0f;

        // +kill_points per enemy killed by any fighter on this team
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team) {
                score += config.kill_points * static_cast<float>(kills[i]);
            }
        }

        // +base_kill_points() for each enemy base destroyed
        for (const auto& base : arena.bases()) {
            if (base.team_id == team) continue;
            if (!base.alive()) {
                score += config.base_kill_points();
            }
        }

        // -death_points per own dead fighter
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team && !arena.ships()[i].alive) {
                score -= config.death_points;
            }
        }

        result.team_scores[t] = score;
    }

    return result;
}

} // namespace neuroflyer

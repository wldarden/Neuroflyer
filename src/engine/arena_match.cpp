#include <neuroflyer/arena_match.h>
#include <neuroflyer/sensor_engine.h>

#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const SquadNetConfig& squad_config,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed) {

    assert(teams.size() == arena_config.num_teams);
    (void)squad_config;  // Config is baked into TeamIndividual's topology

    ArenaMatchResult result;
    result.team_scores.resize(arena_config.num_teams, 0.0f);

    // (a) Create arena session
    ArenaSession arena(arena_config, seed);

    // (b) Compile networks: one squad net + one fighter net per team
    std::vector<neuralnet::Network> squad_nets;
    std::vector<neuralnet::Network> fighter_nets;
    squad_nets.reserve(arena_config.num_teams);
    fighter_nets.reserve(arena_config.num_teams);

    for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
        squad_nets.push_back(teams[t].build_squad_network());
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
        float time_progress = static_cast<float>(arena.current_tick()) /
                              static_cast<float>(arena_config.time_limit_ticks);

        // For each team: build squad net input, run squad net -> broadcast signals
        std::vector<std::vector<float>> broadcast_signals(arena_config.num_teams);

        for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
            int team = static_cast<int>(t);

            // Build squad net input from squad stats (8 floats)
            auto stats = arena.compute_squad_stats(team, 0);

            // Find own base HP and enemy base HP
            float own_base_hp = arena.bases()[t].hp_normalized();
            float enemy_base_hp = 0.0f;
            for (const auto& base : arena.bases()) {
                if (base.team_id != team) {
                    enemy_base_hp = base.hp_normalized();
                    break;  // first non-team base
                }
            }

            std::vector<float> squad_input = {
                stats.alive_fraction,
                stats.avg_dist_to_enemy_base,
                stats.avg_dist_to_home,
                stats.centroid_dir_sin,
                stats.centroid_dir_cos,
                own_base_hp,
                enemy_base_hp,
                time_progress
            };

            // Run squad net
            broadcast_signals[t] = squad_nets[t].forward(
                std::span<const float>(squad_input));
        }

        // For each alive ship: build input, run fighter net, decode output, set actions
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (!arena.ships()[i].alive) continue;

            int team = ship_teams[i];
            auto t = static_cast<std::size_t>(team);

            // Find target: nearest enemy base position
            float target_x = 0, target_y = 0;
            float min_dist_sq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                float dx = base.x - arena.ships()[i].x;
                float dy = base.y - arena.ships()[i].y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < min_dist_sq) {
                    min_dist_sq = dist_sq;
                    target_x = base.x;
                    target_y = base.y;
                }
            }

            // Home: own base position
            float home_x = arena.bases()[t].x;
            float home_y = arena.bases()[t].y;

            // Compute dir+range for target and home
            auto target_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                target_x, target_y,
                arena_config.world_width, arena_config.world_height);

            auto home_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                home_x, home_y,
                arena_config.world_width, arena_config.world_height);

            float own_base_hp = arena.bases()[t].hp_normalized();

            // Build ArenaQueryContext
            ArenaQueryContext ctx;
            ctx.ship_x = arena.ships()[i].x;
            ctx.ship_y = arena.ships()[i].y;
            ctx.ship_rotation = arena.ships()[i].rotation;
            ctx.world_w = arena_config.world_width;
            ctx.world_h = arena_config.world_height;
            ctx.self_index = i;
            ctx.self_team = team;
            ctx.towers = arena.towers();
            ctx.tokens = arena.tokens();
            ctx.ships = arena.ships();
            ctx.ship_teams = ship_teams;
            ctx.bullets = arena.bullets();

            // Build fighter input
            auto input = build_arena_ship_input(
                fighter_design, ctx,
                target_dr.dir_sin, target_dr.dir_cos, target_dr.range,
                home_dr.dir_sin, home_dr.dir_cos, home_dr.range,
                own_base_hp,
                broadcast_signals[t],
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
    auto num_teams = arena_config.num_teams;

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
        std::size_t ships_per_team = arena_config.num_squads * arena_config.fighters_per_squad;
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
        if (arena_config.token_count > 0) {
            int team_tokens = 0;
            const auto& tc = arena.tokens_collected();
            for (std::size_t i = 0; i < total_ships; ++i) {
                if (ship_teams[i] == team) {
                    team_tokens += tc[i];
                }
            }
            token_frac = static_cast<float>(team_tokens) /
                         static_cast<float>(arena_config.token_count);
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

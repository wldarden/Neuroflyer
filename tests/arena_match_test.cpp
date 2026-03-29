#include <neuroflyer/arena_match.h>
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ArenaMatchTest, RunsWithoutCrash) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 5;
    arena_config.token_count = 3;
    arena_config.world_width = 1000.0f;
    arena_config.world_height = 1000.0f;
    arena_config.base_hp = 100.0f;
    arena_config.time_limit_ticks = 60;

    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 200.0f, 0.0f, true, 1},
        {nf::SensorType::Raycast, 0.5f, 200.0f, 0.0f, true, 2},
        {nf::SensorType::Raycast, -0.5f, 200.0f, 0.0f, true, 3},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {6};
    squad_config.output_size = 4;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(nf::TeamIndividual::create(design, {8}, squad_config, rng));
    teams.push_back(nf::TeamIndividual::create(design, {8}, squad_config, rng));

    auto result = nf::run_arena_match(arena_config, design, squad_config, teams, 42);

    EXPECT_EQ(result.team_scores.size(), 2u);
    EXPECT_TRUE(result.match_completed);
    EXPECT_GT(result.ticks_elapsed, 0u);
}

TEST(ArenaMatchTest, ScoresAreNonNegative) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 0;
    arena_config.token_count = 0;
    arena_config.world_width = 1000.0f;
    arena_config.world_height = 1000.0f;
    arena_config.base_hp = 100.0f;
    arena_config.time_limit_ticks = 30;

    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 200.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(nf::TeamIndividual::create(design, {6}, squad_config, rng));
    teams.push_back(nf::TeamIndividual::create(design, {6}, squad_config, rng));

    auto result = nf::run_arena_match(arena_config, design, squad_config, teams, 42);
    EXPECT_GE(result.team_scores[0], 0.0f);
    EXPECT_GE(result.team_scores[1], 0.0f);
}

TEST(ArenaMatchTest, FullGenerationCycle) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 0;
    arena_config.token_count = 0;
    arena_config.world_width = 500.0f;
    arena_config.world_height = 500.0f;
    arena_config.base_hp = 50.0f;
    arena_config.time_limit_ticks = 30;

    nf::ShipDesign design;
    design.sensors = {{nf::SensorType::Raycast, 0.0f, 100.0f, 0.0f, true, 1}};
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    auto pop = nf::create_team_population(design, {6}, squad_config, 6, rng);

    nf::EvolutionConfig evo_config;
    evo_config.elitism_count = 1;
    evo_config.tournament_size = 2;

    for (int gen = 0; gen < 3; ++gen) {
        for (std::size_t i = 0; i + 1 < pop.size(); i += 2) {
            std::vector<nf::TeamIndividual> match_teams = {pop[i], pop[i + 1]};
            auto result = nf::run_arena_match(arena_config, design, squad_config,
                                               match_teams, static_cast<uint32_t>(gen * 100 + i));
            pop[i].fitness += result.team_scores[0];
            pop[i + 1].fitness += result.team_scores[1];
        }
        pop = nf::evolve_team_population(pop, evo_config, rng);
    }

    EXPECT_EQ(pop.size(), 6u);
    auto net = pop[0].build_fighter_network();
    EXPECT_GT(net.input_size(), 0u);
}

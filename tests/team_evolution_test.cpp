// tests/team_evolution_test.cpp
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/arena_sensor.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(TeamEvolutionTest, CreateTeamIndividual) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
        {nf::SensorType::Raycast, 1.0f, 300.0f, 0.0f, true, 2},
    };
    design.memory_slots = 4;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {6};
    squad_config.output_size = 4;

    auto team = nf::TeamIndividual::create(design, {8, 8}, squad_config, rng);

    auto squad_net = team.build_squad_network();
    EXPECT_EQ(squad_net.input_size(), 8u);
    EXPECT_EQ(squad_net.output_size(), 4u);

    auto fighter_net = team.build_fighter_network();
    std::size_t expected_input = nf::compute_arena_input_size(design, 4);
    EXPECT_EQ(fighter_net.input_size(), expected_input);
    EXPECT_EQ(fighter_net.output_size(), nf::compute_output_size(design));
}

TEST(TeamEvolutionTest, EvolveTeamPopulation) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    auto pop = nf::create_team_population(design, {6}, squad_config, 10, rng);
    ASSERT_EQ(pop.size(), 10u);

    // Assign fake fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i) * 10.0f;
    }

    nf::EvolutionConfig evo_config;
    evo_config.elitism_count = 2;
    evo_config.tournament_size = 3;

    auto next = nf::evolve_team_population(pop, evo_config, rng);
    EXPECT_EQ(next.size(), 10u);

    // All fitness should be reset to 0
    for (const auto& t : next) {
        EXPECT_FLOAT_EQ(t.fitness, 0.0f);
    }

    // Both nets should still build valid networks
    auto squad_net = next[0].build_squad_network();
    auto fighter_net = next[0].build_fighter_network();
    EXPECT_EQ(squad_net.input_size(), 8u);
    EXPECT_EQ(squad_net.output_size(), 4u);
}

#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

using namespace neuroflyer;

namespace {

ShipDesign make_test_design() {
    ShipDesign d;
    SensorDef s;
    s.angle = 0.0f;
    s.range = 200.0f;
    s.is_full_sensor = true;
    d.sensors.push_back(s);
    d.memory_slots = 2;
    return d;
}

} // namespace

TEST(TeamEvolution, CreateTeamIndividual) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto team = TeamIndividual::create(design, {6}, ntm_cfg, leader_cfg, rng);

    // NTM net: 7 -> [4] -> 1
    auto ntm_net = team.build_ntm_network();
    EXPECT_EQ(ntm_net.input_size(), 7u);
    EXPECT_EQ(ntm_net.output_size(), 1u);

    // Squad leader: 14 -> [8] -> 5
    auto leader_net = team.build_squad_network();
    EXPECT_EQ(leader_net.input_size(), 14u);
    EXPECT_EQ(leader_net.output_size(), 5u);

    // Fighter: sensors(5) + squad_leader(6) + mem(2) = 13
    auto fighter_net = team.build_fighter_network();
    EXPECT_EQ(fighter_net.input_size(), 5u + 6 + 2);
    EXPECT_EQ(fighter_net.output_size(), 5u + 2);  // 5 actions + 2 memory
}

TEST(TeamEvolution, EvolveTeamPopulation) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto pop = create_team_population(design, {6}, ntm_cfg, leader_cfg, 10, rng);
    EXPECT_EQ(pop.size(), 10u);

    // Assign ascending fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);
    }

    EvolutionConfig evo;
    evo.population_size = 10;
    evo.elitism_count = 2;
    evo.tournament_size = 3;

    auto next = evolve_team_population(pop, evo, rng);
    EXPECT_EQ(next.size(), 10u);

    // All fitness reset to 0
    for (const auto& t : next) {
        EXPECT_FLOAT_EQ(t.fitness, 0.0f);
    }

    // All three nets should still build valid networks
    for (const auto& t : next) {
        auto ntm = t.build_ntm_network();
        auto leader = t.build_squad_network();
        auto fighter = t.build_fighter_network();
        EXPECT_EQ(ntm.input_size(), 7u);
        EXPECT_EQ(leader.input_size(), 14u);
        EXPECT_GT(fighter.input_size(), 0u);
    }
}

TEST(TeamEvolution, EvolveSquadOnlyFreezesFighters) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    NtmNetConfig ntm_cfg;
    SquadLeaderNetConfig leader_cfg;

    auto pop = create_team_population(design, {6}, ntm_cfg, leader_cfg, 6, rng);

    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);
    }

    EvolutionConfig evo;
    evo.population_size = 6;
    evo.elitism_count = 2;
    evo.tournament_size = 3;

    auto next = evolve_squad_only(pop, evo, rng);
    EXPECT_EQ(next.size(), 6u);

    // All individuals should have valid networks
    for (const auto& t : next) {
        EXPECT_GT(t.build_ntm_network().input_size(), 0u);
        EXPECT_GT(t.build_squad_network().input_size(), 0u);
        EXPECT_GT(t.build_fighter_network().input_size(), 0u);
    }
}

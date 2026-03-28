#include <neuroflyer/evolution.h>

#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(EvolutionTest, CreateIndividual) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(15, {12, 12}, 5, rng);

    EXPECT_EQ(ind.topology.input_size, 15);
    EXPECT_EQ(ind.topology.layers.size(), 3);  // 2 hidden + 1 output
    EXPECT_GT(ind.genome.genes().size(), 0);
}

TEST(EvolutionTest, BuildNetworkFromIndividual) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(15, {12, 12}, 5, rng);
    auto net = ind.build_network();

    EXPECT_EQ(net.input_size(), 15);
    EXPECT_EQ(net.output_size(), 5);

    std::vector<float> input(15, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 5);
}

TEST(EvolutionTest, TopologyEquality) {
    std::mt19937 rng(42);
    auto a = nf::Individual::random(15, {12, 12}, 5, rng);
    auto b = nf::Individual::random(15, {12, 12}, 5, rng);
    auto c = nf::Individual::random(15, {12, 8}, 5, rng);  // different topology

    EXPECT_TRUE(nf::same_topology(a, b));
    EXPECT_FALSE(nf::same_topology(a, c));
}

TEST(EvolutionTest, WeightMutation) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(15, {12, 12}, 5, rng);
    auto original = ind.genome.flatten_all();

    nf::EvolutionConfig config;
    nf::mutate_individual(ind, config, rng);

    EXPECT_NE(ind.genome.flatten_all(), original);
}

TEST(EvolutionTest, TopologyMutationAddNode) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(4, {3}, 2, rng);
    auto original_total = ind.genome.total_values();

    nf::add_node(ind, rng);

    // Should have more weight values now
    EXPECT_GT(ind.genome.total_values(), original_total);
}

TEST(EvolutionTest, PopulationEvolves) {
    std::mt19937 rng(42);
    nf::EvolutionConfig config;
    config.population_size = 20;

    auto pop = nf::create_population(15, {12, 12}, 5, config, rng);
    EXPECT_EQ(pop.size(), 20);

    // Assign fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i);
    }

    auto next = nf::evolve_population(pop, config, rng);
    EXPECT_EQ(next.size(), 20);
}


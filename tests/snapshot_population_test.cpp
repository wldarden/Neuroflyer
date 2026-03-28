#include <neuroflyer/evolution.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

namespace nf = neuroflyer;

namespace {

nf::ShipDesign make_test_design() {
    nf::ShipDesign design;
    design.memory_slots = 2;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false},
    };
    return design;
}

} // namespace

TEST(SnapshotPopulation, CreateRandomSnapshot) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    auto snap = nf::create_random_snapshot("TestNet", design, {8, 4}, rng);

    EXPECT_EQ(snap.name, "TestNet");
    EXPECT_EQ(snap.generation, 0u);
    EXPECT_EQ(snap.parent_name, "");
    EXPECT_EQ(snap.topology.input_size, nf::compute_input_size(design));
    ASSERT_EQ(snap.topology.layers.size(), 3u);  // 8, 4, output
    EXPECT_EQ(snap.topology.layers[0].output_size, std::size_t{8});
    EXPECT_EQ(snap.topology.layers[1].output_size, std::size_t{4});
    EXPECT_EQ(snap.topology.layers[2].output_size, nf::compute_output_size(design));
    EXPECT_GT(snap.weights.size(), 0u);
    // Weights should be the right count for this topology
    EXPECT_EQ(snap.weights.size(), nf::count_weight_genes(snap.topology));
}

TEST(SnapshotPopulation, BestAsSnapshot) {
    auto design = make_test_design();
    std::mt19937 rng(42);

    // Create a small population manually
    auto input_sz = nf::compute_input_size(design);
    auto output_sz = nf::compute_output_size(design);
    std::vector<nf::Individual> pop;
    for (int i = 0; i < 5; ++i) {
        pop.push_back(nf::Individual::random(input_sz, {4}, output_sz, rng));
        pop.back().fitness = static_cast<float>(i * 100);
    }

    auto snap = nf::best_as_snapshot("BestNet", pop, design, 42);
    EXPECT_EQ(snap.name, "BestNet");
    EXPECT_EQ(snap.generation, 42u);
    // snap.weights stores weights + biases + per-node activations via flatten("layer_")
    EXPECT_EQ(snap.weights.size(), nf::count_weight_genes(snap.topology));

    // Extract weights + biases + activations from the best individual's genome
    const auto& best = pop.back();
    std::vector<float> expected_weights;
    for (std::size_t l = 0; l < best.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (best.genome.has_gene(lp + "_weights")) {
            const auto& wg = best.genome.gene(lp + "_weights");
            expected_weights.insert(expected_weights.end(), wg.values.begin(), wg.values.end());
        }
        if (best.genome.has_gene(lp + "_biases")) {
            const auto& bg = best.genome.gene(lp + "_biases");
            expected_weights.insert(expected_weights.end(), bg.values.begin(), bg.values.end());
        }
        if (best.genome.has_gene(lp + "_activations")) {
            const auto& ag = best.genome.gene(lp + "_activations");
            expected_weights.insert(expected_weights.end(), ag.values.begin(), ag.values.end());
        }
    }
    EXPECT_EQ(snap.weights, expected_weights);
}

TEST(SnapshotPopulation, CreatePopulationFromSnapshot) {
    std::mt19937 rng(42);
    auto design = make_test_design();
    auto snap = nf::create_random_snapshot("Seed", design, {4}, rng);

    nf::EvolutionConfig config;
    config.population_size = 10;
    auto pop = nf::create_population_from_snapshot(snap, 10, config, rng);

    ASSERT_EQ(pop.size(), 10u);

    // First individual's weights+biases+activations should match the snapshot
    std::vector<float> first_weights;
    for (std::size_t l = 0; l < pop[0].topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (pop[0].genome.has_gene(lp + "_weights")) {
            const auto& wg = pop[0].genome.gene(lp + "_weights");
            first_weights.insert(first_weights.end(), wg.values.begin(), wg.values.end());
        }
        if (pop[0].genome.has_gene(lp + "_biases")) {
            const auto& bg = pop[0].genome.gene(lp + "_biases");
            first_weights.insert(first_weights.end(), bg.values.begin(), bg.values.end());
        }
        if (pop[0].genome.has_gene(lp + "_activations")) {
            const auto& ag = pop[0].genome.gene(lp + "_activations");
            first_weights.insert(first_weights.end(), ag.values.begin(), ag.values.end());
        }
    }
    EXPECT_EQ(first_weights, snap.weights);
    EXPECT_EQ(pop[0].topology.input_size, snap.topology.input_size);
    EXPECT_EQ(pop[0].topology.layers.size(), snap.topology.layers.size());

    // At least some of the rest should have different weights (mutated)
    int different_count = 0;
    for (std::size_t i = 1; i < pop.size(); ++i) {
        if (pop[i].genome.flatten_all() != pop[0].genome.flatten_all()) ++different_count;
    }
    EXPECT_GT(different_count, 0);
}

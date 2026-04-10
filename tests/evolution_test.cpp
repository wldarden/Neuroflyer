#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot_utils.h>
#include "../src/demos/mlp_to_graph.h"

#include <neuralnet/graph_network.h>

#include <gtest/gtest.h>

#include <cmath>
#include <set>

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

TEST(EvolutionTest, AddNodePreservesWeightLayoutInNextLayer) {
    // Create a net with 3 inputs -> 4 hidden -> 2 outputs.
    // Set all weights in the output layer (layer 1) to known values,
    // then add a node to the hidden layer (layer 0) making it 5 nodes.
    // The output layer should preserve existing connections (4 per output
    // neuron) and add one new connection per output neuron (dense).
    std::mt19937 rng(99);
    auto ind = nf::Individual::random(3, {4}, 2, rng);

    // Layer 1 (output layer): 2 outputs × 4 inputs = 8 weights, row-major.
    // Set to known pattern: weight[row][col] = (row+1)*10 + col
    auto& out_weights = ind.genome.gene("layer_1_weights").values;
    ASSERT_EQ(out_weights.size(), 8u);  // 2 × 4
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            out_weights[row * 4 + col] = static_cast<float>((row + 1) * 10 + col);
        }
    }
    // Verify: out_weights = [10, 11, 12, 13, 20, 21, 22, 23]

    // Force add_node to target layer 0 (only hidden layer) by fixing the rng
    // — but add_node picks a random hidden layer. With {4} there's only one
    // hidden layer (layer 0), so it always picks layer 0.
    nf::add_node(ind, rng);

    // Hidden layer should now have 5 nodes
    ASSERT_EQ(ind.topology.layers[0].output_size, 5u);

    // Output layer weights should now be 2 × 5 = 10 values
    const auto& new_out = ind.genome.gene("layer_1_weights").values;
    ASSERT_EQ(new_out.size(), 10u);

    // Existing connections must be preserved at correct positions:
    // Row 0: [10, 11, 12, 13, <new>]
    // Row 1: [20, 21, 22, 23, <new>]
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            float expected = static_cast<float>((row + 1) * 10 + col);
            EXPECT_FLOAT_EQ(new_out[row * 5 + col], expected)
                << "row=" << row << " col=" << col;
        }
        // The new column (col 4) should be filled (any value is fine,
        // just verify it exists at the correct position)
    }
}

TEST(EvolutionTest, RemoveNodePreservesWeightLayoutInNextLayer) {
    std::mt19937 rng(99);
    auto ind = nf::Individual::random(3, {4}, 2, rng);

    // Set output layer weights to known pattern
    auto& out_weights = ind.genome.gene("layer_1_weights").values;
    ASSERT_EQ(out_weights.size(), 8u);  // 2 × 4
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            out_weights[row * 4 + col] = static_cast<float>((row + 1) * 10 + col);
        }
    }

    nf::remove_node(ind, rng);

    // Hidden layer should now have 3 nodes
    ASSERT_EQ(ind.topology.layers[0].output_size, 3u);

    // Output layer weights should now be 2 × 3 = 6 values
    const auto& new_out = ind.genome.gene("layer_1_weights").values;
    ASSERT_EQ(new_out.size(), 6u);

    // First 3 columns of each row should be preserved:
    // Row 0: [10, 11, 12]   (column 3 dropped)
    // Row 1: [20, 21, 22]   (column 3 dropped)
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            float expected = static_cast<float>((row + 1) * 10 + col);
            EXPECT_FLOAT_EQ(new_out[row * 3 + col], expected)
                << "row=" << row << " col=" << col;
        }
    }
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

// --- convert_variant_to_fighter tests ---

TEST(EvolutionTest, ConvertVariantToFighterTopology) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef full_sensor;
    full_sensor.type = nf::SensorType::Occulus;
    full_sensor.angle = 0.0f;
    full_sensor.range = 200.0f;
    full_sensor.width = 0.1f;
    full_sensor.is_full_sensor = true;
    design.sensors.push_back(full_sensor);
    nf::SensorDef sight_sensor;
    sight_sensor.type = nf::SensorType::Raycast;
    sight_sensor.angle = 0.5f;
    sight_sensor.range = 100.0f;
    sight_sensor.width = 0.05f;
    sight_sensor.is_full_sensor = false;
    design.sensors.push_back(sight_sensor);
    design.memory_slots = 2;

    // Scroller input: 4 (full) + 1 (sight) + 3 (pos) + 2 (mem) = 10
    auto variant = nf::Individual::random(10, {6, 4}, 7, rng);

    auto fighter = nf::convert_variant_to_fighter(variant, design);

    // Arena input: 5 (full) + 1 (sight) + 6 (squad leader) + 2 (mem) = 14
    EXPECT_EQ(fighter.topology.input_size, 14u);
    // Same hidden layers
    EXPECT_EQ(fighter.topology.layers.size(), variant.topology.layers.size());
    EXPECT_EQ(fighter.topology.layers[0].output_size, 6u);
    EXPECT_EQ(fighter.topology.layers[1].output_size, 4u);
    // Same output
    EXPECT_EQ(fighter.topology.layers[2].output_size, 7u);
}

TEST(EvolutionTest, ConvertVariantToFighterSensorWeightsPreserved) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef full_sensor;
    full_sensor.type = nf::SensorType::Occulus;
    full_sensor.angle = 0.0f;
    full_sensor.range = 200.0f;
    full_sensor.width = 0.1f;
    full_sensor.is_full_sensor = true;
    design.sensors.push_back(full_sensor);
    design.memory_slots = 2;

    // Scroller: 4 (full sensor) + 3 (pos) + 2 (mem) = 9
    auto variant = nf::Individual::random(9, {4}, 5, rng);
    auto old_weights = variant.genome.gene("layer_0_weights").values;

    auto fighter = nf::convert_variant_to_fighter(variant, design);
    auto new_weights = fighter.genome.gene("layer_0_weights").values;

    // Arena: 5 (full sensor) + 6 (squad leader) + 2 (mem) = 13
    EXPECT_EQ(fighter.topology.input_size, 13u);
    std::size_t hidden_size = 4;

    // For each output node, check distance and is_tower weights
    for (std::size_t out = 0; out < hidden_size; ++out) {
        // Distance weight (arena col 0 = scroller col 0)
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 0], old_weights[out * 9 + 0]);
        // is_tower weight (arena col 1 = scroller col 1)
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 1], old_weights[out * 9 + 1]);
    }
}

TEST(EvolutionTest, ConvertVariantToFighterNewInputsSmallRandom) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef full_sensor;
    full_sensor.type = nf::SensorType::Occulus;
    full_sensor.angle = 0.0f;
    full_sensor.range = 200.0f;
    full_sensor.width = 0.1f;
    full_sensor.is_full_sensor = true;
    design.sensors.push_back(full_sensor);
    design.memory_slots = 2;

    auto variant = nf::Individual::random(9, {4}, 5, rng);
    auto fighter = nf::convert_variant_to_fighter(variant, design);
    auto new_weights = fighter.genome.gene("layer_0_weights").values;

    std::size_t hidden_size = 4;
    // Arena layout: [dist, tower, is_token, is_friend, is_bullet] [6 squad leader] [2 mem]
    // Indices 2,3,4 = new arena sensor values, 5-10 = squad leader
    auto old_weights = variant.genome.gene("layer_0_weights").values;
    for (std::size_t out = 0; out < hidden_size; ++out) {
        // is_token (arena col 2) -- mapped from scroller col 3
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 2], old_weights[out * 9 + 3]);
        // is_friend (arena col 3) -- small random (new input via adapt_topology_inputs)
        EXPECT_LE(std::abs(new_weights[out * 13 + 3]), 0.1f);
        // is_bullet (arena col 4) -- small random
        EXPECT_LE(std::abs(new_weights[out * 13 + 4]), 0.1f);
        // Squad leader inputs (cols 5-10) -- small random
        for (int sl = 5; sl <= 10; ++sl) {
            EXPECT_LE(std::abs(new_weights[out * 13 + sl]), 0.1f);
        }
    }
}

TEST(EvolutionTest, ConvertVariantToFighterMemoryWeightsPreserved) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef full_sensor;
    full_sensor.type = nf::SensorType::Occulus;
    full_sensor.angle = 0.0f;
    full_sensor.range = 200.0f;
    full_sensor.width = 0.1f;
    full_sensor.is_full_sensor = true;
    design.sensors.push_back(full_sensor);
    design.memory_slots = 2;

    auto variant = nf::Individual::random(9, {4}, 5, rng);
    auto old_weights = variant.genome.gene("layer_0_weights").values;
    auto fighter = nf::convert_variant_to_fighter(variant, design);
    auto new_weights = fighter.genome.gene("layer_0_weights").values;

    std::size_t hidden_size = 4;
    // Scroller memory at cols 7,8 (after 4 sensors + 3 pos)
    // Arena memory at cols 11,12 (after 5 sensors + 6 squad leader)
    for (std::size_t out = 0; out < hidden_size; ++out) {
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 11], old_weights[out * 9 + 7]);
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 12], old_weights[out * 9 + 8]);
    }
}

TEST(EvolutionTest, ConvertVariantToFighterHigherLayersUnchanged) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef s;
    s.type = nf::SensorType::Occulus;
    s.is_full_sensor = true;
    s.angle = 0.0f;
    s.range = 200.0f;
    s.width = 0.1f;
    design.sensors.push_back(s);
    design.memory_slots = 2;

    auto variant = nf::Individual::random(9, {4, 3}, 5, rng);
    auto fighter = nf::convert_variant_to_fighter(variant, design);

    // Layer 1 weights should be identical
    auto old_l1 = variant.genome.gene("layer_1_weights").values;
    auto new_l1 = fighter.genome.gene("layer_1_weights").values;
    EXPECT_EQ(old_l1.size(), new_l1.size());
    for (std::size_t i = 0; i < old_l1.size(); ++i) {
        EXPECT_FLOAT_EQ(new_l1[i], old_l1[i]);
    }
}

TEST(EvolutionTest, ConvertVariantToFighterNetworkWorks) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    nf::SensorDef s;
    s.type = nf::SensorType::Occulus;
    s.is_full_sensor = true;
    s.angle = 0.0f;
    s.range = 200.0f;
    s.width = 0.1f;
    design.sensors.push_back(s);
    design.memory_slots = 2;

    auto variant = nf::Individual::random(9, {6}, 5, rng);
    auto fighter = nf::convert_variant_to_fighter(variant, design);

    auto net = fighter.build_network();
    EXPECT_EQ(net.input_size(), 13u);
    EXPECT_EQ(net.output_size(), 5u);

    // Forward pass should work
    std::vector<float> input(13, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 5u);
}

// --- from_design + adapt_individual_inputs integration tests ---

TEST(EvolutionTest, FromDesignSetsInputIds) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    EXPECT_FALSE(ind.topology.input_ids.empty());
    EXPECT_EQ(ind.topology.input_ids.size(), ind.topology.input_size);
    EXPECT_FALSE(ind.topology.output_ids.empty());
}

TEST(EvolutionTest, AdaptIndividualNoOp) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    auto target = ind.topology.input_ids;  // same IDs
    auto [adapted, report] = nf::adapt_individual_inputs(
        ind, target, design, rng);
    EXPECT_FALSE(report.needed());
}

TEST(EvolutionTest, AdaptIndividualAddsRemoves) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    // Modify target: add a new ID, remove one existing
    auto target = ind.topology.input_ids;
    target.push_back("NEW_INPUT");
    if (!target.empty()) target.erase(target.begin());
    auto [adapted, report] = nf::adapt_individual_inputs(
        ind, target, design, rng);
    EXPECT_TRUE(report.needed());
    EXPECT_FALSE(report.added.empty());
    EXPECT_FALSE(report.removed.empty());
    // Adapted individual should have the new topology size
    EXPECT_EQ(adapted.topology.input_size, target.size());
}

TEST(EvolutionTest, AdaptIndividualLegacyNoIds) {
    // Individual with no input_ids (legacy) should pass through unchanged
    nf::ShipDesign design;
    design.memory_slots = 0;
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(5, {4}, 3, rng);
    // Ensure no IDs
    ASSERT_TRUE(ind.topology.input_ids.empty());
    auto [adapted, report] = nf::adapt_individual_inputs(
        ind, {"a", "b", "c", "d", "e"}, design, rng);
    EXPECT_FALSE(report.needed());
}

// --- mlp_to_graph tests ---

TEST(EvolutionTest, MlpToGraphGenomeDenseTopology) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(4, {3}, 2, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};

    auto genome = mlp_snapshot_to_graph_genome(snap, rng);

    // Node counts: 4 input + 3 hidden + 2 output = 9
    ASSERT_EQ(genome.nodes.size(), 9u);

    std::size_t input_count = 0, hidden_count = 0, output_count = 0;
    for (const auto& n : genome.nodes) {
        if (n.role == evolve::NodeRole::Input) ++input_count;
        else if (n.role == evolve::NodeRole::Hidden) ++hidden_count;
        else if (n.role == evolve::NodeRole::Output) ++output_count;
    }
    EXPECT_EQ(input_count, 4u);
    EXPECT_EQ(hidden_count, 3u);
    EXPECT_EQ(output_count, 2u);

    // Connection counts: (4*3) + (3*2) = 12 + 6 = 18 dense connections
    EXPECT_EQ(genome.connections.size(), 18u);
    for (const auto& c : genome.connections) {
        EXPECT_TRUE(c.enabled);
    }

    // All innovation numbers should be unique
    std::set<uint32_t> innovations;
    for (const auto& c : genome.connections) {
        innovations.insert(c.innovation);
    }
    EXPECT_EQ(innovations.size(), genome.connections.size());

    // Should build a valid GraphNetwork
    neuralnet::GraphNetwork net(genome);
    EXPECT_EQ(net.input_size(), 4u);
    EXPECT_EQ(net.output_size(), 2u);

    // Forward pass should work
    std::vector<float> input = {1.0f, -0.5f, 0.2f, 0.8f};
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 2u);
}

TEST(EvolutionTest, MlpToGraphGenomeMultipleHiddenLayers) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(7, {4, 8}, 1, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};

    auto genome = mlp_snapshot_to_graph_genome(snap, rng);

    // 7 input + 4 hidden + 8 hidden + 1 output = 20 nodes
    EXPECT_EQ(genome.nodes.size(), 20u);

    // (7*4) + (4*8) + (8*1) = 28 + 32 + 8 = 68 connections
    EXPECT_EQ(genome.connections.size(), 68u);

    neuralnet::GraphNetwork net(genome);
    EXPECT_EQ(net.input_size(), 7u);
    EXPECT_EQ(net.output_size(), 1u);

    std::vector<float> input(7, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 1u);
}

TEST(EvolutionTest, MlpToGraphWithWeightsPreservesValues) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(3, {2}, 1, rng);
    auto net_mlp = ind.build_network();

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (ind.genome.has_gene(lp + "_weights")) {
            const auto& wg = ind.genome.gene(lp + "_weights");
            snap.weights.insert(snap.weights.end(), wg.values.begin(), wg.values.end());
        }
        if (ind.genome.has_gene(lp + "_biases")) {
            const auto& bg = ind.genome.gene(lp + "_biases");
            snap.weights.insert(snap.weights.end(), bg.values.begin(), bg.values.end());
        }
    }

    auto genome = mlp_snapshot_to_graph_genome_with_weights(snap);
    neuralnet::GraphNetwork net_graph(genome);

    std::vector<float> input = {0.5f, -0.3f, 0.8f};
    auto out_mlp = net_mlp.forward(input);
    auto out_graph = net_graph.forward(input);

    ASSERT_EQ(out_mlp.size(), out_graph.size());
    for (std::size_t i = 0; i < out_mlp.size(); ++i) {
        EXPECT_NEAR(out_mlp[i], out_graph[i], 1e-5f) << "output " << i;
    }
}

TEST(EvolutionTest, MlpToGraphWithWeightsTargetInputSize) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(3, {2}, 1, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (ind.genome.has_gene(lp + "_weights"))
            for (auto v : ind.genome.gene(lp + "_weights").values) snap.weights.push_back(v);
        if (ind.genome.has_gene(lp + "_biases"))
            for (auto v : ind.genome.gene(lp + "_biases").values) snap.weights.push_back(v);
    }

    auto genome = mlp_snapshot_to_graph_genome_with_weights(snap, 5, 1);
    neuralnet::GraphNetwork net(genome);

    EXPECT_EQ(net.input_size(), 5u);
    EXPECT_EQ(net.output_size(), 1u);

    std::vector<float> input(5, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 1u);
}


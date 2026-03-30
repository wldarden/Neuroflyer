#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>

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

// --- convert_variant_to_fighter tests ---

TEST(Evolution, ConvertVariantToFighterTopology) {
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

TEST(Evolution, ConvertVariantToFighterSensorWeightsPreserved) {
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

TEST(Evolution, ConvertVariantToFighterNewInputsZero) {
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
    for (std::size_t out = 0; out < hidden_size; ++out) {
        // is_token (arena col 2) -- zero
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 2], 0.0f);
        // is_friend (arena col 3) -- zero
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 3], 0.0f);
        // is_bullet (arena col 4) -- zero
        EXPECT_FLOAT_EQ(new_weights[out * 13 + 4], 0.0f);
        // Squad leader inputs (cols 5-10) -- all zero
        for (int sl = 5; sl <= 10; ++sl) {
            EXPECT_FLOAT_EQ(new_weights[out * 13 + sl], 0.0f);
        }
    }
}

TEST(Evolution, ConvertVariantToFighterMemoryWeightsPreserved) {
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

TEST(Evolution, ConvertVariantToFighterHigherLayersUnchanged) {
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

TEST(Evolution, ConvertVariantToFighterNetworkWorks) {
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


#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace nf = neuroflyer;

TEST(SnapshotIOTest, RoundTrip_BasicSnapshot) {
    nf::Snapshot snap;
    snap.name = "TestNet";
    snap.generation = 42;
    snap.created_timestamp = 1711300000;
    snap.parent_name = "ParentNet";
    snap.ship_design.memory_slots = 4;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true},
        {nf::SensorType::Raycast, 1.5f, 200.0f, 0.0f, false},
        {nf::SensorType::Occulus, 0.5f, 120.0f, 0.3f, true},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {
        {std::size_t{12}, neuralnet::Activation::Tanh, {}},
        {std::size_t{9}, neuralnet::Activation::Tanh, {}},
    };
    snap.weights.resize(200, 0.5f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    EXPECT_EQ(loaded.name, "TestNet");
    EXPECT_EQ(loaded.generation, 42u);
    EXPECT_EQ(loaded.created_timestamp, 1711300000);
    EXPECT_EQ(loaded.parent_name, "ParentNet");
    EXPECT_EQ(loaded.ship_design.memory_slots, 4);
    ASSERT_EQ(loaded.ship_design.sensors.size(), 3u);
    EXPECT_EQ(loaded.ship_design.sensors[0].type, nf::SensorType::Raycast);
    EXPECT_TRUE(loaded.ship_design.sensors[0].is_full_sensor);
    EXPECT_FALSE(loaded.ship_design.sensors[1].is_full_sensor);
    EXPECT_EQ(loaded.ship_design.sensors[2].type, nf::SensorType::Occulus);
    EXPECT_FLOAT_EQ(loaded.ship_design.sensors[2].width, 0.3f);
    EXPECT_EQ(loaded.topology.input_size, snap.topology.input_size);
    ASSERT_EQ(loaded.topology.layers.size(), 2u);
    EXPECT_EQ(loaded.topology.layers[0].output_size, std::size_t{12});
    EXPECT_EQ(loaded.weights.size(), snap.weights.size());
    EXPECT_FLOAT_EQ(loaded.weights[0], 0.5f);
}

TEST(SnapshotIOTest, RoundTrip_RootGenome) {
    nf::Snapshot snap;
    snap.name = "MyGenome";
    snap.generation = 0;
    snap.parent_name = "";
    snap.ship_design.memory_slots = 0;
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{5}, neuralnet::Activation::ReLU, {}}};
    snap.weights.resize(3 * 5 + 5, 0.1f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    EXPECT_EQ(loaded.parent_name, "");
    EXPECT_EQ(loaded.generation, 0u);
}

TEST(SnapshotIOTest, CorruptMagic_Throws) {
    std::stringstream ss;
    uint32_t bad_magic = 0xDEADBEEF;
    ss.write(reinterpret_cast<const char*>(&bad_magic), sizeof(bad_magic));
    ss.seekg(0);
    EXPECT_THROW(static_cast<void>(nf::load_snapshot(ss)), std::runtime_error);
}

TEST(SnapshotIOTest, CorruptCRC_Throws) {
    nf::Snapshot snap;
    snap.name = "Test";
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(3 * 2 + 2, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);

    // Corrupt a byte in the middle of the payload
    std::string data = ss.str();
    std::size_t mid = 10 + data.size() / 2;
    if (mid < data.size()) data[mid] ^= 0xFF;
    std::stringstream corrupted(data);
    EXPECT_THROW(static_cast<void>(nf::load_snapshot(corrupted)), std::runtime_error);
}

TEST(SnapshotIOTest, FutureVersion_Throws) {
    nf::Snapshot snap;
    snap.name = "Test";
    snap.topology.input_size = 3;
    snap.topology.layers = {{std::size_t{2}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(3 * 2 + 2, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);

    std::string data = ss.str();
    uint16_t bad_version = 99;
    std::memcpy(&data[4], &bad_version, sizeof(bad_version));
    std::stringstream bad_ver(data);
    EXPECT_THROW(static_cast<void>(nf::load_snapshot(bad_ver)), std::runtime_error);
}

TEST(SnapshotIOTest, ReadHeader_OnlyGetsMetadata) {
    nf::Snapshot snap;
    snap.name = "BigNet";
    snap.generation = 100;
    snap.created_timestamp = 1711300000;
    snap.parent_name = "Parent";
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 7;
    snap.topology.layers = {{std::size_t{12}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(100, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto header = nf::read_snapshot_header(ss);

    EXPECT_EQ(header.name, "BigNet");
    EXPECT_EQ(header.generation, 100u);
    EXPECT_EQ(header.parent_name, "Parent");
    EXPECT_EQ(header.created_timestamp, 1711300000);
}

TEST(SnapshotIOTest, RoundTrip_SensorIds) {
    nf::Snapshot snap;
    snap.name = "IdTest";
    snap.generation = 1;
    snap.ship_design.memory_slots = 2;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 100},
        {nf::SensorType::Occulus, 0.5f, 120.0f, 0.3f, false, 200},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {{std::size_t{5}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(50, 0.1f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    ASSERT_EQ(loaded.ship_design.sensors.size(), 2u);
    EXPECT_EQ(loaded.ship_design.sensors[0].id, 100u);
    EXPECT_EQ(loaded.ship_design.sensors[1].id, 200u);
}

TEST(SnapshotIOTest, V2Backfill_SensorsGetIds) {
    nf::Snapshot snap;
    snap.name = "OldFormat";
    snap.generation = 1;
    snap.ship_design.memory_slots = 0;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 0},
        {nf::SensorType::Raycast, 0.5f, 200.0f, 0.0f, false, 0},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {{std::size_t{3}, neuralnet::Activation::Tanh, {}}};
    snap.weights.resize(20, 0.0f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    // After loading, sensors with id==0 should have been backfilled
    for (const auto& s : loaded.ship_design.sensors) {
        EXPECT_NE(s.id, 0u);
    }
}

TEST(SnapshotIOTest, V6PairedFighterName) {
    nf::Snapshot snap;
    snap.name = "squad-v1";
    snap.generation = 50;
    snap.created_timestamp = 1234567890;
    snap.parent_name = "squad-root";
    snap.run_count = 3;
    snap.paired_fighter_name = "elite-fighter-v3";
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 8;
    snap.topology.layers = {{4, neuralnet::Activation::Tanh, {}}, {4, neuralnet::Activation::Tanh, {}}};
    snap.weights = {0.1f, 0.2f, 0.3f};

    std::ostringstream out;
    nf::save_snapshot(snap, out);

    std::istringstream in(out.str());
    auto loaded = nf::load_snapshot(in);

    EXPECT_EQ(loaded.name, "squad-v1");
    EXPECT_EQ(loaded.paired_fighter_name, "elite-fighter-v3");
    EXPECT_EQ(loaded.run_count, 3u);
}

TEST(SnapshotIOTest, V6HeaderPairedFighterName) {
    nf::Snapshot snap;
    snap.name = "squad-v2";
    snap.generation = 10;
    snap.paired_fighter_name = "my-fighter";
    snap.ship_design.memory_slots = 2;
    snap.topology.input_size = 4;
    snap.topology.layers = {{2, neuralnet::Activation::Tanh, {}}};
    snap.weights = {0.5f};

    std::ostringstream out;
    nf::save_snapshot(snap, out);

    std::istringstream in(out.str());
    auto hdr = nf::read_snapshot_header(in);

    EXPECT_EQ(hdr.name, "squad-v2");
    EXPECT_EQ(hdr.paired_fighter_name, "my-fighter");
}

TEST(SnapshotIOTest, V6EmptyPairedFighterForFighterSnapshot) {
    nf::Snapshot snap;
    snap.name = "regular-variant";
    snap.generation = 5;
    snap.ship_design.memory_slots = 4;
    snap.topology.input_size = 8;
    snap.topology.layers = {{4, neuralnet::Activation::Tanh, {}}};
    snap.weights = {0.1f};
    // paired_fighter_name left empty (default)

    std::ostringstream out;
    nf::save_snapshot(snap, out);
    std::istringstream in(out.str());
    auto loaded = nf::load_snapshot(in);
    EXPECT_TRUE(loaded.paired_fighter_name.empty());
}

TEST(SnapshotIOTest, V8RoundTripWithNodeIds) {
    // Create a snapshot with node IDs
    nf::Snapshot snap;
    snap.name = "test-v8-ids";
    snap.topology.input_size = 3;
    snap.topology.input_ids = {"sensor_0", "heading", "speed"};
    snap.topology.layers = {
        {.output_size = 2, .activation = neuralnet::Activation::Tanh, .node_activations = {}}
    };
    snap.topology.output_ids = {"left", "right"};
    snap.ship_design.memory_slots = 0;
    snap.net_type = nf::NetType::Fighter;
    // Weights: 3*2 weights + 2 biases = 8 floats
    snap.weights = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};

    // Save to temp file
    auto tmp = std::filesystem::temp_directory_path() / "test_v8_ids.bin";
    nf::save_snapshot(snap, tmp.string());

    // Load back
    auto loaded = nf::load_snapshot(tmp.string());

    // Verify IDs survived
    ASSERT_EQ(loaded.topology.input_ids.size(), 3u);
    EXPECT_EQ(loaded.topology.input_ids[0], "sensor_0");
    EXPECT_EQ(loaded.topology.input_ids[1], "heading");
    EXPECT_EQ(loaded.topology.input_ids[2], "speed");
    ASSERT_EQ(loaded.topology.output_ids.size(), 2u);
    EXPECT_EQ(loaded.topology.output_ids[0], "left");
    EXPECT_EQ(loaded.topology.output_ids[1], "right");

    // Verify other data survived too
    EXPECT_EQ(loaded.name, "test-v8-ids");
    EXPECT_EQ(loaded.topology.input_size, 3u);
    EXPECT_EQ(loaded.weights.size(), 8u);

    std::filesystem::remove(tmp);
}

TEST(SnapshotIOTest, V8EmptyIdsRoundTrip) {
    // Snapshot with no IDs (legacy behavior) should save and load fine
    nf::Snapshot snap;
    snap.name = "test-v8-no-ids";
    snap.topology.input_size = 2;
    snap.topology.layers = {
        {.output_size = 1, .activation = neuralnet::Activation::Tanh, .node_activations = {}}
    };
    // input_ids and output_ids left empty
    snap.weights = {0.1f, 0.2f, 0.3f};

    auto tmp = std::filesystem::temp_directory_path() / "test_v8_no_ids.bin";
    nf::save_snapshot(snap, tmp.string());
    auto loaded = nf::load_snapshot(tmp.string());

    EXPECT_TRUE(loaded.topology.input_ids.empty());
    EXPECT_TRUE(loaded.topology.output_ids.empty());
    EXPECT_EQ(loaded.topology.input_size, 2u);

    std::filesystem::remove(tmp);
}

#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>
#include <algorithm>

namespace nf = neuroflyer;

TEST(ShipDesignTest, InputSize_AllFullSensors) {
    nf::ShipDesign design;
    design.memory_slots = 4;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, true},
    };
    EXPECT_EQ(nf::compute_input_size(design), 15u);
}

TEST(ShipDesignTest, InputSize_MixedSensors) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, false},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, true},
        {nf::SensorType::Occulus, 1.0f, 120.0f, 0.3f, true},
    };
    EXPECT_EQ(nf::compute_input_size(design), 14u);
}

TEST(ShipDesignTest, InputSize_NoSensors) {
    nf::ShipDesign design;
    design.memory_slots = 0;
    EXPECT_EQ(nf::compute_input_size(design), 3u);
}

TEST(ShipDesignTest, OutputSize) {
    nf::ShipDesign design;
    design.memory_slots = 4;
    EXPECT_EQ(nf::compute_output_size(design), 9u);
}

TEST(ShipDesignTest, OutputSize_ZeroMemory) {
    nf::ShipDesign design;
    design.memory_slots = 0;
    EXPECT_EQ(nf::compute_output_size(design), 5u);
}

TEST(ShipDesignTest, AssignSensorIds_FillsZeroIds) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 0},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false, 0},
        {nf::SensorType::Occulus, 1.0f, 120.0f, 0.3f, true, 0},
    };
    nf::assign_sensor_ids(design);
    for (const auto& s : design.sensors) {
        EXPECT_NE(s.id, 0u);
    }
    // All IDs should be unique
    std::vector<uint16_t> ids;
    for (const auto& s : design.sensors) ids.push_back(s.id);
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::unique(ids.begin(), ids.end()), ids.end());
}

TEST(ShipDesignTest, AssignSensorIds_PreservesExistingIds) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 42},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false, 0},
    };
    nf::assign_sensor_ids(design);
    EXPECT_EQ(design.sensors[0].id, 42u);
    EXPECT_NE(design.sensors[1].id, 0u);
    EXPECT_NE(design.sensors[1].id, 42u);
}

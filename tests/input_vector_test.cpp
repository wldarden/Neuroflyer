#include <neuroflyer/config.h>
#include <neuroflyer/game.h>
#include <neuroflyer/ray.h>
#include <neuroflyer/sensor_engine.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace nf = neuroflyer;

TEST(InputVectorTest, SizeCorrectWithMemorySlots) {
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;
    design.memory_slots = 0;  // will use legacy fallback

    std::vector<float> mem4(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem4);
    EXPECT_EQ(input.size(), 31u + 4u);  // 31 sensor + 4 memory

    std::vector<float> mem8(8, 0.0f);
    auto input2 = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem8);
    EXPECT_EQ(input2.size(), 31u + 8u);

    std::vector<float> mem0;
    auto input3 = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem0);
    EXPECT_EQ(input3.size(), 31u);
}

TEST(InputVectorTest, NothingDetected_AllDistancesOne) {
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;  // empty -> legacy fallback

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // All 8 sight rays should be 1.0 (nothing detected)
    for (int i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(input[static_cast<std::size_t>(i)], 1.0f)
            << "Sight ray " << i << " should be 1.0 when nothing detected";
    }

    // 5 sensor rays: each has [dist, is_dangerous, is_valuable, is_coin]
    for (int s = 0; s < 5; ++s) {
        std::size_t base = 8 + static_cast<std::size_t>(s) * 4;
        EXPECT_FLOAT_EQ(input[base + 0], 1.0f)
            << "Sensor " << s << " distance should be 1.0";
        EXPECT_FLOAT_EQ(input[base + 1], 0.0f)
            << "Sensor " << s << " is_dangerous should be 0.0";
        EXPECT_FLOAT_EQ(input[base + 2], 0.0f)
            << "Sensor " << s << " is_valuable should be 0.0";
        EXPECT_FLOAT_EQ(input[base + 3], 0.0f)
            << "Sensor " << s << " is_coin should be 0.0";
    }
}

TEST(InputVectorTest, TowerOnSensorRay_DangerousIsOne) {
    // Place a tower directly ahead of the ship on the center sensor ray
    // Legacy layout: sensor rays are at indices {2,4,6,8,10} of 13 rays
    // Sensor index 2 (center) is ray 6 -> angle = 0 (straight ahead)
    // Place tower directly ahead at ~150px distance
    std::vector<nf::Tower> towers = {
        {.x = 400.0f, .y = 250.0f, .radius = 20.0f, .alive = true}
    };
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;  // empty -> legacy fallback

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // Sensor ray 2 (center, angle=0) should detect the tower.
    // In legacy layout, sensor rays come after 8 sight rays.
    // Center sensor (index 2 of the 5 sensors) at input offset 8 + 2*4 = 16
    std::size_t center_base = 8 + 2 * 4;
    EXPECT_LT(input[center_base + 0], 1.0f);    // distance < 1.0
    EXPECT_FLOAT_EQ(input[center_base + 1], 1.0f);  // is_dangerous
    EXPECT_FLOAT_EQ(input[center_base + 2], 0.0f);  // is_valuable
    EXPECT_FLOAT_EQ(input[center_base + 3], 0.0f);  // is_coin
}

TEST(InputVectorTest, TokenOnSensorRay_CoinAndValuableSet) {
    // Place a token directly ahead of the ship
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens = {
        {.x = 400.0f, .y = 250.0f, .radius = 10.0f, .alive = true}
    };
    nf::ShipDesign design;  // empty -> legacy fallback

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    std::size_t center_base = 8 + 2 * 4;
    EXPECT_LT(input[center_base + 0], 1.0f);        // distance < 1.0
    EXPECT_FLOAT_EQ(input[center_base + 1], 0.0f);  // is_dangerous = 0
    EXPECT_FLOAT_EQ(input[center_base + 2], 0.5f);  // is_valuable = 500/(500*2) = 0.5
    EXPECT_FLOAT_EQ(input[center_base + 3], 1.0f);  // is_coin
}

TEST(InputVectorTest, TowerOnSightRay_OnlyDistancePresent) {
    // Place a tower to the far left where only the first sight ray (index 0) can see it
    // Legacy ray 0 -> angle = -PI/2 (far left), direction = (-1, 0)
    std::vector<nf::Tower> towers = {
        {.x = 250.0f, .y = 400.0f, .radius = 20.0f, .alive = true}
    };
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;  // empty -> legacy fallback

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // Sight ray 0 should detect the tower (distance < 1.0)
    EXPECT_LT(input[0], 1.0f);
    // Next value should be sight ray 1's distance, NOT a type flag
    EXPECT_FLOAT_EQ(input[1], 1.0f);  // ray 1 has nothing
}

TEST(InputVectorTest, PositionCentered) {
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;

    std::vector<float> mem(4, 0.0f);
    // Ship at center of 800x800 game area
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // Position: (400/800)*2-1 = 0.0, 1-(400/800)*2 = 0.0
    EXPECT_FLOAT_EQ(input[28], 0.0f);  // POS X = center
    EXPECT_FLOAT_EQ(input[29], 0.0f);  // POS Y = center
}

TEST(InputVectorTest, PositionRange) {
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;

    std::vector<float> mem(4, 0.0f);

    // Right edge: x=800 in 800-wide area -> (800/800)*2-1 = 1.0
    auto right = nf::build_ship_input(design, 800.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);
    EXPECT_FLOAT_EQ(right[28], 1.0f);

    // Left edge: x=0 in 800-wide area -> (0/800)*2-1 = -1.0
    auto left = nf::build_ship_input(design, 0.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);
    EXPECT_FLOAT_EQ(left[28], -1.0f);

    // Top: y=0 in 800-high area -> 1-(0/800)*2 = 1.0
    auto top = nf::build_ship_input(design, 400.0f, 0.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);
    EXPECT_FLOAT_EQ(top[29], 1.0f);

    // Bottom: y=800 in 800-high area -> 1-(800/800)*2 = -1.0
    auto bottom = nf::build_ship_input(design, 400.0f, 800.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);
    EXPECT_FLOAT_EQ(bottom[29], -1.0f);
}

TEST(InputVectorTest, MemorySlotsAppendedAtEnd) {
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;

    std::vector<float> mem = {0.1f, 0.2f, -0.5f, 0.9f};
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // Memory starts at index 31 (after 28 sensor + 3 pos/speed)
    EXPECT_FLOAT_EQ(input[31], 0.1f);
    EXPECT_FLOAT_EQ(input[32], 0.2f);
    EXPECT_FLOAT_EQ(input[33], -0.5f);
    EXPECT_FLOAT_EQ(input[34], 0.9f);
}

TEST(InputVectorTest, RaycastHitDetection) {
    // Verify that actual raycasting via sensor engine correctly detects a tower
    // Tower directly ahead at (400, 300), ship at (400, 400)
    std::vector<nf::Tower> towers = {
        {.x = 400.0f, .y = 300.0f, .radius = 20.0f, .alive = true}
    };
    std::vector<nf::Token> tokens;
    nf::ShipDesign design;

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // Center sensor ray (sensor index 2, at offset 8 + 2*4 = 16) should detect tower
    std::size_t center_base = 8 + 2 * 4;
    EXPECT_LT(input[center_base], 1.0f);
    EXPECT_FLOAT_EQ(input[center_base + 1], 1.0f);  // is_dangerous

    // Far side sight rays should not hit it
    EXPECT_FLOAT_EQ(input[0], 1.0f);  // leftmost sight ray
}

TEST(InputVectorTest, IsValuableNormalization) {
    // Token directly ahead
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens = {
        {.x = 400.0f, .y = 250.0f, .radius = 10.0f, .alive = true}
    };
    nf::ShipDesign design;

    std::vector<float> mem(4, 0.0f);
    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    std::size_t center_base = 8 + 2 * 4;
    EXPECT_FLOAT_EQ(input[center_base + 2], 0.5f);  // 500/(500*2) = 0.5

    // With very low token value
    auto input2 = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 10.0f, towers, tokens, mem);
    EXPECT_FLOAT_EQ(input2[center_base + 2], 0.5f);  // still 0.5 — 10/(10*2)
}

TEST(InputVectorTest, CustomShipDesignSizeCorrect) {
    // Custom design with 3 sight-only sensors and 2 full sensors
    nf::ShipDesign design;
    design.memory_slots = 2;
    design.sensors.push_back({nf::SensorType::Raycast, -1.0f, 200.0f, 0.0f, false});
    design.sensors.push_back({nf::SensorType::Raycast, 0.0f, 200.0f, 0.0f, false});
    design.sensors.push_back({nf::SensorType::Raycast, 1.0f, 200.0f, 0.0f, false});
    design.sensors.push_back({nf::SensorType::Raycast, -0.5f, 200.0f, 0.0f, true});
    design.sensors.push_back({nf::SensorType::Raycast, 0.5f, 200.0f, 0.0f, true});

    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    std::vector<float> mem(2, 0.0f);

    auto input = nf::build_ship_input(design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    // 3 sight (1 each) + 2 full (4 each) + 3 pos/speed + 2 memory = 16
    EXPECT_EQ(input.size(), 3u + 8u + 3u + 2u);
}

TEST(InputVectorTest, LegacyFallbackMatchesEmptyDesign) {
    // A design with no sensors should produce the same result as the legacy layout
    nf::ShipDesign empty_design;
    empty_design.memory_slots = 4;

    auto legacy = nf::create_legacy_ship_design(4);

    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    std::vector<float> mem(4, 0.0f);

    auto input_empty = nf::build_ship_input(empty_design, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);
    auto input_legacy = nf::build_ship_input(legacy, 400.0f, 400.0f,
        800.0f, 800.0f, 2.0f, 500.0f, towers, tokens, mem);

    ASSERT_EQ(input_empty.size(), input_legacy.size());
    for (std::size_t i = 0; i < input_empty.size(); ++i) {
        EXPECT_FLOAT_EQ(input_empty[i], input_legacy[i]) << "Mismatch at index " << i;
    }
}

TEST(InputVectorTest, DecodeOutputBasic) {
    std::vector<float> output = {0.5f, -0.3f, 0.1f, -0.9f, 0.7f, 0.3f, -0.2f};
    auto decoded = nf::decode_output(output, 2);

    EXPECT_TRUE(decoded.up);
    EXPECT_FALSE(decoded.down);
    EXPECT_TRUE(decoded.left);
    EXPECT_FALSE(decoded.right);
    EXPECT_TRUE(decoded.shoot);
    ASSERT_EQ(decoded.memory.size(), 2u);
    EXPECT_FLOAT_EQ(decoded.memory[0], 0.3f);
    EXPECT_FLOAT_EQ(decoded.memory[1], -0.2f);
}

TEST(InputVectorTest, DecodeOutputEmptyMemory) {
    std::vector<float> output = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    auto decoded = nf::decode_output(output, 0);

    EXPECT_TRUE(decoded.up);
    EXPECT_TRUE(decoded.down);
    EXPECT_TRUE(decoded.left);
    EXPECT_TRUE(decoded.right);
    EXPECT_TRUE(decoded.shoot);
    EXPECT_TRUE(decoded.memory.empty());
}

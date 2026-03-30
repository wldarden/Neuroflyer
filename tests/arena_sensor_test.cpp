#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>
#include <cmath>

namespace nf = neuroflyer;

TEST(ArenaSensorTest, RaycastDetectsEnemyShip) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Raycast;
    sensor.angle = 0.0f;  // straight ahead
    sensor.range = 300.0f;
    sensor.width = 0.0f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;  // facing up
    ctx.self_index = 0;
    ctx.self_team = 0;

    // Index 0 = self (skipped), index 1 = enemy target
    std::vector<nf::Triangle> ships = {
        nf::Triangle(500.0f, 500.0f),  // self placeholder
        nf::Triangle(500.0f, 400.0f),  // enemy ahead
    };
    std::vector<int> ship_teams = {0, 1};  // self=team0, enemy=team1
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 0.5f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::EnemyShip);
}

TEST(ArenaSensorTest, RaycastDetectsBullet) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Raycast;
    sensor.angle = 0.0f;
    sensor.range = 300.0f;
    sensor.width = 0.0f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    nf::Bullet b;
    b.x = 500.0f;
    b.y = 400.0f;
    b.alive = true;
    b.owner_index = 5;
    std::vector<nf::Bullet> bullets = {b};
    ctx.bullets = bullets;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 0.5f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::Bullet);
}

TEST(ArenaSensorTest, FriendlyShipDetectedAsFriend) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Raycast;
    sensor.angle = 0.0f;
    sensor.range = 300.0f;
    sensor.width = 0.0f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    // Index 0 = self (skipped), index 1 = friendly target
    std::vector<nf::Triangle> ships = {
        nf::Triangle(500.0f, 500.0f),  // self placeholder
        nf::Triangle(500.0f, 400.0f),  // friend ahead
    };
    std::vector<int> ship_teams = {0, 0};  // both same team = friend
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::FriendlyShip);
}

TEST(ArenaSensorTest, DirRangeComputation) {
    auto dr = nf::compute_dir_range(0, 0, 100, 0, 200, 200);
    EXPECT_NEAR(dr.dir_sin, 1.0f, 0.01f);
    EXPECT_NEAR(dr.dir_cos, 0.0f, 0.01f);
    float expected_range = 100.0f / std::sqrt(200.0f * 200.0f + 200.0f * 200.0f);
    EXPECT_NEAR(dr.range, expected_range, 0.01f);
}

TEST(ArenaSensorTest, ArenaInputSize) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},   // full: 5 vals
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false, 2},  // sight: 1 val
    };
    design.memory_slots = 4;
    auto size = nf::compute_arena_input_size(design, 6);
    // 5 + 1 (sensors) + 3 (pos) + 7 (nav) + 6 (squad leader) + 4 (memory) = 26
    EXPECT_EQ(size, 26u);
}

TEST(ArenaSensorTest, BuildArenaShipInputSize) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.world_w = 1000.0f;
    ctx.world_h = 1000.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    std::vector<float> memory = {0.0f, 0.0f};

    auto input = nf::build_arena_ship_input(
        design, ctx,
        0.0f, 1.0f, 0.5f,     // target dir+range
        0.0f, -1.0f, 0.3f,    // home dir+range
        0.8f,                   // base hp
        0.5f, 0.4f,            // squad_target heading/dist
        0.1f, 0.2f,            // squad_center heading/dist
        1.0f, -1.0f,           // aggression, spacing
        memory);

    // 5 (sensor) + 3 (pos) + 7 (nav) + 6 (squad leader) + 2 (memory) = 23
    std::size_t expected = nf::compute_arena_input_size(design, 6);
    EXPECT_EQ(input.size(), expected);
}

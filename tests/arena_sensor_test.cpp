#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

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
    auto size = nf::compute_arena_input_size(design);
    // 5 + 1 (sensors) + 6 (squad leader) + 4 (memory) = 16
    EXPECT_EQ(size, 16u);
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
        0.5f, 0.4f,            // squad_target heading/dist
        0.1f, 0.2f,            // squad_center heading/dist
        1.0f, -1.0f,           // aggression, spacing
        memory);

    // 5 (sensor) + 6 (squad leader) + 2 (memory) = 13
    std::size_t expected = nf::compute_arena_input_size(design);
    EXPECT_EQ(input.size(), expected);
}

// --- Occulus sensor tests (STALE-001 fix) ---

TEST(ArenaSensorTest, OcculusDetectsTowerInEllipse) {
    // Occulus sensor: angle=0 (forward), range=200, width=0.5
    // With ship_rotation=0 (facing up), the ellipse center is placed
    // at (ship_x, ship_y - SHIP_GAP - major_r) where SHIP_GAP=15, major_r=100.
    // So center is at (500, 500 - 115) = (500, 385).
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Occulus;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.5f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    // Place a tower near the ellipse center — well within the ellipse.
    nf::Tower tower;
    tower.x = 500.0f;
    tower.y = 385.0f;
    tower.radius = 10.0f;
    tower.alive = true;
    std::vector<nf::Tower> towers = {tower};
    ctx.towers = towers;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 1.0f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::Tower);
}

TEST(ArenaSensorTest, OcculusMissesEntityOutsideEllipse) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Occulus;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.3f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    // Place a tower far to the right — outside the narrow forward ellipse.
    nf::Tower tower;
    tower.x = 800.0f;
    tower.y = 385.0f;
    tower.radius = 10.0f;
    tower.alive = true;
    std::vector<nf::Tower> towers = {tower};
    ctx.towers = towers;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_NEAR(reading.distance, 1.0f, 0.001f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::Nothing);
}

TEST(ArenaSensorTest, OcculusRespectsShipRotation) {
    // Sensor points forward (angle=0). Rotate the ship 90 degrees right
    // (pi/2), so the ellipse should be placed to the right of the ship.
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Occulus;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.5f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = std::numbers::pi_v<float> / 2.0f; // 90 degrees right
    ctx.self_index = 0;
    ctx.self_team = 0;

    // With rotation=pi/2, the ellipse center should be at roughly
    // (500 + 115, 500) = (615, 500). Place a token there.
    nf::Token token;
    token.x = 615.0f;
    token.y = 500.0f;
    token.radius = 10.0f;
    token.alive = true;
    std::vector<nf::Token> tokens = {token};
    ctx.tokens = tokens;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 1.0f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::Token);
}

TEST(ArenaSensorTest, OcculusDetectsEnemyShip) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Occulus;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.5f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    // Place an enemy ship near the ellipse center.
    std::vector<nf::Triangle> ships = {
        nf::Triangle(500.0f, 500.0f),  // self
        nf::Triangle(500.0f, 390.0f),  // enemy ahead, inside ellipse
    };
    std::vector<int> ship_teams = {0, 1};
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 1.0f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::EnemyShip);
}

TEST(ArenaSensorTest, OcculusDetectsBullet) {
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Occulus;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.5f;
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
    b.y = 385.0f;
    b.alive = true;
    b.owner_index = 5;  // not self
    std::vector<nf::Bullet> bullets = {b};
    ctx.bullets = bullets;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 1.0f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::Bullet);
}

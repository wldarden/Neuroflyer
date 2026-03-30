#include <neuroflyer/fighter_drill_session.h>
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace nf = neuroflyer;

// ── Task 1 tests ────────────────────────────────────────────────────────────

TEST(FighterDrillConfigTest, Defaults) {
    nf::FighterDrillConfig config;
    EXPECT_FLOAT_EQ(config.world_width, 4000.0f);
    EXPECT_FLOAT_EQ(config.world_height, 4000.0f);
    EXPECT_EQ(config.population_size, 200u);
    EXPECT_EQ(config.tower_count, 50u);
    EXPECT_EQ(config.token_count, 30u);
    EXPECT_FLOAT_EQ(config.starbase_distance, 1500.0f);
    EXPECT_EQ(config.phase_duration_ticks, 1200u);
    EXPECT_FLOAT_EQ(config.attack_hit_bonus, 500.0f);
    EXPECT_GT(config.world_diagonal(), 0.0f);
}

TEST(FighterDrillConfigTest, DrillPhaseEnum) {
    EXPECT_EQ(static_cast<int>(nf::DrillPhase::Expand), 0);
    EXPECT_EQ(static_cast<int>(nf::DrillPhase::Contract), 1);
    EXPECT_EQ(static_cast<int>(nf::DrillPhase::Attack), 2);
    EXPECT_EQ(static_cast<int>(nf::DrillPhase::Done), 3);
}

// ── Task 2 tests ────────────────────────────────────────────────────────────

TEST(FighterDrillSessionTest, Construction) {
    nf::FighterDrillConfig config;
    config.population_size = 20;
    config.tower_count = 10;
    config.token_count = 5;
    nf::FighterDrillSession session(config, 42);

    EXPECT_EQ(session.ships().size(), 20u);
    EXPECT_EQ(session.towers().size(), 10u);
    EXPECT_EQ(session.tokens().size(), 5u);
    EXPECT_TRUE(session.starbase().alive());
    EXPECT_FALSE(session.is_over());
    EXPECT_EQ(session.phase(), nf::DrillPhase::Expand);
    EXPECT_EQ(session.current_tick(), 0u);
}

TEST(FighterDrillSessionTest, ShipsSpawnAtCenter) {
    nf::FighterDrillConfig config;
    config.population_size = 50;
    nf::FighterDrillSession session(config, 42);

    float center_x = config.world_width / 2.0f;
    float center_y = config.world_height / 2.0f;

    for (const auto& ship : session.ships()) {
        EXPECT_NEAR(ship.x, center_x, 1.0f);
        EXPECT_NEAR(ship.y, center_y, 1.0f);
    }
}

TEST(FighterDrillSessionTest, StarbaseAtExpectedDistance) {
    nf::FighterDrillConfig config;
    config.starbase_distance = 1500.0f;
    nf::FighterDrillSession session(config, 42);

    float center_x = config.world_width / 2.0f;
    float center_y = config.world_height / 2.0f;
    float dx = session.starbase().x - center_x;
    float dy = session.starbase().y - center_y;
    float dist = std::sqrt(dx * dx + dy * dy);
    EXPECT_NEAR(dist, 1500.0f, 1.0f);
}

TEST(FighterDrillSessionTest, ScoresInitializedToZero) {
    nf::FighterDrillConfig config;
    config.population_size = 10;
    nf::FighterDrillSession session(config, 42);

    auto scores = session.get_scores();
    EXPECT_EQ(scores.size(), 10u);
    for (float s : scores) {
        EXPECT_FLOAT_EQ(s, 0.0f);
    }
}

// ── Task 3 tests ────────────────────────────────────────────────────────────

TEST(FighterDrillSessionTest, ShipMovesOnThrust) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    auto& ship = session.ships()[0];
    ship.rotation = 0.0f;  // facing up
    float start_y = ship.y;

    session.set_ship_actions(0, true, false, false, false, false);
    session.tick();

    EXPECT_LT(session.ships()[0].y, start_y);  // moved up (negative Y)
}

TEST(FighterDrillSessionTest, WorldWrapping) {
    nf::FighterDrillConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    auto& ship = session.ships()[0];
    ship.x = 99.0f;
    ship.y = 50.0f;
    ship.rotation = std::numbers::pi_v<float> / 2.0f;  // facing right

    session.set_ship_actions(0, true, false, false, false, false);
    session.tick();

    EXPECT_LT(session.ships()[0].x, 50.0f);  // wrapped
}

TEST(FighterDrillSessionTest, BulletSpawnsOnShoot) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    EXPECT_EQ(session.bullets().size(), 0u);
    session.set_ship_actions(0, false, false, false, false, true);
    session.tick();
    EXPECT_EQ(session.bullets().size(), 1u);
    EXPECT_TRUE(session.bullets()[0].alive);
}

TEST(FighterDrillSessionTest, BulletDiesAtMaxRange) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.bullet_max_range = 50.0f;
    config.world_width = 10000.0f;
    config.world_height = 10000.0f;
    nf::FighterDrillSession session(config, 42);

    session.set_ship_actions(0, false, false, false, false, true);
    session.tick();
    EXPECT_EQ(session.bullets().size(), 1u);

    for (int i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.bullets().size(), 0u);
}

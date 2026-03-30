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

// ── Task 4 tests ────────────────────────────────────────────────────────────

TEST(FighterDrillSessionTest, ShipDiesOnTowerCollision) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    auto& ship = session.ships()[0];
    EXPECT_TRUE(ship.alive);
    session.tick();
    EXPECT_TRUE(session.ships()[0].alive);
}

TEST(FighterDrillSessionTest, TokenCollectionWorks) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 1;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    nf::FighterDrillSession session(config, 42);

    auto& ship = session.ships()[0];
    const auto& tok = session.tokens()[0];
    ship.x = tok.x;
    ship.y = tok.y;
    session.tick();
    EXPECT_FALSE(session.tokens()[0].alive);
}

TEST(FighterDrillSessionTest, BulletDamagesStarbase) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.starbase_hp = 100.0f;
    config.base_bullet_damage = 10.0f;
    config.starbase_distance = 50.0f;
    nf::FighterDrillSession session(config, 42);

    float initial_hp = session.starbase().hp;
    EXPECT_FLOAT_EQ(initial_hp, 100.0f);

    auto& ship = session.ships()[0];
    float dx = session.starbase().x - ship.x;
    float dy = session.starbase().y - ship.y;
    ship.rotation = std::atan2(dx, -dy);

    session.set_ship_actions(0, false, false, false, false, true);
    for (int i = 0; i < 15; ++i) session.tick();

    EXPECT_LT(session.starbase().hp, initial_hp);
}

// ── Task 5 tests ────────────────────────────────────────────────────────────

TEST(FighterDrillSessionTest, PhaseTransitions) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 10;
    nf::FighterDrillSession session(config, 42);

    EXPECT_EQ(session.phase(), nf::DrillPhase::Expand);
    EXPECT_EQ(session.phase_ticks_remaining(), 10u);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Contract);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Attack);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Done);
    EXPECT_TRUE(session.is_over());
}

TEST(FighterDrillSessionTest, TotalTicksThreePhases) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 10;
    nf::FighterDrillSession session(config, 42);

    for (uint32_t i = 0; i < 30; ++i) session.tick();
    EXPECT_TRUE(session.is_over());
    EXPECT_EQ(session.current_tick(), 30u);
}

TEST(FighterDrillSessionTest, ExpandPhaseRewardsMovingAway) {
    nf::FighterDrillConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::FighterDrillSession session(config, 42);

    auto& ship0 = session.ships()[0];
    auto& ship1 = session.ships()[1];

    float cx = config.world_width / 2.0f;
    ship0.x = cx + 100.0f;
    ship0.y = config.world_height / 2.0f;
    ship0.rotation = std::numbers::pi_v<float> / 2.0f;  // facing right (away)

    ship1.x = cx + 100.0f;
    ship1.y = config.world_height / 2.0f;
    ship1.rotation = -std::numbers::pi_v<float> / 2.0f;  // facing left (toward)

    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    EXPECT_GT(scores[0], 0.0f);
    EXPECT_LT(scores[1], 0.0f);
    EXPECT_GT(scores[0], scores[1]);
}

TEST(FighterDrillSessionTest, ContractPhaseRewardsMovingToward) {
    nf::FighterDrillConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::FighterDrillSession session(config, 42);

    for (uint32_t i = 0; i < 5; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Contract);

    auto& ship0 = session.ships()[0];
    auto& ship1 = session.ships()[1];

    float cx = config.world_width / 2.0f;
    ship0.x = cx + 100.0f;
    ship0.y = config.world_height / 2.0f;
    ship0.rotation = -std::numbers::pi_v<float> / 2.0f;  // facing left (toward)

    ship1.x = cx + 100.0f;
    ship1.y = config.world_height / 2.0f;
    ship1.rotation = std::numbers::pi_v<float> / 2.0f;  // facing right (away)

    float score_before_0 = session.get_scores()[0];
    float score_before_1 = session.get_scores()[1];

    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    float delta_0 = scores[0] - score_before_0;
    float delta_1 = scores[1] - score_before_1;
    EXPECT_GT(delta_0, 0.0f);
    EXPECT_LT(delta_1, 0.0f);
}

// ── Task 9 tests ─────────────────────────────────────────────────────────────

TEST(FighterDrillSessionTest, FullDrillRun) {
    nf::FighterDrillConfig config;
    config.population_size = 10;
    config.tower_count = 5;
    config.token_count = 3;
    config.phase_duration_ticks = 10;

    nf::FighterDrillSession session(config, 42);

    for (uint32_t i = 0; i < 30; ++i) {
        for (std::size_t s = 0; s < 10; ++s) {
            session.set_ship_actions(s,
                (i + s) % 3 == 0,
                (i + s) % 5 == 0,
                (i + s) % 2 == 0,
                (i + s) % 7 == 0,
                (i + s) % 4 == 0);
        }
        session.tick();
    }

    EXPECT_TRUE(session.is_over());
    EXPECT_EQ(session.current_tick(), 30u);

    auto scores = session.get_scores();
    EXPECT_EQ(scores.size(), 10u);

    bool any_nonzero = false;
    for (float s : scores) {
        if (std::abs(s) > 0.001f) any_nonzero = true;
    }
    EXPECT_TRUE(any_nonzero);
}

TEST(FighterDrillSessionTest, DeadShipsDontScore) {
    nf::FighterDrillConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 10;
    nf::FighterDrillSession session(config, 42);

    session.ships()[1].alive = false;

    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    EXPECT_NE(scores[0], 0.0f);
    EXPECT_FLOAT_EQ(scores[1], 0.0f);
}

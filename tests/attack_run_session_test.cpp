#include <neuroflyer/attack_run_session.h>
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace nf = neuroflyer;

// ── Config tests ────────────────────────────────────────────────────────────

TEST(AttackRunConfigTest, Defaults) {
    nf::AttackRunConfig config;
    EXPECT_FLOAT_EQ(config.world_width, 4000.0f);
    EXPECT_FLOAT_EQ(config.world_height, 4000.0f);
    EXPECT_EQ(config.population_size, 200u);
    EXPECT_EQ(config.tower_count, 50u);
    EXPECT_EQ(config.token_count, 30u);
    EXPECT_FLOAT_EQ(config.min_starbase_distance, 1000.0f);
    EXPECT_EQ(config.phase_duration_ticks, 1200u);
    EXPECT_FLOAT_EQ(config.attack_hit_bonus, 500.0f);
    EXPECT_GT(config.world_diagonal(), 0.0f);
}

TEST(AttackRunConfigTest, PhaseEnum) {
    EXPECT_EQ(static_cast<int>(nf::AttackRunPhase::Phase1), 0);
    EXPECT_EQ(static_cast<int>(nf::AttackRunPhase::Phase2), 1);
    EXPECT_EQ(static_cast<int>(nf::AttackRunPhase::Phase3), 2);
    EXPECT_EQ(static_cast<int>(nf::AttackRunPhase::Done), 3);
}

// ── Session tests ───────────────────────────────────────────────────────────

TEST(AttackRunSessionTest, Construction) {
    nf::AttackRunConfig config;
    config.population_size = 20;
    config.tower_count = 10;
    config.token_count = 5;
    nf::AttackRunSession session(config, 42);

    EXPECT_EQ(session.ships().size(), 20u);
    EXPECT_EQ(session.towers().size(), 10u);
    EXPECT_EQ(session.tokens().size(), 5u);
    EXPECT_TRUE(session.starbase().alive());
    EXPECT_FALSE(session.is_over());
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Phase1);
    EXPECT_EQ(session.phase_number(), 1);
    EXPECT_EQ(session.current_tick(), 0u);
}

TEST(AttackRunSessionTest, StarbaseAtMinDistance) {
    // Run with several seeds to verify the starbase is placed at >= ~900 from center.
    // We allow 900 instead of 1000 because clamping to world edges may reduce the
    // effective distance slightly.
    for (uint32_t seed = 0; seed < 20; ++seed) {
        nf::AttackRunConfig config;
        nf::AttackRunSession session(config, seed);

        const float cx = config.world_width / 2.0f;
        const float cy = config.world_height / 2.0f;
        const float dx = session.starbase().x - cx;
        const float dy = session.starbase().y - cy;
        const float dist = std::sqrt(dx * dx + dy * dy);
        EXPECT_GE(dist, 900.0f) << "seed=" << seed;
    }
}

TEST(AttackRunSessionTest, ScoresInitializedToZero) {
    nf::AttackRunConfig config;
    config.population_size = 10;
    nf::AttackRunSession session(config, 42);

    auto scores = session.get_scores();
    EXPECT_EQ(scores.size(), 10u);
    for (float s : scores) {
        EXPECT_FLOAT_EQ(s, 0.0f);
    }
}

TEST(AttackRunSessionTest, PhaseTransitionsOnTimer) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 10;
    nf::AttackRunSession session(config, 42);

    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Phase1);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Phase2);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Phase3);

    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Done);
    EXPECT_TRUE(session.is_over());
    EXPECT_EQ(session.current_tick(), 30u);
}

TEST(AttackRunSessionTest, EarlyPhaseOnStarbaseDestroy) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 400.0f;
    config.world_height = 400.0f;
    config.phase_duration_ticks = 1000;
    config.starbase_hp = 10.0f;
    config.base_bullet_damage = 10.0f;
    config.min_starbase_distance = 50.0f;
    config.starbase_radius = 100.0f;
    config.bullet_max_range = 500.0f;
    nf::AttackRunSession session(config, 42);

    // Continuously re-aim and thrust toward starbase while firing
    for (int i = 0; i < 100; ++i) {
        if (session.phase() != nf::AttackRunPhase::Phase1) break;
        auto& ship = session.ships()[0];
        float dx = session.starbase().x - ship.x;
        float dy = session.starbase().y - ship.y;
        ship.rotation = std::atan2(dx, -dy);

        session.set_ship_actions(0, true, false, false, false, true);
        session.tick();
    }

    // Starbase should have been destroyed and phase advanced to Phase2
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Phase2);
    // New starbase should be alive for Phase2
    EXPECT_TRUE(session.starbase().alive());
}

TEST(AttackRunSessionTest, StarbaseRespawnsEachPhase) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::AttackRunSession session(config, 42);

    float x1 = session.starbase().x;
    float y1 = session.starbase().y;

    for (uint32_t i = 0; i < 5; ++i) session.tick();
    float x2 = session.starbase().x;
    float y2 = session.starbase().y;

    for (uint32_t i = 0; i < 5; ++i) session.tick();
    float x3 = session.starbase().x;
    float y3 = session.starbase().y;

    // At least one pair of positions should differ (extremely unlikely all 3 are identical
    // across random respawns).
    bool all_same = (x1 == x2 && y1 == y2) && (x2 == x3 && y2 == y3);
    EXPECT_FALSE(all_same);
}

TEST(AttackRunSessionTest, TravelTowardTargetScores) {
    nf::AttackRunConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 100;
    nf::AttackRunSession session(config, 42);

    auto& ship0 = session.ships()[0];
    auto& ship1 = session.ships()[1];

    // Point ship0 toward starbase
    float dx0 = session.starbase().x - ship0.x;
    float dy0 = session.starbase().y - ship0.y;
    ship0.rotation = std::atan2(dx0, -dy0);

    // Point ship1 away from starbase (opposite direction)
    ship1.rotation = ship0.rotation + std::numbers::pi_v<float>;

    for (int i = 0; i < 10; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    EXPECT_GT(scores[0], 0.0f);
    EXPECT_LT(scores[1], 0.0f);
}

TEST(AttackRunSessionTest, BulletHitBonus) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 400.0f;
    config.world_height = 400.0f;
    config.starbase_hp = 10000.0f;  // high HP so it stays alive
    config.base_bullet_damage = 10.0f;
    config.min_starbase_distance = 50.0f;
    config.starbase_radius = 100.0f;
    config.attack_hit_bonus = 500.0f;
    config.bullet_max_range = 500.0f;
    config.phase_duration_ticks = 1000;
    nf::AttackRunSession session(config, 42);

    // Continuously re-aim and thrust toward starbase while firing
    for (int i = 0; i < 60; ++i) {
        auto& ship = session.ships()[0];
        float dx = session.starbase().x - ship.x;
        float dy = session.starbase().y - ship.y;
        ship.rotation = std::atan2(dx, -dy);

        session.set_ship_actions(0, true, false, false, false, true);
        session.tick();
    }

    auto scores = session.get_scores();
    // At least one bullet should have hit (500 bonus each), so score >= 400
    EXPECT_GE(scores[0], 400.0f);
}

TEST(AttackRunSessionTest, TokenCollection) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 1;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.min_starbase_distance = 10.0f;
    config.phase_duration_ticks = 100;
    nf::AttackRunSession session(config, 42);

    auto& ship = session.ships()[0];
    const auto& tok = session.tokens()[0];
    ship.x = tok.x;
    ship.y = tok.y;
    session.tick();

    EXPECT_FALSE(session.tokens()[0].alive);
    auto scores = session.get_scores();
    EXPECT_GE(scores[0], config.token_bonus - 1.0f);
}

TEST(AttackRunSessionTest, DeathPenalty) {
    nf::AttackRunConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 100;
    nf::AttackRunSession session(config, 42);

    // Kill ship[1]
    session.ships()[1].alive = false;

    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    // Dead ship should have score 0 (no scoring for dead ships)
    EXPECT_FLOAT_EQ(scores[1], 0.0f);
}

TEST(AttackRunSessionTest, SessionEndsAfterPhase3) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::AttackRunSession session(config, 42);

    for (uint32_t i = 0; i < 15; ++i) session.tick();

    EXPECT_TRUE(session.is_over());
    EXPECT_EQ(session.phase(), nf::AttackRunPhase::Done);
}

TEST(AttackRunSessionTest, ThreeEarlyDestroys) {
    nf::AttackRunConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 400.0f;
    config.world_height = 400.0f;
    config.phase_duration_ticks = 10000;
    config.starbase_hp = 10.0f;
    config.base_bullet_damage = 10.0f;
    config.min_starbase_distance = 50.0f;
    config.starbase_radius = 100.0f;
    config.bullet_max_range = 500.0f;
    nf::AttackRunSession session(config, 42);

    for (uint32_t t = 0; t < 500; ++t) {
        if (session.is_over()) break;

        // Re-aim at the (possibly new) starbase each tick
        auto& ship = session.ships()[0];
        if (ship.alive) {
            float dx = session.starbase().x - ship.x;
            float dy = session.starbase().y - ship.y;
            ship.rotation = std::atan2(dx, -dy);
        }

        session.set_ship_actions(0, true, false, false, false, true);
        session.tick();
    }

    EXPECT_TRUE(session.is_over());
}

TEST(AttackRunSessionTest, FullDrillRun) {
    nf::AttackRunConfig config;
    config.population_size = 10;
    config.tower_count = 5;
    config.token_count = 3;
    config.phase_duration_ticks = 10;
    nf::AttackRunSession session(config, 42);

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
    auto scores = session.get_scores();
    EXPECT_EQ(scores.size(), 10u);

    bool any_nonzero = false;
    for (float s : scores) {
        if (std::abs(s) > 0.001f) any_nonzero = true;
    }
    EXPECT_TRUE(any_nonzero);
}

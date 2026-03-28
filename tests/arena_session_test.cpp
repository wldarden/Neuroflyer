#include <neuroflyer/arena_session.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ArenaSessionTest, Construction) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 5;
    config.tower_count = 10;
    config.token_count = 5;
    nf::ArenaSession arena(config, 42);
    EXPECT_EQ(arena.ships().size(), 10u);
    EXPECT_EQ(arena.towers().size(), 10u);
    EXPECT_EQ(arena.tokens().size(), 5u);
    EXPECT_FALSE(arena.is_over());
}

TEST(ArenaSessionTest, TeamAssignment) {
    nf::ArenaConfig config;
    config.num_teams = 4;
    config.team_size = 3;
    nf::ArenaSession arena(config, 42);
    EXPECT_EQ(arena.team_of(0), 0);
    EXPECT_EQ(arena.team_of(2), 0);
    EXPECT_EQ(arena.team_of(3), 1);
    EXPECT_EQ(arena.team_of(11), 3);
}

TEST(ArenaSessionTest, SpawnInRing) {
    nf::ArenaConfig config;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.num_teams = 4;
    config.team_size = 5;
    nf::ArenaSession arena(config, 42);
    float center_x = config.world_width / 2.0f;
    float center_y = config.world_height / 2.0f;
    float radius = std::min(config.world_width, config.world_height) / 2.0f;
    float inner = radius / 3.0f;
    float outer = radius * 2.0f / 3.0f;
    for (const auto& ship : arena.ships()) {
        float dx = ship.x - center_x;
        float dy = ship.y - center_y;
        float dist = std::sqrt(dx * dx + dy * dy);
        EXPECT_GE(dist, inner - 1.0f) << "Ship too close to center";
        EXPECT_LE(dist, outer + 1.0f) << "Ship too far from center";
    }
}

TEST(ArenaSessionTest, ShipsFaceCenter) {
    nf::ArenaConfig config;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.num_teams = 2;
    config.team_size = 1;
    nf::ArenaSession arena(config, 42);
    float cx = config.world_width / 2.0f;
    float cy = config.world_height / 2.0f;
    for (const auto& ship : arena.ships()) {
        float to_center_x = cx - ship.x;
        float to_center_y = cy - ship.y;
        float angle_to_center = std::atan2(to_center_x, -to_center_y);
        float diff = std::abs(ship.rotation - angle_to_center);
        if (diff > static_cast<float>(M_PI)) diff = 2.0f * static_cast<float>(M_PI) - diff;
        EXPECT_LT(diff, 0.1f) << "Ship not facing center";
    }
}

TEST(ArenaSessionTest, TickAdvances) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 100;
    nf::ArenaSession arena(config, 42);
    arena.tick();
    EXPECT_EQ(arena.current_tick(), 1u);
}

TEST(ArenaSessionTest, TimeLimitEndsRound) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 5;
    nf::ArenaSession arena(config, 42);
    for (int i = 0; i < 5; ++i) arena.tick();
    EXPECT_TRUE(arena.is_over());
}

TEST(ArenaSessionTest, SurvivalScoring) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 100;
    nf::ArenaSession arena(config, 42);
    for (int i = 0; i < 60; ++i) arena.tick();
    auto scores = arena.get_scores();
    EXPECT_EQ(scores.size(), 2u);
    EXPECT_NEAR(scores[0], 1.0f, 0.1f);
    EXPECT_NEAR(scores[1], 1.0f, 0.1f);
}

TEST(ArenaSessionTest, WrapNS) {
    nf::ArenaConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = true;
    config.wrap_ew = false;
    config.num_teams = 1;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    nf::ArenaSession arena(config, 42);
    arena.ships()[0].y = -1.0f;
    arena.apply_boundary_rules();
    EXPECT_GT(arena.ships()[0].y, 90.0f);
}

TEST(ArenaSessionTest, ClampEW) {
    nf::ArenaConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = false;
    config.wrap_ew = false;
    config.num_teams = 1;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    nf::ArenaSession arena(config, 42);
    arena.ships()[0].x = 110.0f;
    arena.apply_boundary_rules();
    EXPECT_LE(arena.ships()[0].x, 100.0f);
}

TEST(ArenaSessionTest, BulletShipCollisionSkipsSelf) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);
    nf::Bullet b;
    b.x = arena.ships()[0].x;
    b.y = arena.ships()[0].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    arena.add_bullet(b);
    arena.resolve_bullet_ship_collisions();
    EXPECT_TRUE(arena.ships()[0].alive);
}

TEST(ArenaSessionTest, BulletKillsEnemy) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);
    nf::Bullet b;
    b.x = arena.ships()[1].x;
    b.y = arena.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    arena.add_bullet(b);
    arena.resolve_bullet_ship_collisions();
    EXPECT_FALSE(arena.ships()[1].alive);
}

TEST(ArenaSessionTest, LastTeamStandingEndsRound) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.team_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 10000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);
    arena.ships()[1].alive = false;
    arena.tick();
    EXPECT_TRUE(arena.is_over());
}

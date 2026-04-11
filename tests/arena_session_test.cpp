#include <neuroflyer/arena_session.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ArenaSessionTest, Construction) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 5;
    config.world.tower_count = 10;
    config.world.token_count = 5;
    nf::ArenaSession arena(config, 42);
    EXPECT_EQ(arena.ships().size(), 10u);
    EXPECT_EQ(arena.towers().size(), 10u);
    EXPECT_EQ(arena.tokens().size(), 5u);
    EXPECT_FALSE(arena.is_over());
}

TEST(ArenaSessionTest, TeamAssignment) {
    nf::ArenaConfig config;
    config.world.num_teams = 4;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 3;
    nf::ArenaSession arena(config, 42);
    EXPECT_EQ(arena.team_of(0), 0);
    EXPECT_EQ(arena.team_of(2), 0);
    EXPECT_EQ(arena.team_of(3), 1);
    EXPECT_EQ(arena.team_of(11), 3);
}

TEST(ArenaSessionTest, SpawnInRing) {
    nf::ArenaConfig config;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.num_teams = 4;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 5;
    nf::ArenaSession arena(config, 42);
    float center_x = config.world.world_width / 2.0f;
    float center_y = config.world.world_height / 2.0f;
    float radius = std::min(config.world.world_width, config.world.world_height) / 2.0f;
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
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    nf::ArenaSession arena(config, 42);
    float cx = config.world.world_width / 2.0f;
    float cy = config.world.world_height / 2.0f;
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
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 2;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 100;
    nf::ArenaSession arena(config, 42);
    arena.tick();
    EXPECT_EQ(arena.current_tick(), 1u);
}

TEST(ArenaSessionTest, TimeLimitEndsRound) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 5;
    nf::ArenaSession arena(config, 42);
    for (int i = 0; i < 5; ++i) arena.tick();
    EXPECT_TRUE(arena.is_over());
}

TEST(ArenaSessionTest, SurvivalScoring) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
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
    config.world.world_width = 100.0f;
    config.world.world_height = 100.0f;
    config.world.wrap_ns = true;
    config.world.wrap_ew = false;
    config.world.num_teams = 1;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    nf::ArenaSession arena(config, 42);
    arena.ships()[0].y = -1.0f;
    arena.apply_boundary_rules();
    EXPECT_GT(arena.ships()[0].y, 90.0f);
}

TEST(ArenaSessionTest, ClampEW) {
    nf::ArenaConfig config;
    config.world.world_width = 100.0f;
    config.world.world_height = 100.0f;
    config.world.wrap_ns = false;
    config.world.wrap_ew = false;
    config.world.num_teams = 1;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    nf::ArenaSession arena(config, 42);
    arena.ships()[0].x = 110.0f;
    arena.apply_boundary_rules();
    EXPECT_LE(arena.ships()[0].x, 100.0f);
}

TEST(ArenaSessionTest, BulletShipCollisionSkipsSelf) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
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
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.ship_hp = 1.0f;
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
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 10000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);
    arena.ships()[1].alive = false;
    arena.tick();
    EXPECT_TRUE(arena.is_over());
}

TEST(ArenaSessionTest, EnemyKillAwards1000Points) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.ship_hp = 1.0f;
    nf::ArenaSession arena(config, 42);

    // Place bullet from ship 0 (team 0) on top of ship 1 (team 1)
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

    EXPECT_EQ(arena.enemy_kills()[0], 1);
    EXPECT_EQ(arena.ally_kills()[0], 0);

    auto scores = arena.get_scores();
    // Team 0 should have +1000 from the kill (plus survival ticks which are 0)
    EXPECT_GE(scores[0], 1000.0f);
    // Team 1 should have 0 (no kills, no survival yet)
    EXPECT_NEAR(scores[1], 0.0f, 0.01f);
}

TEST(ArenaSessionTest, AllyKillRemoves1000Points) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 2;  // 2 ships per team
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.friendly_fire = true;  // must be on to allow ally kills
    config.world.ship_hp = 1.0f;
    nf::ArenaSession arena(config, 42);

    // Ship 0 and 1 are on team 0. Place bullet from ship 0 on ship 1.
    ASSERT_EQ(arena.team_of(0), 0);
    ASSERT_EQ(arena.team_of(1), 0);

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

    EXPECT_EQ(arena.ally_kills()[0], 1);
    EXPECT_EQ(arena.enemy_kills()[0], 0);

    auto scores = arena.get_scores();
    // Team 0 should have -1000 from the ally kill
    EXPECT_LE(scores[0], -999.0f);
}

TEST(ArenaSessionTest, BasesSpawnPerTeam) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 4;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.base_hp = 500.0f;
    config.world.base_radius = 50.0f;
    nf::ArenaSession arena(config, 42);

    ASSERT_EQ(arena.bases().size(), 2u);
    EXPECT_FLOAT_EQ(arena.bases()[0].max_hp, 500.0f);
    EXPECT_FLOAT_EQ(arena.bases()[0].radius, 50.0f);
    EXPECT_EQ(arena.bases()[0].team_id, 0);
    EXPECT_EQ(arena.bases()[1].team_id, 1);
    float dx = arena.bases()[0].x - arena.bases()[1].x;
    float dy = arena.bases()[0].y - arena.bases()[1].y;
    EXPECT_GT(dx * dx + dy * dy, 100.0f);
}

TEST(ArenaSessionTest, BulletDamagesEnemyBase) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.base_hp = 100.0f;
    config.world.base_radius = 50.0f;
    config.world.base_bullet_damage = 10.0f;
    nf::ArenaSession arena(config, 42);

    // Move all ships far away so they don't intercept the bullet
    for (auto& ship : arena.ships()) {
        ship.x = 0.0f;
        ship.y = 0.0f;
        ship.dx = 0.0f;
        ship.dy = 0.0f;
    }

    nf::Bullet b;
    b.x = arena.bases()[1].x;
    b.y = arena.bases()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    arena.add_bullet(b);
    arena.tick();

    EXPECT_FLOAT_EQ(arena.bases()[1].hp, 90.0f);
    EXPECT_TRUE(arena.bases()[1].alive());
}

TEST(ArenaSessionTest, BulletDoesNotDamageOwnBase) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.base_hp = 100.0f;
    config.world.base_radius = 50.0f;
    config.world.base_bullet_damage = 10.0f;
    nf::ArenaSession arena(config, 42);

    nf::Bullet b;
    b.x = arena.bases()[0].x;
    b.y = arena.bases()[0].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    arena.add_bullet(b);
    arena.tick();

    EXPECT_FLOAT_EQ(arena.bases()[0].hp, 100.0f);
}

TEST(ArenaSessionTest, BaseDestroyedEndsRound) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.base_hp = 10.0f;
    config.world.base_radius = 50.0f;
    config.world.base_bullet_damage = 10.0f;
    config.time_limit_ticks = 10000;
    nf::ArenaSession arena(config, 42);

    arena.bases()[1].take_damage(10.0f);
    ASSERT_FALSE(arena.bases()[1].alive());
    arena.tick();
    EXPECT_TRUE(arena.is_over());
}

TEST(ArenaConfigTest, PopulationFromSquads) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 2;
    config.world.fighters_per_squad = 4;
    EXPECT_EQ(config.population_size(), 16u);
}

TEST(ArenaSessionTest, SquadAssignment) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 2;
    config.world.fighters_per_squad = 3;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);

    EXPECT_EQ(arena.ships().size(), 12u);
    EXPECT_EQ(arena.squad_of(0), 0);   // team 0, squad 0
    EXPECT_EQ(arena.squad_of(2), 0);   // team 0, squad 0
    EXPECT_EQ(arena.squad_of(3), 1);   // team 0, squad 1
    EXPECT_EQ(arena.squad_of(5), 1);   // team 0, squad 1
    EXPECT_EQ(arena.squad_of(6), 0);   // team 1, squad 0
    EXPECT_EQ(arena.squad_of(9), 1);   // team 1, squad 1
}

TEST(ArenaSessionTest, SquadStats) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 4;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.base_hp = 100.0f;
    nf::ArenaSession arena(config, 42);

    auto stats = arena.compute_squad_stats(0, 0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 1.0f);
    EXPECT_GT(stats.centroid_x, 0.0f);
    EXPECT_GT(stats.centroid_y, 0.0f);

    // Kill 2 of 4 fighters
    arena.ships()[0].alive = false;
    arena.ships()[1].alive = false;
    stats = arena.compute_squad_stats(0, 0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 0.5f);
}

TEST(ArenaSession, SquadStatsIncludesSpacing) {
    nf::ArenaConfig cfg;
    cfg.world.num_teams = 1;
    cfg.world.num_squads = 1;
    cfg.world.fighters_per_squad = 4;
    cfg.world.tower_count = 0;
    cfg.world.token_count = 0;
    cfg.world.base_hp = 1000.0f;

    nf::ArenaSession arena(cfg, 42);

    auto stats = arena.compute_squad_stats(0, 0);

    // Spacing should be a valid normalized value
    EXPECT_GE(stats.squad_spacing, 0.0f);
    EXPECT_LE(stats.squad_spacing, 1.0f);
}

TEST(ArenaSessionTest, FriendlyFireOffBulletsPassThroughTeammates) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 2;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.friendly_fire = false;  // bullets should pass through teammates
    nf::ArenaSession arena(config, 42);

    // Ship 0 and 1 are on team 0.
    ASSERT_EQ(arena.team_of(0), 0);
    ASSERT_EQ(arena.team_of(1), 0);

    // Place bullet from ship 0 directly on ship 1.
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

    // Ship 1 should still be alive — bullet passed through.
    EXPECT_TRUE(arena.ships()[1].alive);
    EXPECT_EQ(arena.ally_kills()[0], 0);
    // Bullet should still be alive (it wasn't consumed).
    EXPECT_TRUE(arena.bullets()[0].alive);
}

TEST(ArenaSessionTest, FriendlyFireOnBulletsKillTeammates) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 2;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.friendly_fire = true;  // bullets SHOULD kill teammates
    config.world.ship_hp = 1.0f;
    nf::ArenaSession arena(config, 42);

    ASSERT_EQ(arena.team_of(0), 0);
    ASSERT_EQ(arena.team_of(1), 0);

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

    // Ship 1 should be dead — friendly fire is on.
    EXPECT_FALSE(arena.ships()[1].alive);
    EXPECT_EQ(arena.ally_kills()[0], 1);
}

TEST(ArenaSessionTest, FriendlyFireOffEnemyBulletsStillKill) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 2;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.friendly_fire = false;
    config.world.ship_hp = 1.0f;
    nf::ArenaSession arena(config, 42);

    // Ship 0 is team 0, ship 2 is team 1.
    ASSERT_EQ(arena.team_of(0), 0);
    ASSERT_EQ(arena.team_of(2), 1);

    // Place bullet from ship 0 on enemy ship 2.
    nf::Bullet b;
    b.x = arena.ships()[2].x;
    b.y = arena.ships()[2].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    arena.add_bullet(b);
    arena.resolve_bullet_ship_collisions();

    // Enemy ship should be dead — cross-team kills still work.
    EXPECT_FALSE(arena.ships()[2].alive);
    EXPECT_EQ(arena.enemy_kills()[0], 1);
}

TEST(ArenaSessionTest, ShipSurvivesBulletWithHP) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.ship_hp = 3.0f;
    config.world.bullet_ship_damage = 1.0f;
    nf::ArenaSession arena(config, 42);

    // One bullet should damage but not kill
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

    EXPECT_TRUE(arena.ships()[1].alive);
    EXPECT_FLOAT_EQ(arena.ships()[1].hp, 2.0f);
    EXPECT_EQ(arena.enemy_kills()[0], 0);  // no kill yet
}

TEST(ArenaSessionTest, ShipDiesAfterEnoughBullets) {
    nf::ArenaConfig config;
    config.world.num_teams = 2;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world.world_width = 1000.0f;
    config.world.world_height = 1000.0f;
    config.world.ship_hp = 3.0f;
    config.world.bullet_ship_damage = 1.0f;
    nf::ArenaSession arena(config, 42);

    // Fire 3 bullets, each doing 1 damage
    for (int hit = 0; hit < 3; ++hit) {
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
    }

    EXPECT_FALSE(arena.ships()[1].alive);
    EXPECT_FLOAT_EQ(arena.ships()[1].hp, 0.0f);
    EXPECT_EQ(arena.enemy_kills()[0], 1);
}

TEST(ArenaSessionTest, DamageLevel) {
    nf::Triangle ship(100.0f, 100.0f);
    ship.hp = 3.0f;
    ship.max_hp = 3.0f;

    EXPECT_EQ(ship.damage_level(), 0);  // pristine

    ship.take_damage(1.0f);
    EXPECT_EQ(ship.damage_level(), 1);  // cracked
    EXPECT_TRUE(ship.alive);

    ship.take_damage(1.0f);
    EXPECT_EQ(ship.damage_level(), 2);  // on fire
    EXPECT_TRUE(ship.alive);

    ship.take_damage(1.0f);
    EXPECT_FALSE(ship.alive);           // dead
}

TEST(ArenaSessionTest, ShipHPInitFromConfig) {
    nf::ArenaConfig config;
    config.world.num_teams = 1;
    config.world.num_squads = 1;
    config.world.fighters_per_squad = 1;
    config.world.tower_count = 0;
    config.world.token_count = 0;
    config.world.ship_hp = 5.0f;
    nf::ArenaSession arena(config, 42);

    EXPECT_FLOAT_EQ(arena.ships()[0].hp, 5.0f);
    EXPECT_FLOAT_EQ(arena.ships()[0].max_hp, 5.0f);
}

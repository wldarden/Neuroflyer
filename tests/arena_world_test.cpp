#include <neuroflyer/arena_world.h>
#include <gtest/gtest.h>

#include <cmath>

namespace nf = neuroflyer;

// --- Construction and entity counts ---

TEST(ArenaWorldTest, ConstructionEntityCounts) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 5;
    config.tower_count = 10;
    config.token_count = 5;
    nf::ArenaWorld world(config, 42);
    EXPECT_EQ(world.ships().size(), 10u);
    EXPECT_EQ(world.towers().size(), 10u);
    EXPECT_EQ(world.tokens().size(), 5u);
    EXPECT_EQ(world.bases().size(), 2u);
}

// --- Team assignments ---

TEST(ArenaWorldTest, TeamAssignments) {
    nf::ArenaWorldConfig config;
    config.num_teams = 4;
    config.num_squads = 1;
    config.fighters_per_squad = 3;
    nf::ArenaWorld world(config, 42);
    EXPECT_EQ(world.team_of(0), 0);
    EXPECT_EQ(world.team_of(2), 0);
    EXPECT_EQ(world.team_of(3), 1);
    EXPECT_EQ(world.team_of(11), 3);
    EXPECT_EQ(world.team_of(999), -1);  // out of bounds
}

TEST(ArenaWorldTest, SquadAssignments) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 2;
    config.fighters_per_squad = 3;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    EXPECT_EQ(world.ships().size(), 12u);
    EXPECT_EQ(world.squad_of(0), 0);   // team 0, squad 0
    EXPECT_EQ(world.squad_of(2), 0);   // team 0, squad 0
    EXPECT_EQ(world.squad_of(3), 1);   // team 0, squad 1
    EXPECT_EQ(world.squad_of(5), 1);   // team 0, squad 1
    EXPECT_EQ(world.squad_of(6), 0);   // team 1, squad 0
    EXPECT_EQ(world.squad_of(9), 1);   // team 1, squad 1
    EXPECT_EQ(world.squad_of(999), -1);
}

// --- Tick counter ---

TEST(ArenaWorldTest, TickCounter) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    EXPECT_EQ(world.current_tick(), 0u);
    (void)world.tick();
    EXPECT_EQ(world.current_tick(), 1u);
    (void)world.tick();
    EXPECT_EQ(world.current_tick(), 2u);
}

// --- Empty events ---

TEST(ArenaWorldTest, EmptyEventsOnQuietTick) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 10000.0f;
    config.world_height = 10000.0f;
    nf::ArenaWorld world(config, 42);

    auto events = world.tick();
    EXPECT_TRUE(events.bullets_fired.empty());
    EXPECT_TRUE(events.ship_kills.empty());
    EXPECT_TRUE(events.ship_tower_deaths.empty());
    EXPECT_TRUE(events.tokens_collected.empty());
    EXPECT_TRUE(events.base_hits.empty());
    EXPECT_TRUE(events.towers_destroyed.empty());
}

// --- Boundary wrapping ---

TEST(ArenaWorldTest, BoundaryWrapNS) {
    nf::ArenaWorldConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = true;
    config.wrap_ew = false;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    world.ships()[0].y = -1.0f;
    world.apply_boundary_rules();
    EXPECT_GT(world.ships()[0].y, 90.0f);
}

TEST(ArenaWorldTest, BoundaryWrapEW) {
    nf::ArenaWorldConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = false;
    config.wrap_ew = true;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    world.ships()[0].x = -5.0f;
    world.apply_boundary_rules();
    EXPECT_GT(world.ships()[0].x, 90.0f);
}

// --- Boundary clamping ---

TEST(ArenaWorldTest, BoundaryClampEW) {
    nf::ArenaWorldConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = false;
    config.wrap_ew = false;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    world.ships()[0].x = 110.0f;
    world.apply_boundary_rules();
    EXPECT_LE(world.ships()[0].x, 100.0f);
}

TEST(ArenaWorldTest, BoundaryClampNS) {
    nf::ArenaWorldConfig config;
    config.world_width = 100.0f;
    config.world_height = 100.0f;
    config.wrap_ns = false;
    config.wrap_ew = false;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    world.ships()[0].y = 110.0f;
    world.apply_boundary_rules();
    EXPECT_LE(world.ships()[0].y, 100.0f);
}

// --- Bullet fired events ---

TEST(ArenaWorldTest, BulletFiredEvent) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 10000.0f;
    config.world_height = 10000.0f;
    nf::ArenaWorld world(config, 42);

    // Make ship 0 want to shoot
    world.set_ship_actions(0, false, false, false, false, true);
    auto events = world.tick();
    ASSERT_EQ(events.bullets_fired.size(), 1u);
    EXPECT_EQ(events.bullets_fired[0].ship_idx, 0u);
    EXPECT_EQ(world.bullets().size(), 1u);
}

// --- Bullet kills enemy ---

TEST(ArenaWorldTest, BulletKillsEnemyReportsEvent) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.ship_hp = 1.0f;
    nf::ArenaWorld world(config, 42);

    // Place bullet from ship 0 on top of ship 1
    nf::Bullet b;
    b.x = world.ships()[1].x;
    b.y = world.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);

    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);

    EXPECT_FALSE(world.ships()[1].alive);
    ASSERT_EQ(events.ship_kills.size(), 1u);
    EXPECT_EQ(events.ship_kills[0].victim_idx, 1u);
    EXPECT_EQ(events.ship_kills[0].killer_idx, 0u);
    EXPECT_FALSE(events.ship_kills[0].friendly);

    // Backward-compat tracking
    EXPECT_EQ(world.enemy_kills()[0], 1);
    EXPECT_EQ(world.ally_kills()[0], 0);
}

// --- Friendly fire ---

TEST(ArenaWorldTest, FriendlyFireOffBulletsPassThrough) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.friendly_fire = false;
    nf::ArenaWorld world(config, 42);

    ASSERT_EQ(world.team_of(0), 0);
    ASSERT_EQ(world.team_of(1), 0);

    nf::Bullet b;
    b.x = world.ships()[1].x;
    b.y = world.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);

    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);

    EXPECT_TRUE(world.ships()[1].alive);
    EXPECT_TRUE(events.ship_kills.empty());
    EXPECT_TRUE(world.bullets()[0].alive);
}

TEST(ArenaWorldTest, FriendlyFireOnKillsTeammate) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.friendly_fire = true;
    config.ship_hp = 1.0f;
    nf::ArenaWorld world(config, 42);

    ASSERT_EQ(world.team_of(0), 0);
    ASSERT_EQ(world.team_of(1), 0);

    nf::Bullet b;
    b.x = world.ships()[1].x;
    b.y = world.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);

    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);

    EXPECT_FALSE(world.ships()[1].alive);
    ASSERT_EQ(events.ship_kills.size(), 1u);
    EXPECT_TRUE(events.ship_kills[0].friendly);
    EXPECT_EQ(world.ally_kills()[0], 1);
}

// --- Self-hit ---

TEST(ArenaWorldTest, BulletSkipsSelf) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaWorld world(config, 42);

    nf::Bullet b;
    b.x = world.ships()[0].x;
    b.y = world.ships()[0].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);

    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);
    EXPECT_TRUE(world.ships()[0].alive);
}

// --- Alive count ---

TEST(ArenaWorldTest, AliveCount) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 3;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    EXPECT_EQ(world.alive_count(), 6u);
    world.ships()[0].alive = false;
    world.ships()[2].alive = false;
    EXPECT_EQ(world.alive_count(), 4u);
}

// --- Teams alive ---

TEST(ArenaWorldTest, TeamsAlive) {
    nf::ArenaWorldConfig config;
    config.num_teams = 3;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    EXPECT_EQ(world.teams_alive(), 3u);

    // Kill all ships on team 0 (indices 0, 1)
    world.ships()[0].alive = false;
    world.ships()[1].alive = false;
    EXPECT_EQ(world.teams_alive(), 2u);
}

// --- Reset ---

TEST(ArenaWorldTest, ResetClearsBulletsAndTick) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 5;
    config.token_count = 3;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaWorld world(config, 42);

    // Tick a few times
    (void)world.tick();
    (void)world.tick();
    EXPECT_EQ(world.current_tick(), 2u);

    // Add a bullet manually
    nf::Bullet b;
    b.x = 50.0f;
    b.y = 50.0f;
    b.alive = true;
    world.add_bullet(b);
    EXPECT_FALSE(world.bullets().empty());

    // Reset
    world.reset(99);
    EXPECT_EQ(world.current_tick(), 0u);
    EXPECT_TRUE(world.bullets().empty());

    // Towers and tokens are respawned
    EXPECT_EQ(world.towers().size(), 5u);
    EXPECT_EQ(world.tokens().size(), 3u);

    // Ships and bases are NOT touched
    EXPECT_EQ(world.ships().size(), 4u);
    EXPECT_EQ(world.bases().size(), 2u);
}

TEST(ArenaWorldTest, ResetClearsKillTracking) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.ship_hp = 1.0f;
    nf::ArenaWorld world(config, 42);

    // Create a kill
    nf::Bullet b;
    b.x = world.ships()[1].x;
    b.y = world.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);
    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);
    ASSERT_EQ(world.enemy_kills()[0], 1);

    world.reset(99);
    EXPECT_EQ(world.enemy_kills()[0], 0);
    EXPECT_EQ(world.ally_kills()[0], 0);
}

// --- Population size formula ---

TEST(ArenaWorldConfigTest, PopulationSize) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 2;
    config.fighters_per_squad = 4;
    EXPECT_EQ(config.population_size(), 16u);
}

TEST(ArenaWorldConfigTest, WorldDiagonal) {
    nf::ArenaWorldConfig config;
    config.world_width = 300.0f;
    config.world_height = 400.0f;
    EXPECT_NEAR(config.world_diagonal(), 500.0f, 0.01f);
}

// --- Base bullet collisions via tick ---

TEST(ArenaWorldTest, BulletDamagesEnemyBase) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 100.0f;
    config.base_radius = 50.0f;
    config.base_bullet_damage = 10.0f;
    nf::ArenaWorld world(config, 42);

    // Move ships out of the way
    for (auto& ship : world.ships()) {
        ship.x = 0.0f;
        ship.y = 0.0f;
        ship.dx = 0.0f;
        ship.dy = 0.0f;
    }

    nf::Bullet b;
    b.x = world.bases()[1].x;
    b.y = world.bases()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);
    auto events = world.tick();

    EXPECT_FLOAT_EQ(world.bases()[1].hp, 90.0f);
    ASSERT_EQ(events.base_hits.size(), 1u);
    EXPECT_EQ(events.base_hits[0].base_idx, 1u);
    EXPECT_EQ(events.base_hits[0].shooter_idx, 0u);
    EXPECT_FLOAT_EQ(events.base_hits[0].damage, 10.0f);
}

TEST(ArenaWorldTest, BulletDoesNotDamageOwnBase) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 100.0f;
    config.base_radius = 50.0f;
    config.base_bullet_damage = 10.0f;
    nf::ArenaWorld world(config, 42);

    nf::Bullet b;
    b.x = world.bases()[0].x;
    b.y = world.bases()[0].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);
    auto events = world.tick();

    EXPECT_FLOAT_EQ(world.bases()[0].hp, 100.0f);
    EXPECT_TRUE(events.base_hits.empty());
}

// --- Ship HP ---

TEST(ArenaWorldTest, ShipSurvivesBulletWithHP) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.ship_hp = 3.0f;
    config.bullet_ship_damage = 1.0f;
    nf::ArenaWorld world(config, 42);

    nf::Bullet b;
    b.x = world.ships()[1].x;
    b.y = world.ships()[1].y;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 0;
    b.distance_traveled = 0.0f;
    b.max_range = 500.0f;
    world.add_bullet(b);

    nf::TickEvents events;
    world.resolve_bullet_ship_collisions(events);

    EXPECT_TRUE(world.ships()[1].alive);
    EXPECT_FLOAT_EQ(world.ships()[1].hp, 2.0f);
    EXPECT_TRUE(events.ship_kills.empty());  // no kill, just damage
}

// --- Ship HP init from config ---

TEST(ArenaWorldTest, ShipHPInitFromConfig) {
    nf::ArenaWorldConfig config;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.ship_hp = 5.0f;
    nf::ArenaWorld world(config, 42);

    EXPECT_FLOAT_EQ(world.ships()[0].hp, 5.0f);
    EXPECT_FLOAT_EQ(world.ships()[0].max_hp, 5.0f);
}

// --- Squad stats ---

TEST(ArenaWorldTest, SquadStatsAlive) {
    nf::ArenaWorldConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 4;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 100.0f;
    nf::ArenaWorld world(config, 42);

    auto stats = world.compute_squad_stats(0, 0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 1.0f);
    EXPECT_GT(stats.centroid_x, 0.0f);
    EXPECT_GT(stats.centroid_y, 0.0f);

    // Kill 2 of 4
    world.ships()[0].alive = false;
    world.ships()[1].alive = false;
    stats = world.compute_squad_stats(0, 0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 0.5f);
}

TEST(ArenaWorldTest, SquadStatsSpacing) {
    nf::ArenaWorldConfig config;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 4;
    config.tower_count = 0;
    config.token_count = 0;
    config.base_hp = 1000.0f;
    nf::ArenaWorld world(config, 42);

    auto stats = world.compute_squad_stats(0, 0);
    EXPECT_GE(stats.squad_spacing, 0.0f);
    EXPECT_LE(stats.squad_spacing, 1.0f);
}

// --- Bases per team ---

TEST(ArenaWorldTest, BasesSpawnPerTeam) {
    nf::ArenaWorldConfig config;
    config.num_teams = 3;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 500.0f;
    config.base_radius = 50.0f;
    nf::ArenaWorld world(config, 42);

    ASSERT_EQ(world.bases().size(), 3u);
    EXPECT_EQ(world.bases()[0].team_id, 0);
    EXPECT_EQ(world.bases()[1].team_id, 1);
    EXPECT_EQ(world.bases()[2].team_id, 2);
    EXPECT_FLOAT_EQ(world.bases()[0].max_hp, 500.0f);
    EXPECT_FLOAT_EQ(world.bases()[0].radius, 50.0f);
}

// --- Ships face center ---

TEST(ArenaWorldTest, ShipsFaceCenter) {
    nf::ArenaWorldConfig config;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    nf::ArenaWorld world(config, 42);

    const float cx = config.world_width / 2.0f;
    const float cy = config.world_height / 2.0f;
    for (const auto& ship : world.ships()) {
        const float to_center_x = cx - ship.x;
        const float to_center_y = cy - ship.y;
        const float angle_to_center = std::atan2(to_center_x, -to_center_y);
        float diff = std::abs(ship.rotation - angle_to_center);
        if (diff > static_cast<float>(M_PI)) diff = 2.0f * static_cast<float>(M_PI) - diff;
        EXPECT_LT(diff, 0.1f) << "Ship not facing center";
    }
}

// --- Add bullet ---

TEST(ArenaWorldTest, AddBullet) {
    nf::ArenaWorldConfig config;
    config.num_teams = 1;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::ArenaWorld world(config, 42);

    EXPECT_TRUE(world.bullets().empty());
    nf::Bullet b;
    b.x = 50.0f;
    b.y = 50.0f;
    b.alive = true;
    world.add_bullet(b);
    EXPECT_EQ(world.bullets().size(), 1u);
}

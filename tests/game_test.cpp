#include <neuroflyer/game.h>
#include <neuroflyer/collision.h>

#include <cmath>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(GameTest, SessionSpawnsTowers) {
    nf::GameSession session(42, 800.0f, 600.0f);
    EXPECT_GT(session.towers().size(), 0);  // towers generated on construction
}

TEST(GameTest, TriangleStartsAtBottom) {
    nf::Triangle tri(400.0f, 500.0f);  // x, y (near bottom of 600px screen)
    EXPECT_FLOAT_EQ(tri.x, 400.0f);
    EXPECT_FLOAT_EQ(tri.y, 500.0f);
    EXPECT_TRUE(tri.alive);
}

TEST(GameTest, TriangleMoves) {
    nf::Triangle tri(400.0f, 500.0f);
    tri.apply_actions(true, false, true, false, false);  // up, not down, left, not right, not shoot
    tri.update(800.0f, 600.0f);  // screen bounds
    EXPECT_LT(tri.x, 400.0f);  // moved left
    EXPECT_LT(tri.y, 500.0f);  // moved up
}

TEST(GameTest, TriangleClampsToScreen) {
    nf::Triangle tri(0.0f, 0.0f);
    tri.apply_actions(true, false, true, false, false);  // up + left at corner
    tri.update(800.0f, 600.0f);
    EXPECT_GE(tri.x, 0.0f);
    EXPECT_GE(tri.y, 0.0f);
}

TEST(GameTest, BulletMovesUp) {
    nf::Bullet b{.x = 100.0f, .y = 500.0f, .alive = true};
    b.update();
    EXPECT_LT(b.y, 500.0f);
}

TEST(GameTest, TowerCollisionKillsTriangle) {
    nf::Triangle tri(100.0f, 100.0f);
    nf::Tower tower{.x = 100.0f, .y = 100.0f, .radius = 20.0f, .alive = true};
    EXPECT_TRUE(nf::check_collision(tri, tower));
}

TEST(GameTest, BulletDestroysTower) {
    nf::Bullet bullet{.x = 100.0f, .y = 100.0f, .alive = true};
    nf::Tower tower{.x = 105.0f, .y = 105.0f, .radius = 20.0f, .alive = true};
    EXPECT_TRUE(nf::check_bullet_hit(bullet, tower));
}

TEST(GameTest, GameSessionScoring) {
    nf::GameSession session(42, 800.0f, 600.0f);  // seed, screen dims
    session.tick();
    session.tick();
    EXPECT_GT(session.score(), 0.0f);
    EXPECT_TRUE(session.alive());
}

TEST(TriangleTest, ArenaRotateLeft) {
    nf::Triangle tri(100.0f, 100.0f);
    tri.rotation_speed = 0.05f;
    tri.apply_arena_actions(false, false, true, false, false);
    EXPECT_FLOAT_EQ(tri.rotation, -0.05f);
}

TEST(TriangleTest, ArenaRotateRight) {
    nf::Triangle tri(100.0f, 100.0f);
    tri.rotation_speed = 0.05f;
    tri.apply_arena_actions(false, false, false, true, false);
    EXPECT_FLOAT_EQ(tri.rotation, 0.05f);
}

TEST(TriangleTest, ArenaThrustForward) {
    nf::Triangle tri(100.0f, 100.0f);
    tri.rotation = 0.0f;  // facing up
    tri.apply_arena_actions(true, false, false, false, false);
    // Facing up: dx=0, dy=-speed (screen coords: up is negative)
    EXPECT_FLOAT_EQ(tri.dx, 0.0f);
    EXPECT_LT(tri.dy, 0.0f);
}

TEST(TriangleTest, ArenaThrustForwardRotated) {
    nf::Triangle tri(100.0f, 100.0f);
    tri.rotation = static_cast<float>(M_PI / 2.0);  // facing right
    tri.apply_arena_actions(true, false, false, false, false);
    EXPECT_GT(tri.dx, 0.0f);
    EXPECT_NEAR(tri.dy, 0.0f, 0.001f);
}

TEST(TriangleTest, ArenaThrustReverse) {
    nf::Triangle tri(100.0f, 100.0f);
    tri.rotation = 0.0f;  // facing up
    tri.apply_arena_actions(false, true, false, false, false);
    // Reverse: opposite of forward
    EXPECT_FLOAT_EQ(tri.dx, 0.0f);
    EXPECT_GT(tri.dy, 0.0f);
}

TEST(BulletTest, DirectionalBullet) {
    nf::Bullet b;
    b.x = 100.0f;
    b.y = 100.0f;
    b.alive = true;
    b.dir_x = 0.0f;
    b.dir_y = -1.0f;
    b.owner_index = 3;
    b.distance_traveled = 0.0f;
    b.update_directional();
    EXPECT_FLOAT_EQ(b.y, 100.0f - nf::Bullet::SPEED);
    EXPECT_GT(b.distance_traveled, 0.0f);
}

TEST(BulletTest, DirectionalBulletMaxRange) {
    nf::Bullet b;
    b.x = 100.0f;
    b.y = 100.0f;
    b.alive = true;
    b.dir_x = 1.0f;
    b.dir_y = 0.0f;
    b.owner_index = 0;
    b.distance_traveled = 995.0f;
    b.max_range = 1000.0f;
    b.update_directional();
    EXPECT_FALSE(b.alive);
}

TEST(CollisionTest, BulletTriangleHit) {
    nf::Triangle tri(100.0f, 100.0f);
    // Bullet near the top vertex (100, 88) — within HIT_R = SIZE*0.8 = 9.6
    EXPECT_TRUE(nf::bullet_triangle_collision(100.0f, 89.0f, tri));
}

TEST(CollisionTest, BulletTriangleMiss) {
    nf::Triangle tri(100.0f, 100.0f);
    // Bullet far away
    EXPECT_FALSE(nf::bullet_triangle_collision(300.0f, 300.0f, tri));
}

TEST(CollisionTest, TriangleCircleHit) {
    nf::Triangle tri(100.0f, 100.0f);
    // Circle overlapping triangle top vertex
    EXPECT_TRUE(nf::triangle_circle_collision(tri, 100.0f, 88.0f, 5.0f));
}

TEST(CollisionTest, TriangleCircleMiss) {
    nf::Triangle tri(100.0f, 100.0f);
    // Circle far away
    EXPECT_FALSE(nf::triangle_circle_collision(tri, 300.0f, 300.0f, 5.0f));
}

TEST(CollisionTest, BulletCircleHit) {
    EXPECT_TRUE(nf::bullet_circle_collision(100.0f, 100.0f, 102.0f, 100.0f, 5.0f));
}

TEST(CollisionTest, BulletCircleMiss) {
    EXPECT_FALSE(nf::bullet_circle_collision(100.0f, 100.0f, 200.0f, 200.0f, 5.0f));
}

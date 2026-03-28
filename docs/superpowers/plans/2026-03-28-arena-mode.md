# Arena Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an arena battle game mode where ships rotate, thrust, and fight each other in a large 2D world with configurable teams, boundaries, and scoring.

**Architecture:** Shared entity layer (Triangle, Bullet, Tower, Token) with collision free functions, plus a new ArenaSession orchestrator. New UI screens (ArenaConfigScreen, ArenaGameScreen) with views (ArenaGameView, ArenaConfigView, ArenaGameInfoView) follow the existing 4-layer UI framework. Camera system provides follow-cam and free-cam with zoom.

**Tech Stack:** C++20, SDL2, ImGui, GoogleTest. Builds on existing neuralnet/evolve libs unchanged.

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-28-arena-mode-design.md`

---

## File Map

### New files (engine)

| File | Responsibility |
|------|----------------|
| `include/neuroflyer/arena_config.h` | `ArenaConfig` struct — world size, teams, wrapping, time limit, densities |
| `include/neuroflyer/arena_session.h` | `ArenaSession` class — shared world orchestrator, tick, scoring, spawn |
| `include/neuroflyer/camera.h` | `Camera` struct — position, zoom, follow mode, world-to-screen transform |
| `src/engine/arena_session.cpp` | ArenaSession implementation |

### New files (UI)

| File | Responsibility |
|------|----------------|
| `include/neuroflyer/ui/screens/arena_config_screen.h` | Screen: arena setup before launch |
| `include/neuroflyer/ui/screens/arena_game_screen.h` | Screen: arena game loop + evolution |
| `include/neuroflyer/ui/views/arena_config_view.h` | View: config controls (teams, map, etc.) |
| `include/neuroflyer/ui/views/arena_game_view.h` | View: arena renderer with camera |
| `include/neuroflyer/ui/views/arena_game_info_view.h` | View: stats panel (teams alive, timer) |
| `src/ui/screens/arena/arena_config_screen.cpp` | ArenaConfigScreen implementation |
| `src/ui/screens/arena/arena_game_screen.cpp` | ArenaGameScreen implementation |
| `src/ui/views/arena_config_view.cpp` | ArenaConfigView implementation |
| `src/ui/views/arena_game_view.cpp` | ArenaGameView implementation |
| `src/ui/views/arena_game_info_view.cpp` | ArenaGameInfoView implementation |

### New files (tests)

| File | Responsibility |
|------|----------------|
| `tests/arena_session_test.cpp` | ArenaSession tick, collision, scoring, spawn, boundaries |
| `tests/camera_test.cpp` | Camera transforms, clamping, follow mode |

### Modified files

| File | Change |
|------|--------|
| `include/neuroflyer/game.h` | Add `rotation` to Triangle, add `dx`,`dy`,`owner_index`,`distance_traveled` to Bullet |
| `src/engine/game.cpp` | Extract collision helpers to free functions, add rotation-aware apply_actions |
| `include/neuroflyer/collision.h` | Add `bullet_triangle_collision()` |
| `include/neuroflyer/config.h` | Add `GameMode` enum |
| `include/neuroflyer/sensor_engine.h` | Add world-size params to `build_ship_input()` |
| `src/engine/sensor_engine.cpp` | Arena-mode position/speed normalization |
| `neuroflyer/CMakeLists.txt` | Add new source files |
| `neuroflyer/tests/CMakeLists.txt` | Add new test files |
| `src/ui/screens/hangar/variant_viewer_screen.cpp` | Add "Train: Arena" button |

---

## Task 1: Add rotation to Triangle and directional fields to Bullet

**Files:**
- Modify: `include/neuroflyer/game.h`
- Modify: `src/engine/game.cpp`
- Modify: `tests/game_test.cpp`

- [ ] **Step 1: Write failing tests for rotated Triangle movement**

Add to `tests/game_test.cpp`:

```cpp
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
    b.distance_traveled = 990.0f;
    b.max_range = 1000.0f;
    b.update_directional();
    EXPECT_FALSE(b.alive);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation errors — `rotation`, `rotation_speed`, `apply_arena_actions`, `dir_x`, `dir_y`, `owner_index`, `distance_traveled`, `max_range`, `update_directional` don't exist yet.

- [ ] **Step 3: Add new fields to Triangle and Bullet in game.h**

In `include/neuroflyer/game.h`, add to `Triangle`:

```cpp
float rotation = 0.0f;          // radians, 0 = facing up, CW positive
float rotation_speed = 0.05f;   // radians per tick

void apply_arena_actions(bool up, bool down, bool left, bool right, bool shoot);
```

Add to `Bullet`:

```cpp
float dir_x = 0.0f;            // normalized direction X
float dir_y = -1.0f;           // normalized direction Y (default: up)
int owner_index = -1;           // which ship fired this
float distance_traveled = 0.0f; // for max-range cutoff
float max_range = 1000.0f;      // despawn after this distance

void update_directional();
```

- [ ] **Step 4: Implement apply_arena_actions and update_directional in game.cpp**

In `src/engine/game.cpp`, add:

```cpp
void Triangle::apply_arena_actions(bool up, bool down, bool left, bool right,
                                    bool shoot) {
    dx = 0.0f;
    dy = 0.0f;

    if (left)  rotation -= rotation_speed;
    if (right) rotation += rotation_speed;

    // Thrust in facing direction (rotation=0 is up, CW positive)
    float fx = std::sin(rotation);   // X component of facing direction
    float fy = -std::cos(rotation);  // Y component (negative = up in screen coords)

    if (up)   { dx += fx * speed; dy += fy * speed; }
    if (down) { dx -= fx * speed; dy -= fy * speed; }

    wants_shoot = shoot;
}

void Bullet::update_directional() {
    float move_x = dir_x * SPEED;
    float move_y = dir_y * SPEED;
    x += move_x;
    y += move_y;
    distance_traveled += std::sqrt(move_x * move_x + move_y * move_y);
    if (distance_traveled >= max_range) {
        alive = false;
    }
}
```

Add `#include <cmath>` to game.cpp if not already present.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R "TriangleTest|BulletTest" --output-on-failure`
Expected: All new tests PASS. Existing tests still PASS (rotation defaults to 0, existing `apply_actions` and `update` unchanged).

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/game.h src/engine/game.cpp tests/game_test.cpp
git commit -m "feat(neuroflyer): add rotation to Triangle and directional fields to Bullet"
```

---

## Task 2: Extract collision helpers to free functions

**Files:**
- Modify: `include/neuroflyer/collision.h`
- Modify: `src/engine/game.cpp`
- Modify: `tests/game_test.cpp`

- [ ] **Step 1: Write failing tests for collision free functions**

Add to `tests/game_test.cpp`:

```cpp
TEST(CollisionTest, BulletTriangleHit) {
    nf::Triangle tri(100.0f, 100.0f);
    // Bullet right on the triangle center
    EXPECT_TRUE(nf::bullet_triangle_collision(100.0f, 100.0f, tri));
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation errors — `bullet_triangle_collision`, `triangle_circle_collision`, `bullet_circle_collision` not declared.

- [ ] **Step 3: Add free collision functions to collision.h**

In `include/neuroflyer/collision.h`, add:

```cpp
/// Check if a bullet point hits a triangle (using the triangle's 3 vertices).
[[nodiscard]] inline bool bullet_triangle_collision(
    float bx, float by, const Triangle& tri) {
    // Check against all 3 triangle vertices with a small hit radius
    constexpr float HIT_R = Triangle::SIZE * 0.8f;
    return point_in_circle(bx, by, tri.x, tri.y - Triangle::SIZE, HIT_R) ||
           point_in_circle(bx, by, tri.x - Triangle::SIZE * 0.6f,
                           tri.y + Triangle::SIZE * 0.5f, HIT_R) ||
           point_in_circle(bx, by, tri.x + Triangle::SIZE * 0.6f,
                           tri.y + Triangle::SIZE * 0.5f, HIT_R);
}

/// Check if any triangle vertex is inside a circle.
[[nodiscard]] inline bool triangle_circle_collision(
    const Triangle& tri, float cx, float cy, float r) {
    return point_in_circle(tri.x, tri.y - Triangle::SIZE, cx, cy, r) ||
           point_in_circle(tri.x - Triangle::SIZE * 0.6f,
                           tri.y + Triangle::SIZE * 0.5f, cx, cy, r) ||
           point_in_circle(tri.x + Triangle::SIZE * 0.6f,
                           tri.y + Triangle::SIZE * 0.5f, cx, cy, r);
}

/// Check if a bullet point is inside a circle (tower/token).
[[nodiscard]] inline bool bullet_circle_collision(
    float bx, float by, float cx, float cy, float r) {
    return point_in_circle(bx, by, cx, cy, r);
}
```

Add `#include <neuroflyer/game.h>` to collision.h (for Triangle forward reference). Or use a forward declaration if preferred.

- [ ] **Step 4: Refactor GameSession::tick() to use the new free functions**

In `src/engine/game.cpp`, replace the inline `check_collision` and `check_bullet_hit` lambdas/helpers with calls to the new free functions from `collision.h`. The behavior is identical — this is a pure extraction refactor.

- [ ] **Step 5: Run all tests to verify nothing broke**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build --output-on-failure`
Expected: All tests PASS (new collision tests + existing game tests).

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/collision.h src/engine/game.cpp tests/game_test.cpp
git commit -m "refactor(neuroflyer): extract collision helpers to free functions"
```

---

## Task 3: ArenaConfig and Camera structs

**Files:**
- Create: `include/neuroflyer/arena_config.h`
- Create: `include/neuroflyer/camera.h`
- Create: `tests/camera_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for Camera transform and clamping**

Create `tests/camera_test.cpp`:

```cpp
#include <neuroflyer/camera.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(CameraTest, WorldToScreenCenter) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    // Object at camera center should map to viewport center
    auto [sx, sy] = cam.world_to_screen(500.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 400.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, WorldToScreenOffset) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    // Object 100px to the right of camera center
    auto [sx, sy] = cam.world_to_screen(600.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 500.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, WorldToScreenZoomed) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 2.0f;
    // At 2x zoom, 50 world px = 100 screen px
    auto [sx, sy] = cam.world_to_screen(550.0f, 500.0f, 800, 600);
    EXPECT_FLOAT_EQ(sx, 500.0f);
    EXPECT_FLOAT_EQ(sy, 300.0f);
}

TEST(CameraTest, ClampToWorldBounds) {
    nf::Camera cam;
    cam.x = 10.0f;   // near left edge
    cam.y = 10.0f;   // near top edge
    cam.zoom = 1.0f;
    cam.clamp_to_world(1000.0f, 1000.0f, 800, 600);
    // Camera should be clamped so viewport doesn't show past world edge
    EXPECT_GE(cam.x, 400.0f);  // half viewport width
    EXPECT_GE(cam.y, 300.0f);  // half viewport height
}

TEST(CameraTest, ScreenToWorld) {
    nf::Camera cam;
    cam.x = 500.0f;
    cam.y = 500.0f;
    cam.zoom = 1.0f;
    // Screen center should map back to camera position
    auto [wx, wy] = cam.screen_to_world(400.0f, 300.0f, 800, 600);
    EXPECT_FLOAT_EQ(wx, 500.0f);
    EXPECT_FLOAT_EQ(wy, 500.0f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation errors — `camera.h` doesn't exist yet.

- [ ] **Step 3: Create arena_config.h**

Create `include/neuroflyer/arena_config.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroflyer {

struct ArenaConfig {
    // World dimensions in pixels
    float world_width = 1280.0f * 64.0f;
    float world_height = 800.0f * 64.0f;

    // Teams
    std::size_t num_teams = 2;
    std::size_t team_size = 50;

    // Round timing
    uint32_t time_limit_ticks = 60 * 60;  // 60 seconds at 60fps
    std::size_t rounds_per_generation = 1;

    // Obstacles
    std::size_t tower_count = 200;
    std::size_t token_count = 100;

    // Bullets
    float bullet_max_range = 1000.0f;

    // Boundaries
    bool wrap_ns = true;
    bool wrap_ew = true;

    // Ship
    float rotation_speed = 0.05f;   // radians per tick

    // Derived
    [[nodiscard]] std::size_t population_size() const noexcept {
        return num_teams * team_size;
    }
};

} // namespace neuroflyer
```

- [ ] **Step 4: Create camera.h**

Create `include/neuroflyer/camera.h`:

```cpp
#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace neuroflyer {

struct Camera {
    float x = 0.0f;            // world X of camera center
    float y = 0.0f;            // world Y of camera center
    float zoom = 1.0f;         // 1.0 = 1:1, >1 = zoomed in, <1 = zoomed out
    bool following = true;      // true = tracking a ship
    int follow_index = 0;      // which ship to follow

    static constexpr float MIN_ZOOM = 0.05f;  // see whole arena
    static constexpr float MAX_ZOOM = 4.0f;   // close-up
    static constexpr float PAN_SPEED = 5.0f;  // pixels per tick at zoom=1

    /// Convert world coordinates to screen (viewport) coordinates.
    [[nodiscard]] std::pair<float, float> world_to_screen(
        float wx, float wy, int viewport_w, int viewport_h) const {
        float sx = (wx - x) * zoom + static_cast<float>(viewport_w) / 2.0f;
        float sy = (wy - y) * zoom + static_cast<float>(viewport_h) / 2.0f;
        return {sx, sy};
    }

    /// Convert screen coordinates back to world coordinates.
    [[nodiscard]] std::pair<float, float> screen_to_world(
        float sx, float sy, int viewport_w, int viewport_h) const {
        float wx = (sx - static_cast<float>(viewport_w) / 2.0f) / zoom + x;
        float wy = (sy - static_cast<float>(viewport_h) / 2.0f) / zoom + y;
        return {wx, wy};
    }

    /// Clamp camera so the viewport never shows past world boundaries.
    void clamp_to_world(float world_w, float world_h,
                        int viewport_w, int viewport_h) {
        float half_vw = static_cast<float>(viewport_w) / (2.0f * zoom);
        float half_vh = static_cast<float>(viewport_h) / (2.0f * zoom);
        x = std::clamp(x, half_vw, world_w - half_vw);
        y = std::clamp(y, half_vh, world_h - half_vh);
    }

    /// Zoom in or out, clamped to [MIN_ZOOM, MAX_ZOOM].
    void adjust_zoom(float delta) {
        zoom = std::clamp(zoom + delta, MIN_ZOOM, MAX_ZOOM);
    }
};

} // namespace neuroflyer
```

- [ ] **Step 5: Add camera_test.cpp to tests/CMakeLists.txt**

Add `camera_test.cpp` to the test executable source list in `tests/CMakeLists.txt`.

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R "CameraTest" --output-on-failure`
Expected: All camera tests PASS.

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/arena_config.h include/neuroflyer/camera.h tests/camera_test.cpp tests/CMakeLists.txt
git commit -m "feat(neuroflyer): add ArenaConfig and Camera structs with tests"
```

---

## Task 4: ArenaSession — core engine class

**Files:**
- Create: `include/neuroflyer/arena_session.h`
- Create: `src/engine/arena_session.cpp`
- Create: `tests/arena_session_test.cpp`
- Modify: `neuroflyer/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for ArenaSession**

Create `tests/arena_session_test.cpp`:

```cpp
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
    EXPECT_EQ(arena.ships().size(), 10u);   // 2 teams * 5
    EXPECT_EQ(arena.towers().size(), 10u);
    EXPECT_EQ(arena.tokens().size(), 5u);
    EXPECT_FALSE(arena.is_over());
}

TEST(ArenaSessionTest, TeamAssignment) {
    nf::ArenaConfig config;
    config.num_teams = 4;
    config.team_size = 3;
    nf::ArenaSession arena(config, 42);
    // Ship 0,1,2 = team 0; Ship 3,4,5 = team 1; etc.
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
        // Ship rotation should roughly face center
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
    // Tick 60 times = 1 second at 60fps = 1 point each
    for (int i = 0; i < 60; ++i) arena.tick();
    // Both ships alive, each should have ~1.0 point (1 pt/sec)
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
    // Force ship to top edge
    arena.ships()[0].y = -1.0f;
    arena.apply_boundary_rules();
    EXPECT_GT(arena.ships()[0].y, 90.0f);  // wrapped to bottom
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
    // Force ship past right edge
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
    // Place a bullet at ship 0's location, owned by ship 0
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
    // Ship 0 should still be alive (self-hit skipped)
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
    // Place a bullet at ship 1's location, owned by ship 0
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
    // Ship 1 should be dead
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
    // Kill all of team 1
    arena.ships()[1].alive = false;
    arena.tick();
    EXPECT_TRUE(arena.is_over());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation errors — `arena_session.h` doesn't exist.

- [ ] **Step 3: Create arena_session.h**

Create `include/neuroflyer/arena_session.h`:

```cpp
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/game.h>

#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

class ArenaSession {
public:
    ArenaSession(const ArenaConfig& config, uint32_t seed);

    /// Advance one tick: move ships, update bullets, resolve collisions, check end.
    void tick();

    /// Apply ship actions for a specific ship index.
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);

    /// Apply world boundary rules (wrap or clamp).
    void apply_boundary_rules();

    /// Resolve bullet-vs-ship collisions (skip self-hits).
    void resolve_bullet_ship_collisions();

    /// Add a bullet to the arena (used by tick and tests).
    void add_bullet(const Bullet& b);

    // --- Queries ---
    [[nodiscard]] bool is_over() const noexcept;
    [[nodiscard]] uint32_t current_tick() const noexcept { return tick_count_; }
    [[nodiscard]] int team_of(std::size_t ship_idx) const noexcept;
    [[nodiscard]] std::vector<float> get_scores() const;
    [[nodiscard]] std::size_t alive_count() const noexcept;
    [[nodiscard]] std::size_t teams_alive() const;

    // --- Mutable access (for tests and external tick logic) ---
    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const ArenaConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_obstacles();
    void spawn_bullets_from_ships();
    void update_bullets();
    void resolve_bullet_tower_collisions();
    void resolve_ship_tower_collisions();
    void resolve_ship_token_collisions();
    void check_end_conditions();

    ArenaConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    std::vector<int> team_assignments_;   // ship index → team ID
    std::vector<float> survival_ticks_;   // per-ship survival tick counter
    std::mt19937 rng_;
    uint32_t tick_count_ = 0;
    bool over_ = false;
};

} // namespace neuroflyer
```

- [ ] **Step 4: Create arena_session.cpp**

Create `src/engine/arena_session.cpp`:

```cpp
#include <neuroflyer/arena_session.h>
#include <neuroflyer/collision.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace neuroflyer {

ArenaSession::ArenaSession(const ArenaConfig& config, uint32_t seed)
    : config_(config), rng_(seed) {
    std::size_t pop = config.population_size();
    ships_.reserve(pop);
    team_assignments_.resize(pop);
    survival_ticks_.resize(pop, 0.0f);

    // Assign teams: ship i belongs to team i / team_size
    for (std::size_t i = 0; i < pop; ++i) {
        team_assignments_[i] = static_cast<int>(i / config.team_size);
    }

    spawn_ships();
    spawn_obstacles();
}

void ArenaSession::spawn_ships() {
    float cx = config_.world_width / 2.0f;
    float cy = config_.world_height / 2.0f;
    float radius = std::min(config_.world_width, config_.world_height) / 2.0f;
    float inner = radius / 3.0f;
    float outer = radius * 2.0f / 3.0f;

    std::size_t pop = config_.population_size();
    float slice_angle = 2.0f * static_cast<float>(M_PI) /
                        static_cast<float>(config_.num_teams);

    std::uniform_real_distribution<float> r_dist(inner, outer);
    // Within each slice, spread ships across ~60% of the slice width
    // to avoid clustering at slice edges
    std::uniform_real_distribution<float> angle_frac(-0.3f, 0.3f);

    for (std::size_t i = 0; i < pop; ++i) {
        int team = team_assignments_[i];
        float base_angle = static_cast<float>(team) * slice_angle;
        float r = r_dist(rng_);
        float a = base_angle + angle_frac(rng_) * slice_angle;

        float sx = cx + r * std::cos(a);
        float sy = cy + r * std::sin(a);

        Triangle tri(sx, sy);
        tri.speed = 2.0f;
        tri.rotation_speed = config_.rotation_speed;

        // Face toward center
        float to_cx = cx - sx;
        float to_cy = cy - sy;
        tri.rotation = std::atan2(to_cx, -to_cy);

        ships_.push_back(tri);
    }
}

void ArenaSession::spawn_obstacles() {
    std::uniform_real_distribution<float> x_dist(0.0f, config_.world_width);
    std::uniform_real_distribution<float> y_dist(0.0f, config_.world_height);
    std::uniform_real_distribution<float> r_dist(15.0f, 35.0f);

    towers_.reserve(config_.tower_count);
    for (std::size_t i = 0; i < config_.tower_count; ++i) {
        Tower t;
        t.x = x_dist(rng_);
        t.y = y_dist(rng_);
        t.radius = r_dist(rng_);
        t.alive = true;
        towers_.push_back(t);
    }

    tokens_.reserve(config_.token_count);
    for (std::size_t i = 0; i < config_.token_count; ++i) {
        Token tok;
        tok.x = x_dist(rng_);
        tok.y = y_dist(rng_);
        tok.alive = true;
        tokens_.push_back(tok);
    }
}

void ArenaSession::set_ship_actions(std::size_t idx,
                                     bool up, bool down, bool left, bool right,
                                     bool shoot) {
    if (idx < ships_.size() && ships_[idx].alive) {
        ships_[idx].apply_arena_actions(up, down, left, right, shoot);
    }
}

void ArenaSession::tick() {
    if (over_) return;

    // 1. Update ship positions
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        ship.x += ship.dx;
        ship.y += ship.dy;
    }

    // 2. Apply boundary rules
    apply_boundary_rules();

    // 3. Spawn bullets from ships that want to shoot
    spawn_bullets_from_ships();

    // 4. Update bullets
    update_bullets();

    // 5. Collisions
    resolve_bullet_ship_collisions();
    resolve_bullet_tower_collisions();
    resolve_ship_tower_collisions();
    resolve_ship_token_collisions();

    // 6. Survival scoring
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            survival_ticks_[i] += 1.0f;
        }
    }

    // 7. Cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 8. Clean up dead bullets
    bullets_.erase(
        std::remove_if(bullets_.begin(), bullets_.end(),
                        [](const Bullet& b) { return !b.alive; }),
        bullets_.end());

    ++tick_count_;

    // 9. Check end
    check_end_conditions();
}

void ArenaSession::apply_boundary_rules() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;

        if (config_.wrap_ew) {
            if (ship.x < 0.0f) ship.x += config_.world_width;
            if (ship.x > config_.world_width) ship.x -= config_.world_width;
        } else {
            ship.x = std::clamp(ship.x, Triangle::SIZE,
                                 config_.world_width - Triangle::SIZE);
        }

        if (config_.wrap_ns) {
            if (ship.y < 0.0f) ship.y += config_.world_height;
            if (ship.y > config_.world_height) ship.y -= config_.world_height;
        } else {
            ship.y = std::clamp(ship.y, Triangle::SIZE,
                                 config_.world_height - Triangle::SIZE);
        }
    }
}

void ArenaSession::spawn_bullets_from_ships() {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        auto& ship = ships_[i];
        if (!ship.alive || !ship.wants_shoot || ship.shoot_cooldown > 0)
            continue;

        Bullet b;
        // Spawn bullet at ship's nose (offset in facing direction)
        b.x = ship.x + std::sin(ship.rotation) * Triangle::SIZE;
        b.y = ship.y - std::cos(ship.rotation) * Triangle::SIZE;
        b.alive = true;
        b.dir_x = std::sin(ship.rotation);
        b.dir_y = -std::cos(ship.rotation);
        b.owner_index = static_cast<int>(i);
        b.distance_traveled = 0.0f;
        b.max_range = config_.bullet_max_range;

        bullets_.push_back(b);
        ship.shoot_cooldown = ship.fire_cooldown;
    }
}

void ArenaSession::update_bullets() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        b.update_directional();

        // Destroy at world boundary (bullets never wrap)
        if (b.x < 0.0f || b.x > config_.world_width ||
            b.y < 0.0f || b.y > config_.world_height) {
            b.alive = false;
        }
    }
}

void ArenaSession::add_bullet(const Bullet& b) {
    bullets_.push_back(b);
}

void ArenaSession::resolve_bullet_ship_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (!ships_[i].alive) continue;
            if (static_cast<int>(i) == b.owner_index) continue;  // skip self
            if (bullet_triangle_collision(b.x, b.y, ships_[i])) {
                ships_[i].alive = false;
                b.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_bullet_tower_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& t : towers_) {
            if (!t.alive) continue;
            if (bullet_circle_collision(b.x, b.y, t.x, t.y, t.radius)) {
                t.alive = false;
                b.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_ship_tower_collisions() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        for (const auto& t : towers_) {
            if (!t.alive) continue;
            if (triangle_circle_collision(ship, t.x, t.y, t.radius)) {
                ship.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_ship_token_collisions() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        for (auto& tok : tokens_) {
            if (!tok.alive) continue;
            float dx = ship.x - tok.x;
            float dy = ship.y - tok.y;
            if (dx * dx + dy * dy < (tok.radius + Triangle::SIZE) *
                                     (tok.radius + Triangle::SIZE)) {
                tok.alive = false;
                // Token scoring can be added here later
            }
        }
    }
}

void ArenaSession::check_end_conditions() {
    // Time limit
    if (tick_count_ >= config_.time_limit_ticks) {
        over_ = true;
        return;
    }

    // Last team standing (or zero teams)
    if (teams_alive() <= 1) {
        over_ = true;
    }
}

bool ArenaSession::is_over() const noexcept {
    return over_;
}

int ArenaSession::team_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < team_assignments_.size())
        return team_assignments_[ship_idx];
    return -1;
}

std::vector<float> ArenaSession::get_scores() const {
    std::vector<float> scores(ships_.size());
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        // 1 point per second of survival (60 ticks = 1 second)
        scores[i] = survival_ticks_[i] / 60.0f;
    }
    return scores;
}

std::size_t ArenaSession::alive_count() const noexcept {
    std::size_t count = 0;
    for (const auto& s : ships_) {
        if (s.alive) ++count;
    }
    return count;
}

std::size_t ArenaSession::teams_alive() const {
    std::set<int> alive_teams;
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            alive_teams.insert(team_assignments_[i]);
        }
    }
    return alive_teams.size();
}

} // namespace neuroflyer
```

- [ ] **Step 5: Add arena_session.cpp to CMakeLists.txt and arena_session_test.cpp to tests/CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add `src/engine/arena_session.cpp` to the engine sources section.

In `neuroflyer/tests/CMakeLists.txt`, add `arena_session_test.cpp` to the test source list, and add `../src/engine/arena_session.cpp` to the engine sources list.

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R "ArenaSessionTest" --output-on-failure`
Expected: All arena session tests PASS.

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/arena_session.h src/engine/arena_session.cpp tests/arena_session_test.cpp neuroflyer/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(neuroflyer): add ArenaSession engine class with tests"
```

---

## Task 5: GameMode enum and sensor_engine arena support

**Files:**
- Modify: `include/neuroflyer/config.h`
- Modify: `include/neuroflyer/sensor_engine.h`
- Modify: `src/engine/sensor_engine.cpp`
- Modify: `tests/input_vector_test.cpp`

- [ ] **Step 1: Add GameMode enum to config.h**

In `include/neuroflyer/config.h`, add before `GameConfig`:

```cpp
enum class GameMode { Scroller, Arena };
```

- [ ] **Step 2: Write failing test for arena-mode build_ship_input**

Add to `tests/input_vector_test.cpp`:

```cpp
TEST(InputVectorTest, ArenaPositionNormalization) {
    nf::ShipDesign design;
    design.memory_slots = 0;
    // No sensors — just system inputs
    std::vector<nf::Tower> towers;
    std::vector<nf::Token> tokens;
    std::vector<float> memory;
    // Ship at center of a 1000x1000 world
    auto input = nf::build_ship_input(
        design, 500.0f, 500.0f,
        1000.0f, 1000.0f,
        0.0f,   // scroll_speed = 0 for arena
        500.0f, // pts_per_token
        towers, tokens, memory);
    // Position should be near 0,0 (center) when normalized to [-1,1]
    // Find the system input indices (after sensor inputs)
    std::size_t sys_start = input.size() - 3;  // pos_x, pos_y, speed
    EXPECT_NEAR(input[sys_start], 0.0f, 0.01f);   // X center
    EXPECT_NEAR(input[sys_start + 1], 0.0f, 0.01f); // Y center
    EXPECT_FLOAT_EQ(input[sys_start + 2], 0.0f);    // speed = 0
}
```

- [ ] **Step 3: Run test to verify it fails or passes**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R "ArenaPositionNormalization" --output-on-failure`

This test should actually pass with the current implementation if `build_ship_input` already normalizes against `game_w`/`game_h` parameters. Verify it passes. If it does, the existing `build_ship_input` already works for arena — no changes needed to sensor_engine.

- [ ] **Step 4: Run full test suite to verify nothing broke**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build --output-on-failure`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/config.h tests/input_vector_test.cpp
git commit -m "feat(neuroflyer): add GameMode enum, verify sensor_engine arena compatibility"
```

---

## Task 6: ArenaConfigScreen and ArenaConfigView

**Files:**
- Create: `include/neuroflyer/ui/screens/arena_config_screen.h`
- Create: `include/neuroflyer/ui/views/arena_config_view.h`
- Create: `src/ui/screens/arena/arena_config_screen.cpp`
- Create: `src/ui/views/arena_config_view.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create arena_config_screen.h**

Create `include/neuroflyer/ui/screens/arena_config_screen.h`:

```cpp
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/ui/ui_screen.h>

namespace neuroflyer {

class ArenaConfigScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "ArenaConfig"; }

private:
    ArenaConfig config_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create arena_config_view.h**

Create `include/neuroflyer/ui/views/arena_config_view.h`:

```cpp
#pragma once

#include <neuroflyer/arena_config.h>

namespace neuroflyer {

/// Draw arena configuration controls. Returns true when user clicks "Start".
[[nodiscard]] bool draw_arena_config_view(ArenaConfig& config);

} // namespace neuroflyer
```

- [ ] **Step 3: Create arena_config_view.cpp**

Create `src/ui/views/arena_config_view.cpp`:

```cpp
#include <neuroflyer/ui/views/arena_config_view.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

namespace neuroflyer {

bool draw_arena_config_view(ArenaConfig& config) {
    bool start = false;

    ui::section_header("Teams");
    int num_teams = static_cast<int>(config.num_teams);
    int team_size = static_cast<int>(config.team_size);
    ui::input_int("Number of Teams", &num_teams, 1, 200);
    ui::input_int("Team Size", &team_size, 1, 100);
    config.num_teams = static_cast<std::size_t>(std::max(1, num_teams));
    config.team_size = static_cast<std::size_t>(std::max(1, team_size));
    ImGui::Text("Population: %zu", config.population_size());

    ImGui::Spacing();
    ui::section_header("World");
    float world_w = config.world_width;
    float world_h = config.world_height;
    ImGui::InputFloat("World Width", &world_w, 100.0f, 1000.0f, "%.0f");
    ImGui::InputFloat("World Height", &world_h, 100.0f, 1000.0f, "%.0f");
    config.world_width = std::max(100.0f, world_w);
    config.world_height = std::max(100.0f, world_h);

    ImGui::Spacing();
    ui::section_header("Boundaries");
    ImGui::Checkbox("North/South Wrap", &config.wrap_ns);
    ImGui::Checkbox("East/West Wrap", &config.wrap_ew);

    ImGui::Spacing();
    ui::section_header("Round");
    int time_limit_sec = static_cast<int>(config.time_limit_ticks / 60);
    ui::input_int("Time Limit (seconds)", &time_limit_sec, 1, 600);
    config.time_limit_ticks = static_cast<uint32_t>(std::max(1, time_limit_sec)) * 60;
    int rounds = static_cast<int>(config.rounds_per_generation);
    ui::input_int("Rounds per Generation", &rounds, 1, 10);
    config.rounds_per_generation = static_cast<std::size_t>(std::max(1, rounds));

    ImGui::Spacing();
    ui::section_header("Obstacles");
    int towers = static_cast<int>(config.tower_count);
    int tokens = static_cast<int>(config.token_count);
    ui::input_int("Towers", &towers, 0, 5000);
    ui::input_int("Tokens", &tokens, 0, 5000);
    config.tower_count = static_cast<std::size_t>(std::max(0, towers));
    config.token_count = static_cast<std::size_t>(std::max(0, tokens));

    ImGui::Spacing();
    ui::section_header("Combat");
    ImGui::SliderFloat("Bullet Max Range", &config.bullet_max_range,
                        100.0f, 5000.0f, "%.0f");
    ImGui::SliderFloat("Rotation Speed", &config.rotation_speed,
                        0.01f, 0.2f, "%.3f rad/tick");

    ImGui::Spacing();
    ImGui::Spacing();
    if (ui::button("Start Arena", ImVec2(200, 40))) {
        start = true;
    }

    ImGui::SameLine();
    if (ui::button("Back", ImVec2(100, 40))) {
        // Signal back — handled by screen
    }

    return start;
}

} // namespace neuroflyer
```

- [ ] **Step 4: Create arena_config_screen.cpp**

Create `src/ui/screens/arena/arena_config_screen.cpp`:

```cpp
#include <neuroflyer/ui/screens/arena_config_screen.h>
#include <neuroflyer/ui/views/arena_config_view.h>
#include <neuroflyer/ui/screens/arena_game_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

#include <imgui.h>

namespace neuroflyer {

void ArenaConfigScreen::on_draw(AppState& state, Renderer& renderer,
                                 UIManager& ui) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(renderer.screen_w()),
        static_cast<float>(renderer.screen_h())));
    ImGui::Begin("Arena Configuration", nullptr,
                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("Arena Mode Configuration");
    ImGui::Separator();
    ImGui::Spacing();

    if (draw_arena_config_view(config_)) {
        // Start arena — push ArenaGameScreen
        ui.push_screen(std::make_unique<ArenaGameScreen>(config_));
    }

    // Handle Escape to go back
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer
```

Note: `ArenaGameScreen` is referenced here but created in Task 7. This file won't compile until Task 7 is done. That's OK — we'll add all new source files to CMake together.

- [ ] **Step 5: Add new source files to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add to UI sources:

```
src/ui/screens/arena/arena_config_screen.cpp
src/ui/views/arena_config_view.cpp
```

- [ ] **Step 6: Commit (won't compile yet — ArenaGameScreen needed)**

```bash
git add include/neuroflyer/ui/screens/arena_config_screen.h include/neuroflyer/ui/views/arena_config_view.h src/ui/screens/arena/arena_config_screen.cpp src/ui/views/arena_config_view.cpp neuroflyer/CMakeLists.txt
git commit -m "feat(neuroflyer): add ArenaConfigScreen and ArenaConfigView"
```

---

## Task 7: ArenaGameScreen, ArenaGameView, ArenaGameInfoView

**Files:**
- Create: `include/neuroflyer/ui/screens/arena_game_screen.h`
- Create: `include/neuroflyer/ui/views/arena_game_view.h`
- Create: `include/neuroflyer/ui/views/arena_game_info_view.h`
- Create: `src/ui/screens/arena/arena_game_screen.cpp`
- Create: `src/ui/views/arena_game_view.cpp`
- Create: `src/ui/views/arena_game_info_view.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create arena_game_screen.h**

Create `include/neuroflyer/ui/screens/arena_game_screen.h`:

```cpp
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/ui/ui_screen.h>

#include <memory>
#include <vector>

namespace neuroflyer {

class ArenaGameScreen : public UIScreen {
public:
    explicit ArenaGameScreen(const ArenaConfig& config);

    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "Arena"; }

private:
    void initialize(AppState& state);
    void handle_input(UIManager& ui);
    void tick_arena(AppState& state);
    void do_arena_evolution(AppState& state);
    void render_arena(Renderer& renderer);

    ArenaConfig config_;
    std::unique_ptr<ArenaSession> arena_;
    Camera camera_;

    // Population
    std::vector<Individual> population_;
    std::vector<neuralnet::Network> networks_;
    std::vector<std::vector<float>> recurrent_states_;
    EvolutionConfig evo_config_;
    ShipDesign ship_design_;

    // State
    bool initialized_ = false;
    std::size_t generation_ = 0;
    int ticks_per_frame_ = 1;
    std::size_t current_round_ = 0;
    std::vector<float> cumulative_scores_;

    // View state
    int selected_ship_ = 0;  // for net viewer, -1 = none
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create arena_game_view.h**

Create `include/neuroflyer/ui/views/arena_game_view.h`:

```cpp
#pragma once

#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>

#include <SDL2/SDL.h>
#include <vector>

namespace neuroflyer {

class ArenaGameView {
public:
    explicit ArenaGameView(SDL_Renderer* sdl_renderer);

    void set_bounds(int x, int y, int w, int h) noexcept;

    void render(const ArenaSession& arena, const Camera& camera,
                int follow_index, const std::vector<int>& team_assignments);

private:
    void render_background();
    void render_towers(const std::vector<Tower>& towers, const Camera& camera);
    void render_tokens(const std::vector<Token>& tokens, const Camera& camera);
    void render_ships(const std::vector<Triangle>& ships, const Camera& camera,
                      int follow_index, const std::vector<int>& team_assignments);
    void render_bullets(const std::vector<Bullet>& bullets, const Camera& camera);
    void render_follow_indicator(const Triangle& ship, const Camera& camera);

    SDL_Renderer* renderer_;
    int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace neuroflyer
```

- [ ] **Step 3: Create arena_game_info_view.h**

Create `include/neuroflyer/ui/views/arena_game_info_view.h`:

```cpp
#pragma once

#include <neuroflyer/arena_session.h>

#include <cstddef>

namespace neuroflyer {

struct ArenaInfoState {
    std::size_t generation = 0;
    uint32_t current_tick = 0;
    uint32_t time_limit_ticks = 0;
    std::size_t alive_count = 0;
    std::size_t total_count = 0;
    std::size_t teams_alive = 0;
    std::size_t num_teams = 0;
};

/// Draw arena stats panel. Shown when no ship is selected.
void draw_arena_game_info_view(const ArenaInfoState& info);

} // namespace neuroflyer
```

- [ ] **Step 4: Create arena_game_info_view.cpp**

Create `src/ui/views/arena_game_info_view.cpp`:

```cpp
#include <neuroflyer/ui/views/arena_game_info_view.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

namespace neuroflyer {

void draw_arena_game_info_view(const ArenaInfoState& info) {
    ui::section_header("Arena Status");

    ImGui::Text("Generation: %zu", info.generation);

    float time_remaining =
        static_cast<float>(info.time_limit_ticks - info.current_tick) / 60.0f;
    ImGui::Text("Time Remaining: %.1f s", time_remaining);

    ImGui::Spacing();
    ImGui::Text("Ships Alive: %zu / %zu", info.alive_count, info.total_count);
    ImGui::Text("Teams Alive: %zu / %zu", info.teams_alive, info.num_teams);

    ImGui::Spacing();
    ImGui::TextDisabled("Tab: cycle ships | Arrows: free cam");
    ImGui::TextDisabled("Scroll: zoom | 1-4: speed");
}

} // namespace neuroflyer
```

- [ ] **Step 5: Create arena_game_view.cpp**

Create `src/ui/views/arena_game_view.cpp`:

```cpp
#include <neuroflyer/ui/views/arena_game_view.h>

#include <cmath>

namespace neuroflyer {

namespace {
// Team colors (RGB)
constexpr struct { uint8_t r, g, b; } TEAM_COLORS[] = {
    {255,  80,  80},  // red
    { 80, 180,  80},  // green
    { 80,  80, 255},  // blue
    {255, 200,  60},  // yellow
    {200,  80, 200},  // purple
    { 80, 200, 200},  // cyan
    {255, 140,  60},  // orange
    {200, 200, 200},  // white
};
constexpr int NUM_TEAM_COLORS = sizeof(TEAM_COLORS) / sizeof(TEAM_COLORS[0]);
} // anonymous namespace

ArenaGameView::ArenaGameView(SDL_Renderer* sdl_renderer)
    : renderer_(sdl_renderer) {}

void ArenaGameView::set_bounds(int x, int y, int w, int h) noexcept {
    x_ = x; y_ = y; w_ = w; h_ = h;
}

void ArenaGameView::render(const ArenaSession& arena, const Camera& camera,
                            int follow_index,
                            const std::vector<int>& team_assignments) {
    // Set clip rect to game panel
    SDL_Rect clip = {x_, y_, w_, h_};
    SDL_RenderSetClipRect(renderer_, &clip);

    render_background();
    render_towers(arena.towers(), camera);
    render_tokens(arena.tokens(), camera);
    render_bullets(arena.bullets(), camera);
    render_ships(arena.ships(), camera, follow_index, team_assignments);

    // Follow indicator
    if (follow_index >= 0 &&
        static_cast<std::size_t>(follow_index) < arena.ships().size() &&
        arena.ships()[static_cast<std::size_t>(follow_index)].alive) {
        render_follow_indicator(
            arena.ships()[static_cast<std::size_t>(follow_index)], camera);
    }

    SDL_RenderSetClipRect(renderer_, nullptr);
}

void ArenaGameView::render_background() {
    SDL_SetRenderDrawColor(renderer_, 5, 5, 15, 255);
    SDL_Rect bg = {x_, y_, w_, h_};
    SDL_RenderFillRect(renderer_, &bg);

    // Static star dots
    SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 100);
    // Simple deterministic stars based on position
    for (int i = 0; i < 100; ++i) {
        int sx = x_ + ((i * 7919) % w_);
        int sy = y_ + ((i * 6271) % h_);
        SDL_RenderDrawPoint(renderer_, sx, sy);
    }
}

void ArenaGameView::render_towers(const std::vector<Tower>& towers,
                                   const Camera& camera) {
    for (const auto& t : towers) {
        if (!t.alive) continue;
        auto [sx, sy] = camera.world_to_screen(t.x, t.y, w_, h_);
        sx += static_cast<float>(x_);
        sy += static_cast<float>(y_);
        float sr = t.radius * camera.zoom;

        // Skip if off-screen
        if (sx + sr < x_ || sx - sr > x_ + w_ ||
            sy + sr < y_ || sy - sr > y_ + h_) continue;

        // Draw filled circle approximation (octagon)
        SDL_SetRenderDrawColor(renderer_, 100, 100, 100, 255);
        int segments = 12;
        for (int i = 0; i < segments; ++i) {
            float a1 = 2.0f * static_cast<float>(M_PI) *
                        static_cast<float>(i) / static_cast<float>(segments);
            float a2 = 2.0f * static_cast<float>(M_PI) *
                        static_cast<float>(i + 1) / static_cast<float>(segments);
            SDL_RenderDrawLine(renderer_,
                static_cast<int>(sx + sr * std::cos(a1)),
                static_cast<int>(sy + sr * std::sin(a1)),
                static_cast<int>(sx + sr * std::cos(a2)),
                static_cast<int>(sy + sr * std::sin(a2)));
        }
    }
}

void ArenaGameView::render_tokens(const std::vector<Token>& tokens,
                                   const Camera& camera) {
    for (const auto& tok : tokens) {
        if (!tok.alive) continue;
        auto [sx, sy] = camera.world_to_screen(tok.x, tok.y, w_, h_);
        sx += static_cast<float>(x_);
        sy += static_cast<float>(y_);
        float sr = tok.radius * camera.zoom;

        if (sx + sr < x_ || sx - sr > x_ + w_ ||
            sy + sr < y_ || sy - sr > y_ + h_) continue;

        SDL_SetRenderDrawColor(renderer_, 255, 200, 0, 255);
        int segments = 8;
        for (int i = 0; i < segments; ++i) {
            float a1 = 2.0f * static_cast<float>(M_PI) *
                        static_cast<float>(i) / static_cast<float>(segments);
            float a2 = 2.0f * static_cast<float>(M_PI) *
                        static_cast<float>(i + 1) / static_cast<float>(segments);
            SDL_RenderDrawLine(renderer_,
                static_cast<int>(sx + sr * std::cos(a1)),
                static_cast<int>(sy + sr * std::sin(a1)),
                static_cast<int>(sx + sr * std::cos(a2)),
                static_cast<int>(sy + sr * std::sin(a2)));
        }
    }
}

void ArenaGameView::render_ships(const std::vector<Triangle>& ships,
                                  const Camera& camera, int follow_index,
                                  const std::vector<int>& team_assignments) {
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const auto& ship = ships[i];
        if (!ship.alive) continue;

        auto [sx, sy] = camera.world_to_screen(ship.x, ship.y, w_, h_);
        sx += static_cast<float>(x_);
        sy += static_cast<float>(y_);

        // Skip if off-screen
        float size = Triangle::SIZE * camera.zoom;
        if (sx + size < x_ || sx - size > x_ + w_ ||
            sy + size < y_ || sy - size > y_ + h_) continue;

        // Team color
        int team = (i < team_assignments.size())
                    ? team_assignments[i] : 0;
        auto color = TEAM_COLORS[team % NUM_TEAM_COLORS];

        // Alpha: dimmer for non-focused ships
        uint8_t alpha = (static_cast<int>(i) == follow_index) ? 255 : 100;
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, alpha);

        // Rotated triangle (3 vertices)
        float cos_r = std::cos(ship.rotation);
        float sin_r = std::sin(ship.rotation);

        // Triangle points relative to center (facing up at rotation=0)
        float pts[][2] = {
            { 0.0f, -size},           // nose
            {-size * 0.6f, size * 0.5f},  // bottom-left
            { size * 0.6f, size * 0.5f},  // bottom-right
        };

        // Rotate and translate
        int screen_pts[3][2];
        for (int p = 0; p < 3; ++p) {
            float rx = pts[p][0] * cos_r - pts[p][1] * sin_r;
            float ry = pts[p][0] * sin_r + pts[p][1] * cos_r;
            screen_pts[p][0] = static_cast<int>(sx + rx);
            screen_pts[p][1] = static_cast<int>(sy + ry);
        }

        // Draw triangle outline
        SDL_RenderDrawLine(renderer_,
            screen_pts[0][0], screen_pts[0][1],
            screen_pts[1][0], screen_pts[1][1]);
        SDL_RenderDrawLine(renderer_,
            screen_pts[1][0], screen_pts[1][1],
            screen_pts[2][0], screen_pts[2][1]);
        SDL_RenderDrawLine(renderer_,
            screen_pts[2][0], screen_pts[2][1],
            screen_pts[0][0], screen_pts[0][1]);
    }
}

void ArenaGameView::render_bullets(const std::vector<Bullet>& bullets,
                                    const Camera& camera) {
    SDL_SetRenderDrawColor(renderer_, 255, 255, 80, 255);
    for (const auto& b : bullets) {
        if (!b.alive) continue;
        auto [sx, sy] = camera.world_to_screen(b.x, b.y, w_, h_);
        sx += static_cast<float>(x_);
        sy += static_cast<float>(y_);

        if (sx < x_ || sx > x_ + w_ || sy < y_ || sy > y_ + h_) continue;

        SDL_Rect rect = {static_cast<int>(sx) - 1, static_cast<int>(sy) - 1,
                          3, 3};
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void ArenaGameView::render_follow_indicator(const Triangle& ship,
                                             const Camera& camera) {
    auto [sx, sy] = camera.world_to_screen(ship.x, ship.y, w_, h_);
    sx += static_cast<float>(x_);
    sy += static_cast<float>(y_);

    float r = Triangle::SIZE * camera.zoom * 2.0f;
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 180);
    int segments = 24;
    for (int i = 0; i < segments; ++i) {
        float a1 = 2.0f * static_cast<float>(M_PI) *
                    static_cast<float>(i) / static_cast<float>(segments);
        float a2 = 2.0f * static_cast<float>(M_PI) *
                    static_cast<float>(i + 1) / static_cast<float>(segments);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(sx + r * std::cos(a1)),
            static_cast<int>(sy + r * std::sin(a1)),
            static_cast<int>(sx + r * std::cos(a2)),
            static_cast<int>(sy + r * std::sin(a2)));
    }
}

} // namespace neuroflyer
```

- [ ] **Step 6: Create arena_game_screen.cpp**

Create `src/ui/screens/arena/arena_game_screen.cpp`:

```cpp
#include <neuroflyer/ui/screens/arena_game_screen.h>
#include <neuroflyer/ui/views/arena_config_view.h>
#include <neuroflyer/ui/views/arena_game_info_view.h>
#include <neuroflyer/ui/screens/pause_config_screen.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/ui/ui_manager.h>

#include <imgui.h>
#include <algorithm>
#include <iostream>

namespace neuroflyer {

ArenaGameScreen::ArenaGameScreen(const ArenaConfig& config)
    : config_(config) {}

void ArenaGameScreen::on_enter() {
    initialized_ = false;
}

void ArenaGameScreen::initialize(AppState& state) {
    // Build population from pending (set by variant viewer)
    if (!state.pending_population.empty()) {
        population_ = std::move(state.pending_population);
    } else {
        // Fallback: create random population
        std::vector<std::size_t> hidden = {8};
        evo_config_.population_size = config_.population_size();
        population_ = create_population(
            3, hidden, 5, evo_config_, state.rng);  // minimal inputs for now
    }

    // Ensure population size matches config
    if (population_.size() != config_.population_size()) {
        // Resize by cloning or trimming
        while (population_.size() < config_.population_size()) {
            population_.push_back(population_[population_.size() % population_.size()]);
        }
        population_.resize(config_.population_size());
    }

    ship_design_ = state.pending_ship_design;
    evo_config_.population_size = config_.population_size();
    evo_config_.elitism_count = std::min(
        static_cast<std::size_t>(10), config_.population_size());

    // Compile networks
    networks_.clear();
    networks_.reserve(population_.size());
    for (auto& ind : population_) {
        networks_.push_back(ind.build_network());
    }

    // Init recurrent states
    recurrent_states_.assign(population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    // Cumulative scores for multi-round
    cumulative_scores_.assign(population_.size(), 0.0f);

    // Create first arena
    arena_ = std::make_unique<ArenaSession>(config_,
        static_cast<uint32_t>(state.rng()));

    generation_ = 1;
    current_round_ = 0;
    camera_.following = true;
    camera_.follow_index = 0;
    camera_.x = config_.world_width / 2.0f;
    camera_.y = config_.world_height / 2.0f;
    camera_.zoom = 0.5f;  // start somewhat zoomed out

    initialized_ = true;
}

void ArenaGameScreen::on_draw(AppState& state, Renderer& renderer,
                               UIManager& ui) {
    if (!initialized_) {
        initialize(state);
    }

    handle_input(ui);

    // Tick
    for (int t = 0; t < ticks_per_frame_; ++t) {
        if (arena_ && !arena_->is_over()) {
            tick_arena(state);
        }
    }

    // Check if round is over
    if (arena_ && arena_->is_over()) {
        // Accumulate scores
        auto scores = arena_->get_scores();
        for (std::size_t i = 0; i < scores.size() && i < cumulative_scores_.size(); ++i) {
            cumulative_scores_[i] += scores[i];
        }
        ++current_round_;

        if (current_round_ < config_.rounds_per_generation) {
            // Start next round
            arena_ = std::make_unique<ArenaSession>(config_,
                static_cast<uint32_t>(state.rng()));
            recurrent_states_.assign(population_.size(),
                std::vector<float>(ship_design_.memory_slots, 0.0f));
        } else {
            // All rounds done — evolve
            do_arena_evolution(state);
        }
    }

    render_arena(renderer);
}

void ArenaGameScreen::tick_arena(AppState& state) {
    if (!arena_) return;

    for (std::size_t i = 0; i < population_.size(); ++i) {
        if (!arena_->ships()[i].alive) continue;

        auto input = build_ship_input(
            ship_design_,
            arena_->ships()[i].x, arena_->ships()[i].y,
            config_.world_width, config_.world_height,
            0.0f,  // no scroll speed in arena
            0.0f,  // pts_per_token (unused scoring for now)
            arena_->towers(), arena_->tokens(),
            recurrent_states_[i]);

        auto output = networks_[i].forward(input);
        auto decoded = decode_output(output, ship_design_.memory_slots);

        recurrent_states_[i] = decoded.memory;

        arena_->set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right,
            decoded.shoot);
    }

    arena_->tick();
}

void ArenaGameScreen::do_arena_evolution(AppState& state) {
    // Assign fitness
    for (std::size_t i = 0; i < population_.size(); ++i) {
        population_[i].fitness = cumulative_scores_[i];
    }

    // Evolve
    population_ = evolve_population(population_, evo_config_, state.rng);
    ++generation_;

    // Rebuild networks
    networks_.clear();
    networks_.reserve(population_.size());
    for (auto& ind : population_) {
        networks_.push_back(ind.build_network());
    }

    // Reset for new generation
    recurrent_states_.assign(population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));
    cumulative_scores_.assign(population_.size(), 0.0f);
    current_round_ = 0;
    arena_ = std::make_unique<ArenaSession>(config_,
        static_cast<uint32_t>(state.rng()));
}

void ArenaGameScreen::handle_input(UIManager& ui) {
    if (!arena_) return;

    // Tab: cycle follow target
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        camera_.following = true;
        int dir = ImGui::GetIO().KeyShift ? -1 : 1;
        int n = static_cast<int>(arena_->ships().size());
        for (int attempt = 0; attempt < n; ++attempt) {
            camera_.follow_index = (camera_.follow_index + dir + n) % n;
            if (arena_->ships()[static_cast<std::size_t>(camera_.follow_index)].alive)
                break;
        }
        selected_ship_ = camera_.follow_index;
    }

    // Arrow keys: free cam
    float pan = Camera::PAN_SPEED / camera_.zoom;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  { camera_.x -= pan; camera_.following = false; }
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) { camera_.x += pan; camera_.following = false; }
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    { camera_.y -= pan; camera_.following = false; }
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  { camera_.y += pan; camera_.following = false; }

    // Zoom: mouse scroll
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        camera_.adjust_zoom(wheel * 0.1f);
    }

    // +/- zoom
    if (ImGui::IsKeyPressed(ImGuiKey_Equal))  camera_.adjust_zoom(0.1f);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus))  camera_.adjust_zoom(-0.1f);

    // Speed control
    if (ImGui::IsKeyPressed(ImGuiKey_1)) ticks_per_frame_ = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_2)) ticks_per_frame_ = 5;
    if (ImGui::IsKeyPressed(ImGuiKey_3)) ticks_per_frame_ = 20;
    if (ImGui::IsKeyPressed(ImGuiKey_4)) ticks_per_frame_ = 100;

    // Space: pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        ui.push_screen(std::make_unique<PauseConfigScreen>());
    }

    // Escape: quit arena
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }

    // Follow mode: update camera position
    if (camera_.following &&
        camera_.follow_index >= 0 &&
        static_cast<std::size_t>(camera_.follow_index) < arena_->ships().size() &&
        arena_->ships()[static_cast<std::size_t>(camera_.follow_index)].alive) {
        camera_.x = arena_->ships()[static_cast<std::size_t>(camera_.follow_index)].x;
        camera_.y = arena_->ships()[static_cast<std::size_t>(camera_.follow_index)].y;
    }

    // Clamp camera to world
    // Using renderer dimensions isn't available here, so use a reasonable default
    // The actual clamping happens in render_arena with real viewport dims
}

void ArenaGameScreen::render_arena(Renderer& renderer) {
    int game_w = renderer.game_w();
    int screen_h = renderer.screen_h();
    int net_w = renderer.net_w();

    // Clamp camera with actual viewport dimensions
    camera_.clamp_to_world(config_.world_width, config_.world_height,
                            game_w, screen_h);

    // Left panel: arena game view
    // Create view on stack (lightweight — just a renderer pointer + bounds)
    ArenaGameView arena_view(renderer.renderer_);
    arena_view.set_bounds(0, 0, game_w, screen_h);

    // Build team_assignments vector for rendering
    std::vector<int> team_assignments;
    team_assignments.reserve(arena_->ships().size());
    for (std::size_t i = 0; i < arena_->ships().size(); ++i) {
        team_assignments.push_back(arena_->team_of(i));
    }

    arena_view.render(*arena_, camera_, camera_.follow_index, team_assignments);

    // Divider
    SDL_SetRenderDrawColor(renderer.renderer_, 60, 60, 60, 255);
    SDL_RenderDrawLine(renderer.renderer_, game_w, 0, game_w, screen_h);

    // Right panel: info or net viewer
    ImGui::SetNextWindowPos(ImVec2(
        static_cast<float>(game_w + 1), 0.0f));
    ImGui::SetNextWindowSize(ImVec2(
        static_cast<float>(net_w - 1),
        static_cast<float>(screen_h)));
    ImGui::Begin("##arena_right", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Arena info (always shown for now, net viewer integration later)
    ArenaInfoState info;
    info.generation = generation_;
    info.current_tick = arena_ ? arena_->current_tick() : 0;
    info.time_limit_ticks = config_.time_limit_ticks;
    info.alive_count = arena_ ? arena_->alive_count() : 0;
    info.total_count = config_.population_size();
    info.teams_alive = arena_ ? arena_->teams_alive() : 0;
    info.num_teams = config_.num_teams;
    draw_arena_game_info_view(info);

    ImGui::End();
}

void ArenaGameScreen::post_render(SDL_Renderer* /*sdl_renderer*/) {
    // Reserved for deferred rendering (topology views etc.)
}

} // namespace neuroflyer
```

- [ ] **Step 7: Add all new source files to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add to UI sources:

```
src/ui/screens/arena/arena_game_screen.cpp
src/ui/views/arena_game_view.cpp
src/ui/views/arena_game_info_view.cpp
```

- [ ] **Step 8: Build the full executable to verify compilation**

Run: `cmake --build build --target neuroflyer 2>&1 | tail -30`
Expected: Compiles without errors. (May have warnings — address any `-Werror` failures.)

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h include/neuroflyer/ui/screens/arena_config_screen.h include/neuroflyer/ui/views/arena_game_view.h include/neuroflyer/ui/views/arena_game_info_view.h src/ui/screens/arena/arena_game_screen.cpp src/ui/screens/arena/arena_config_screen.cpp src/ui/views/arena_game_view.cpp src/ui/views/arena_game_info_view.cpp neuroflyer/CMakeLists.txt
git commit -m "feat(neuroflyer): add ArenaGameScreen, ArenaGameView, and ArenaGameInfoView"
```

---

## Task 8: Wire "Train: Arena" button in variant viewer

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

- [ ] **Step 1: Add include for ArenaConfigScreen**

At the top of `src/ui/screens/hangar/variant_viewer_screen.cpp`, add:

```cpp
#include <neuroflyer/ui/screens/arena_config_screen.h>
```

- [ ] **Step 2: Add "Train: Arena" buttons next to existing Train buttons**

In the variant viewer's Training section (around line 218-225), after the existing "Train Fresh" and "Train from This Variant" buttons, add arena variants:

```cpp
ImGui::Spacing();
if (ImGui::Button("Arena: Fresh (Random Weights)",
                   ImVec2(btn_w, 30))) {
    action = Action::ArenaFresh;
}
if (ImGui::Button("Arena: From This Variant",
                   ImVec2(btn_w, 30))) {
    action = Action::ArenaFrom;
}
```

- [ ] **Step 3: Add ArenaFresh and ArenaFrom to the Action enum**

In the `variant_viewer_screen.cpp` or its header, add `ArenaFresh` and `ArenaFrom` to the `Action` enum.

- [ ] **Step 4: Handle the new actions in the switch statement**

In the action handling switch (around line 571+), add cases for `ArenaFresh` and `ArenaFrom` that mirror the existing `TrainFresh`/`TrainFrom` logic but push `ArenaConfigScreen` instead of `FlySessionScreen`:

```cpp
case Action::ArenaFresh: {
    try {
        auto genome_snap = load_snapshot(vs_.genome_dir + "/genome.bin");
        EvolutionConfig evo_cfg;
        evo_cfg.population_size = state.config.population_size;
        evo_cfg.elitism_count = state.config.elitism_count;
        state.pending_population = create_population_from_snapshot(
            genome_snap, state.config.population_size, evo_cfg, state.rng);
        state.pending_ship_design = genome_snap.ship_design;
        state.training_parent_name = genome_snap.name;
        state.config.active_genome =
            std::filesystem::path(vs_.genome_dir).filename().string();
        state.config.save(state.settings_path);
        state.return_to_variant_view = true;
        ui.push_screen(std::make_unique<ArenaConfigScreen>());
    } catch (const std::exception& e) {
        std::cerr << "ArenaFresh failed: " << e.what() << "\n";
    }
    break;
}

case Action::ArenaFrom: {
    if (!vs_.variants.empty() && vs_.selected_idx >= 0 &&
        static_cast<std::size_t>(vs_.selected_idx) < vs_.variants.size()) {
        const auto& sel = vs_.variants[
            static_cast<std::size_t>(vs_.selected_idx)];
        try {
            auto variant_snap = load_snapshot(variant_path(sel));
            EvolutionConfig evo_cfg;
            evo_cfg.population_size = state.config.population_size;
            evo_cfg.elitism_count = state.config.elitism_count;
            state.pending_population = create_population_from_snapshot(
                variant_snap, state.config.population_size, evo_cfg, state.rng);
            state.pending_ship_design = variant_snap.ship_design;
            state.training_parent_name = sel.name;
            state.config.active_genome =
                std::filesystem::path(vs_.genome_dir).filename().string();
            state.config.save(state.settings_path);
            state.return_to_variant_view = true;
            ui.push_screen(std::make_unique<ArenaConfigScreen>());
        } catch (const std::exception& e) {
            std::cerr << "ArenaFrom failed: " << e.what() << "\n";
        }
    }
    break;
}
```

- [ ] **Step 5: Build and verify compilation**

Run: `cmake --build build --target neuroflyer 2>&1 | tail -20`
Expected: Compiles without errors.

- [ ] **Step 6: Run all tests to verify nothing broke**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build --output-on-failure`
Expected: All tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat(neuroflyer): wire Train Arena buttons in variant viewer"
```

---

## Task 9: Integration test — full build and manual smoke test

**Files:**
- None (verification only)

- [ ] **Step 1: Full clean build**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build 2>&1 | tail -30`
Expected: Compiles without errors or warnings (with `-Werror`).

- [ ] **Step 2: Run full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: All tests PASS.

- [ ] **Step 3: Run the application and verify arena flow**

Run: `./build/neuroflyer/neuroflyer`

Manual verification checklist:
1. Open Hangar, select a genome, see "Arena: Fresh" and "Arena: From This Variant" buttons
2. Click "Arena: Fresh" — ArenaConfigScreen appears with all config controls
3. Adjust settings (team count, time limit, etc.), click "Start Arena"
4. ArenaGameScreen appears — ships visible in the arena, teams colored
5. Ships rotate and thrust (neural net controlled)
6. Bullets fire in facing direction
7. Tab cycles followed ship, camera tracks it
8. Arrow keys break to free cam, Tab re-engages follow
9. Mouse scroll zooms in/out
10. 1-4 keys change speed
11. Space pauses (existing PauseConfigScreen)
12. Round ends on time limit → evolution → new generation starts
13. Escape returns to previous screen

- [ ] **Step 4: Final commit with any fixes**

If any issues found during smoke test, fix and commit. Otherwise, this task is just verification.

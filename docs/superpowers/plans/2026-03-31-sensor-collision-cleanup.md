# Sensor/Collision Engine Cleanup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 4 tightly-coupled architecture issues in the sensor/collision engine layer (ARCH-001, CONS-003/ARCH-005, CONS-012, ARCH-009/CONS-009).

**Architecture:** All changes are in the engine layer (`src/engine/`, `include/neuroflyer/`) and the rendering layer (`src/ui/renderers/`, `src/ui/views/`, `src/ui/screens/`). Engine code loses UI dependencies and duplication; UI code gains ownership of presentation-only functions. No new files are created.

**Tech Stack:** C++20, Google Test, CMake

---

## File Map

| File | Role in this plan |
|------|-------------------|
| `include/neuroflyer/sensor_engine.h` | Remove `build_input_colors()` declaration; add `ellipse_overlap_distance()` declaration |
| `src/engine/sensor_engine.cpp` | Remove `build_input_colors()` impl + `theme.h` import; extract `ellipse_overlap_distance()` from lambda |
| `include/neuroflyer/arena_sensor.h` | Remove `build_arena_fighter_input_colors()` declaration; add `ArenaQueryContext::for_ship()` factory |
| `src/engine/arena_sensor.cpp` | Remove `build_arena_fighter_input_colors()` impl; rewrite `ray_circle_hit()`; use `ellipse_overlap_distance()`; add `for_ship()` impl |
| `include/neuroflyer/collision.h` | No changes (just consumed by arena_sensor.cpp) |
| `include/neuroflyer/renderers/variant_net_render.h` | Add declarations for both `build_input_colors()` and `build_arena_fighter_input_colors()` |
| `src/ui/renderers/variant_net_render.cpp` | Add both color function implementations using `theme.h` |
| `src/ui/views/topology_preview_view.cpp` | Switch import from `sensor_engine.h` to `variant_net_render.h` for `build_input_colors()` |
| `src/ui/screens/hangar/hangar_screen.cpp` | Same import switch |
| `src/ui/screens/hangar/create_genome_screen.cpp` | Same import switch |
| `src/engine/arena_match.cpp` | Use `ArenaQueryContext::for_ship()` |
| `src/ui/screens/arena/arena_game_screen.cpp` | Use `ArenaQueryContext::for_ship()` |
| `src/ui/screens/game/fighter_drill_screen.cpp` | Use `ArenaQueryContext::for_ship()` |
| `tests/arena_sensor_test.cpp` | Add tests for `ellipse_overlap_distance()`, `for_ship()`, and verify `ray_circle_hit` wrapper behavior |

---

### Task 1: Extract `ellipse_overlap_distance()` (CONS-003 / ARCH-005)

Extract the duplicated ellipse overlap lambda into a shared function. This is the foundation — the occulus dedup in both sensor files depends on it.

**Files:**
- Modify: `include/neuroflyer/sensor_engine.h:26` (after `SensorShape` struct)
- Modify: `src/engine/sensor_engine.cpp:76-135` (query_occulus function)
- Test: `tests/arena_sensor_test.cpp`

- [ ] **Step 1: Write the failing test for `ellipse_overlap_distance()`**

Add to `tests/arena_sensor_test.cpp`:

```cpp
#include <neuroflyer/sensor_engine.h>

TEST(SensorEngineTest, EllipseOverlapHitInsideEllipse) {
    // Ellipse centered at (500, 385), major=100, minor=57.5, rotation=0 (facing up).
    // Object at ellipse center should be a clear hit with small normalized distance.
    nf::SensorShape shape;
    shape.center_x = 500.0f;
    shape.center_y = 385.0f;
    shape.major_radius = 100.0f;
    shape.minor_radius = 57.5f;
    shape.rotation = 0.0f;

    float d = nf::ellipse_overlap_distance(shape, 500.0f, 500.0f,
                                            500.0f, 385.0f, 10.0f);
    EXPECT_GE(d, 0.0f);
    EXPECT_LT(d, 1.0f);
}

TEST(SensorEngineTest, EllipseOverlapMissOutsideEllipse) {
    nf::SensorShape shape;
    shape.center_x = 500.0f;
    shape.center_y = 385.0f;
    shape.major_radius = 100.0f;
    shape.minor_radius = 57.5f;
    shape.rotation = 0.0f;

    // Object far to the right — outside the ellipse.
    float d = nf::ellipse_overlap_distance(shape, 500.0f, 500.0f,
                                            800.0f, 385.0f, 10.0f);
    EXPECT_LT(d, 0.0f);
}

TEST(SensorEngineTest, EllipseOverlapDistanceIncreasesWithRange) {
    nf::SensorShape shape;
    shape.center_x = 500.0f;
    shape.center_y = 385.0f;
    shape.major_radius = 100.0f;
    shape.minor_radius = 57.5f;
    shape.rotation = 0.0f;

    // Near object
    float d_near = nf::ellipse_overlap_distance(shape, 500.0f, 500.0f,
                                                 500.0f, 390.0f, 10.0f);
    // Far object (still inside ellipse)
    float d_far = nf::ellipse_overlap_distance(shape, 500.0f, 500.0f,
                                                500.0f, 310.0f, 10.0f);
    ASSERT_GE(d_near, 0.0f);
    ASSERT_GE(d_far, 0.0f);
    EXPECT_LT(d_near, d_far);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation error — `ellipse_overlap_distance` not declared.

- [ ] **Step 3: Add declaration to `sensor_engine.h`**

Add after the `SensorShape` struct (after line 26) in `include/neuroflyer/sensor_engine.h`:

```cpp
/// Test whether a circular object overlaps a sensor ellipse.
/// Returns normalized distance [0,1] if inside the inflated ellipse, or -1.0 if outside.
/// The ship position is needed for distance normalization (distance from ship to object edge).
[[nodiscard]] float ellipse_overlap_distance(
    const SensorShape& shape,
    float ship_x, float ship_y,
    float obj_x, float obj_y, float obj_radius);
```

- [ ] **Step 4: Implement `ellipse_overlap_distance()` in `sensor_engine.cpp`**

Add before the `query_occulus()` function (before the anonymous namespace closing brace at line 137):

```cpp
} // namespace (close anonymous namespace temporarily)

float ellipse_overlap_distance(
    const SensorShape& shape,
    float ship_x, float ship_y,
    float obj_x, float obj_y, float obj_radius) {
    float cos_r = std::cos(shape.rotation);
    float sin_r = std::sin(shape.rotation);
    float minor_radius = std::max(shape.minor_radius, 0.01f);

    float ddx = obj_x - shape.center_x;
    float ddy = obj_y - shape.center_y;
    float lmaj = ddx * cos_r + ddy * sin_r;
    float lmin = -ddx * sin_r + ddy * cos_r;
    float eff_maj = shape.major_radius + obj_radius;
    float eff_min = minor_radius + obj_radius;
    float val = (lmaj * lmaj) / (eff_maj * eff_maj) +
                (lmin * lmin) / (eff_min * eff_min);
    if (val <= 1.0f) {
        float obj_dx = obj_x - ship_x;
        float obj_dy = obj_y - ship_y;
        float center_dist = std::sqrt(obj_dx * obj_dx + obj_dy * obj_dy);
        float edge_dist = std::max(0.0f, center_dist - obj_radius);
        float max_reach = SHIP_SENSOR_GAP + shape.major_radius * 2.0f;
        return std::min(edge_dist / max_reach, 1.0f);
    }
    return -1.0f;
}

namespace { // reopen anonymous namespace
```

**Important:** The function must be outside the anonymous namespace since it's declared in the header. Restructure the anonymous namespace boundaries: close it before this function, reopen it after. Alternatively, move the function after the anonymous namespace closing brace on line 137 (before `query_sensor()`).

- [ ] **Step 5: Refactor `query_occulus()` to use the new function**

Replace the `check_overlap` lambda and the setup code in `query_occulus()` (lines 82-114) with:

```cpp
SensorReading query_occulus(const SensorDef& sensor,
                            float ship_x, float ship_y,
                            const std::vector<Tower>& towers,
                            const std::vector<Token>& tokens) {
    auto shape = compute_sensor_shape(sensor, ship_x, ship_y);

    float closest_dist = 1.0f;
    HitType closest_type = HitType::Nothing;

    for (const auto& tower : towers) {
        if (!tower.alive) continue;
        float d = ellipse_overlap_distance(shape, ship_x, ship_y,
                                            tower.x, tower.y, tower.radius);
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = HitType::Tower;
        }
    }

    for (const auto& token : tokens) {
        if (!token.alive) continue;
        float d = ellipse_overlap_distance(shape, ship_x, ship_y,
                                            token.x, token.y, token.radius);
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = HitType::Token;
        }
    }

    return {closest_dist, closest_type};
}
```

- [ ] **Step 6: Refactor `query_arena_occulus()` in `arena_sensor.cpp` to use the shared function**

Replace the `check_overlap` lambda and setup in `arena_sensor.cpp` lines 109-140 with calls to `ellipse_overlap_distance()`. The function is already available via `#include <neuroflyer/sensor_engine.h>`.

```cpp
ArenaSensorReading query_arena_occulus(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    SensorDef rotated = sensor;
    rotated.angle = ctx.ship_rotation + sensor.angle;
    auto shape = compute_sensor_shape(rotated, ctx.ship_x, ctx.ship_y);

    float closest_dist = 1.0f;
    ArenaHitType closest_type = ArenaHitType::Nothing;

    auto update_closest = [&](float d, ArenaHitType type) {
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = type;
        }
    };

    for (const auto& tower : ctx.towers) {
        if (!tower.alive) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      tower.x, tower.y, tower.radius),
            ArenaHitType::Tower);
    }

    for (const auto& token : ctx.tokens) {
        if (!token.alive) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      token.x, token.y, token.radius),
            ArenaHitType::Token);
    }

    for (std::size_t i = 0; i < ctx.ships.size(); ++i) {
        if (i == ctx.self_index) continue;
        const auto& ship = ctx.ships[i];
        if (!ship.alive) continue;
        bool is_friend = (i < ctx.ship_teams.size() &&
                          ctx.ship_teams[i] == ctx.self_team);
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      ship.x, ship.y, Triangle::SIZE),
            is_friend ? ArenaHitType::FriendlyShip : ArenaHitType::EnemyShip);
    }

    for (const auto& bullet : ctx.bullets) {
        if (!bullet.alive) continue;
        if (bullet.owner_index == static_cast<int>(ctx.self_index)) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      bullet.x, bullet.y, BULLET_RADIUS),
            ArenaHitType::Bullet);
    }

    return {closest_dist, closest_type};
}
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="*SensorEngine*:*ArenaSensor*"`
Expected: All tests PASS (new ellipse tests + existing occulus tests).

- [ ] **Step 8: Commit**

```bash
git add include/neuroflyer/sensor_engine.h src/engine/sensor_engine.cpp src/engine/arena_sensor.cpp tests/arena_sensor_test.cpp
git commit -m "refactor: extract ellipse_overlap_distance() to deduplicate occulus math (CONS-003/ARCH-005)"
```

---

### Task 2: Rewrite `ray_circle_hit` to use `collision.h` (CONS-012)

**Files:**
- Modify: `src/engine/arena_sensor.cpp:1-35`
- Test: `tests/arena_sensor_test.cpp`

- [ ] **Step 1: Write a test that exercises the ray-circle path through arena sensors**

The existing `RaycastDetectsEnemyShip` test already exercises this path. Add one test that specifically verifies normalized distance behavior:

```cpp
TEST(ArenaSensorTest, RaycastNormalizedDistance) {
    // A ship at 500,500 facing up with range=200.
    // An enemy at 500,400 (100px away) should have distance ~0.5.
    nf::SensorDef sensor;
    sensor.type = nf::SensorType::Raycast;
    sensor.angle = 0.0f;
    sensor.range = 200.0f;
    sensor.width = 0.0f;
    sensor.is_full_sensor = true;
    sensor.id = 1;

    nf::ArenaQueryContext ctx;
    ctx.ship_x = 500.0f;
    ctx.ship_y = 500.0f;
    ctx.ship_rotation = 0.0f;
    ctx.self_index = 0;
    ctx.self_team = 0;

    std::vector<nf::Triangle> ships = {
        nf::Triangle(500.0f, 500.0f),
        nf::Triangle(500.0f, 400.0f),
    };
    std::vector<int> ship_teams = {0, 1};
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    // Ship center is ~100px away, but hit circle radius is Triangle::SIZE * 0.8.
    // The distance should be roughly (100 - hit_r) / 200.
    EXPECT_GT(reading.distance, 0.2f);
    EXPECT_LT(reading.distance, 0.6f);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::EnemyShip);
}
```

- [ ] **Step 2: Run the test to verify it passes (baseline)**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="*RaycastNormalized*"`
Expected: PASS (current implementation should produce correct results).

- [ ] **Step 3: Rewrite `ray_circle_hit` in `arena_sensor.cpp`**

Add `#include <neuroflyer/collision.h>` after the existing includes (line 3). Then replace the `ray_circle_hit` function body (lines 17-35) with:

```cpp
float ray_circle_hit(float ox, float oy, float dx, float dy, float range,
                     float cx, float cy, float cr) {
    if (range < 1e-6f) return 1.0f;
    float t = ray_circle_intersect(ox, oy, dx, dy, cx, cy, cr);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    float dist = t * std::sqrt(dx * dx + dy * dy);
    return std::min(dist / range, 1.0f);
}
```

- [ ] **Step 4: Run all arena sensor tests to verify no regression**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="*ArenaSensor*"`
Expected: All PASS.

- [ ] **Step 5: Commit**

```bash
git add src/engine/arena_sensor.cpp tests/arena_sensor_test.cpp
git commit -m "refactor: rewrite ray_circle_hit as wrapper around collision.h (CONS-012)"
```

---

### Task 3: Add `ArenaQueryContext::for_ship()` factory (ARCH-009 / CONS-009)

**Files:**
- Modify: `include/neuroflyer/arena_sensor.h:23-36`
- Modify: `src/engine/arena_sensor.cpp` (add implementation)
- Modify: `src/engine/arena_match.cpp:140-152`
- Modify: `src/ui/screens/arena/arena_game_screen.cpp:370-382`
- Modify: `src/ui/screens/game/fighter_drill_screen.cpp:325-338`
- Test: `tests/arena_sensor_test.cpp`

- [ ] **Step 1: Write failing test for `ArenaQueryContext::for_ship()`**

Add to `tests/arena_sensor_test.cpp`:

```cpp
TEST(ArenaSensorTest, ForShipFactoryBuildsContext) {
    nf::Triangle ship(500.0f, 400.0f);
    ship.rotation = 1.5f;
    ship.alive = true;

    std::vector<nf::Tower> towers = {{100.0f, 200.0f, 15.0f, true}};
    std::vector<nf::Token> tokens;
    std::vector<nf::Triangle> ships = {ship, nf::Triangle(300.0f, 300.0f)};
    std::vector<int> ship_teams = {0, 1};
    std::vector<nf::Bullet> bullets;

    auto ctx = nf::ArenaQueryContext::for_ship(
        ship, 0, 0, 1000.0f, 800.0f,
        towers, tokens, ships, ship_teams, bullets);

    EXPECT_FLOAT_EQ(ctx.ship_x, 500.0f);
    EXPECT_FLOAT_EQ(ctx.ship_y, 400.0f);
    EXPECT_FLOAT_EQ(ctx.ship_rotation, 1.5f);
    EXPECT_EQ(ctx.self_index, 0u);
    EXPECT_EQ(ctx.self_team, 0);
    EXPECT_FLOAT_EQ(ctx.world_w, 1000.0f);
    EXPECT_FLOAT_EQ(ctx.world_h, 800.0f);
    EXPECT_EQ(ctx.towers.size(), 1u);
    EXPECT_EQ(ctx.ships.size(), 2u);
    EXPECT_EQ(ctx.ship_teams.size(), 2u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compilation error — `for_ship` is not a member of `ArenaQueryContext`.

- [ ] **Step 3: Add declaration to `arena_sensor.h`**

Add inside the `ArenaQueryContext` struct, after the `bullets` field (after line 35):

```cpp
    /// Build a context for querying sensors from a specific ship's perspective.
    [[nodiscard]] static ArenaQueryContext for_ship(
        const Triangle& ship, std::size_t index, int team,
        float world_w, float world_h,
        std::span<const Tower> towers,
        std::span<const Token> tokens,
        std::span<const Triangle> ships,
        std::span<const int> ship_teams,
        std::span<const Bullet> bullets);
```

- [ ] **Step 4: Implement `for_ship()` in `arena_sensor.cpp`**

Add after the anonymous namespace block (before `query_arena_sensor()`):

```cpp
ArenaQueryContext ArenaQueryContext::for_ship(
    const Triangle& ship, std::size_t index, int team,
    float world_w, float world_h,
    std::span<const Tower> towers,
    std::span<const Token> tokens,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Bullet> bullets) {

    ArenaQueryContext ctx;
    ctx.ship_x = ship.x;
    ctx.ship_y = ship.y;
    ctx.ship_rotation = ship.rotation;
    ctx.world_w = world_w;
    ctx.world_h = world_h;
    ctx.self_index = index;
    ctx.self_team = team;
    ctx.towers = towers;
    ctx.tokens = tokens;
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;
    ctx.bullets = bullets;
    return ctx;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="*ForShipFactory*"`
Expected: PASS.

- [ ] **Step 6: Replace boilerplate in `arena_match.cpp`**

Replace lines 140-152 in `src/engine/arena_match.cpp`:

```cpp
// Before (13 lines):
ArenaQueryContext ctx;
ctx.ship_x = arena.ships()[i].x;
// ... 11 more lines ...
ctx.bullets = arena.bullets();

// After (1 line):
auto ctx = ArenaQueryContext::for_ship(
    arena.ships()[i], i, team,
    arena_config.world_width, arena_config.world_height,
    arena.towers(), arena.tokens(),
    arena.ships(), ship_teams, arena.bullets());
```

- [ ] **Step 7: Replace boilerplate in `arena_game_screen.cpp`**

Replace lines 370-382 in `src/ui/screens/arena/arena_game_screen.cpp`:

```cpp
auto ctx = ArenaQueryContext::for_ship(
    arena_->ships()[i], i, team,
    config_.world_width, config_.world_height,
    arena_->towers(), arena_->tokens(),
    arena_->ships(), ship_teams_, arena_->bullets());
```

- [ ] **Step 8: Replace boilerplate in `fighter_drill_screen.cpp`**

Replace lines 325-338 in `src/ui/screens/game/fighter_drill_screen.cpp`:

```cpp
auto ctx = ArenaQueryContext::for_ship(
    ships[i], i, 0,
    drill_config_.world_width, drill_config_.world_height,
    session_->towers(), session_->tokens(),
    session_->ships(), drill_ship_teams_, session_->bullets());
```

- [ ] **Step 9: Build and run all tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All PASS.

- [ ] **Step 10: Commit**

```bash
git add include/neuroflyer/arena_sensor.h src/engine/arena_sensor.cpp src/engine/arena_match.cpp src/ui/screens/arena/arena_game_screen.cpp src/ui/screens/game/fighter_drill_screen.cpp tests/arena_sensor_test.cpp
git commit -m "refactor: add ArenaQueryContext::for_ship() factory to eliminate boilerplate (ARCH-009/CONS-009)"
```

---

### Task 4: Move `build_input_colors()` out of engine (ARCH-001)

This is the keystone fix — removes the `theme.h` import from `sensor_engine.cpp`.

**Files:**
- Modify: `include/neuroflyer/sensor_engine.h:92-94` (remove declaration)
- Modify: `include/neuroflyer/arena_sensor.h:76` (remove declaration)
- Modify: `src/engine/sensor_engine.cpp:3,325-353` (remove import + function)
- Modify: `src/engine/arena_sensor.cpp:313-343` (remove function)
- Modify: `include/neuroflyer/renderers/variant_net_render.h` (add declarations)
- Modify: `src/ui/renderers/variant_net_render.cpp` (add implementations + `theme.h` import)
- Modify: `src/ui/views/topology_preview_view.cpp:3` (switch import)
- Modify: `src/ui/screens/hangar/hangar_screen.cpp:13` (switch import)
- Modify: `src/ui/screens/hangar/create_genome_screen.cpp:8` (switch import)

- [ ] **Step 1: Add declarations to `variant_net_render.h`**

Add after the `build_variant_net_config()` declaration (before the closing `}` of the namespace):

```cpp
/// Build input node colors for solo/scroller nets from a ShipDesign.
/// Green=sight, Purple=sensor, Blue=system, Red=memory.
/// Presentation-only — uses theme colors. Not needed by engine code.
[[nodiscard]] std::vector<NodeStyle> build_input_colors(const ShipDesign& design);

/// Build input node colors for arena fighter nets from a ShipDesign.
/// Green=sight, Purple=sensor, Yellow=squad inputs, Red=memory.
[[nodiscard]] std::vector<NodeStyle> build_arena_fighter_input_colors(const ShipDesign& design);
```

- [ ] **Step 2: Add implementations to `variant_net_render.cpp`**

Add `#include <neuroflyer/ui/theme.h>` to the includes. Then add both functions at the end of the file (before the closing namespace brace):

```cpp
std::vector<NodeStyle> build_input_colors(const ShipDesign& design) {
    std::vector<NodeStyle> colors;

    auto ns = [](const theme::Color& c) -> NodeStyle { return {c.r, c.g, c.b}; };

    for (const auto& s : design.sensors) {
        if (!s.is_full_sensor) {
            colors.push_back(ns(theme::node_sight));
        } else {
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
        }
    }

    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));

    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        colors.push_back(ns(theme::node_memory));
    }

    return colors;
}

std::vector<NodeStyle> build_arena_fighter_input_colors(const ShipDesign& design) {
    std::vector<NodeStyle> colors;

    auto ns = [](const theme::Color& c) -> NodeStyle { return {c.r, c.g, c.b}; };

    for (const auto& s : design.sensors) {
        if (!s.is_full_sensor) {
            colors.push_back(ns(theme::node_sight));
        } else {
            for (int j = 0; j < 5; ++j) {
                colors.push_back(ns(theme::node_sensor));
            }
        }
    }

    for (std::size_t i = 0; i < ArenaConfig::squad_leader_fighter_inputs; ++i) {
        colors.push_back({220, 180, 40});  // squad leader yellow
    }

    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        colors.push_back(ns(theme::node_memory));
    }

    return colors;
}
```

Note: Add `#include <neuroflyer/arena_config.h>` to `variant_net_render.cpp` for `ArenaConfig::squad_leader_fighter_inputs`.

- [ ] **Step 3: Remove `build_input_colors()` from engine**

In `include/neuroflyer/sensor_engine.h`, remove lines 92-94:
```cpp
/// Build input node colors from a ShipDesign.
/// Green=sight, Purple=sensor, Blue=system, Red=memory.
[[nodiscard]] std::vector<NodeStyle> build_input_colors(const ShipDesign& design);
```

In `src/engine/sensor_engine.cpp`, remove the `build_input_colors()` function (lines 325-353).

Remove `#include <neuroflyer/ui/theme.h>` (line 3) from `sensor_engine.cpp`.

- [ ] **Step 4: Remove `build_arena_fighter_input_colors()` from engine**

In `include/neuroflyer/arena_sensor.h`, remove the declaration (line 76):
```cpp
[[nodiscard]] std::vector<NodeStyle> build_arena_fighter_input_colors(const ShipDesign& design);
```

In `src/engine/arena_sensor.cpp`, remove the function (lines 313-343).

- [ ] **Step 5: Update UI callers to import from `variant_net_render.h`**

In `src/ui/views/topology_preview_view.cpp`, add:
```cpp
#include <neuroflyer/renderers/variant_net_render.h>
```
(Keep the existing `#include <neuroflyer/sensor_engine.h>` — it's still needed for `compute_sensor_shape` etc. if used, but check. If only `build_input_colors` was the reason, it can be removed.)

In `src/ui/screens/hangar/hangar_screen.cpp`, add:
```cpp
#include <neuroflyer/renderers/variant_net_render.h>
```

In `src/ui/screens/hangar/create_genome_screen.cpp`, add:
```cpp
#include <neuroflyer/renderers/variant_net_render.h>
```

- [ ] **Step 6: Verify no engine code imports `theme.h`**

Run: `grep -r "theme.h" src/engine/`
Expected: No results.

- [ ] **Step 7: Build and run all tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All PASS. (Tests only compile engine sources — they never called `build_input_colors`, so removal is safe.)

- [ ] **Step 8: Build the full app**

Run: `cmake --build build`
Expected: Full build succeeds. The UI code now imports `build_input_colors` from `variant_net_render.h`.

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/sensor_engine.h include/neuroflyer/arena_sensor.h include/neuroflyer/renderers/variant_net_render.h src/engine/sensor_engine.cpp src/engine/arena_sensor.cpp src/ui/renderers/variant_net_render.cpp src/ui/views/topology_preview_view.cpp src/ui/screens/hangar/hangar_screen.cpp src/ui/screens/hangar/create_genome_screen.cpp
git commit -m "refactor: move build_input_colors to UI layer, remove theme.h from engine (ARCH-001)"
```

---

### Task 5: Update cleanup sprint tracker

**Files:**
- Modify: `docs/reviews/review-mar-30-04-46-pm/cleanup-sprint-plan.md`

- [ ] **Step 1: Mark resolved items in the sprint plan**

Update the status column for these items from `pending` to `completed`:
- ARCH-001
- CONS-003
- ARCH-005
- CONS-012
- ARCH-009
- CONS-009

Update the Status Tracker table at the bottom:
```
| Total Items | Completed | Pending | Rejected | Blocked |
|-------------|-----------|---------|----------|---------|
| 56 | 6 | 50 | 0 | 0 |
```

- [ ] **Step 2: Commit**

```bash
git add docs/reviews/review-mar-30-04-46-pm/cleanup-sprint-plan.md
git commit -m "docs: mark ARCH-001, CONS-003/ARCH-005, CONS-012, ARCH-009/CONS-009 as completed"
```

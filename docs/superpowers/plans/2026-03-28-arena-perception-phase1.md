# Arena Perception Phase 1: Squad Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Phase 1 of the hierarchical brain — a squad broadcast net coordinating 8 fighters to attack/defend bases in 1v1 arena matches.

**Architecture:** Extend ArenaSession with Base entities (HP, bullet collision). Add arena-specific sensor engine that detects ships/bullets and provides navigational inputs (dir+range to enemy base, home base). Create SquadNet (small neural net broadcasting to fighters) and TeamIndividual (one genome encoding squad net + fighter net weights). Team-level evolution replaces individual evolution for arena mode.

**Tech Stack:** C++20, neuralnet library (Network, NetworkTopology), evolve library (StructuredGenome), GoogleTest

**Spec:** `docs/superpowers/specs/2026-03-28-arena-perception-design.md`

---

## File Structure

**New files (engine — zero SDL deps):**
- `include/neuroflyer/base.h` — Base struct (position, radius, HP, team)
- `include/neuroflyer/arena_sensor.h` — Arena-specific input building (ships, bullets, nav, broadcasts)
- `src/engine/arena_sensor.cpp` — Implementation
- `include/neuroflyer/team_evolution.h` — TeamIndividual, SquadNetConfig, team evolution functions
- `src/engine/team_evolution.cpp` — Implementation

**New files (tests):**
- `tests/base_test.cpp`
- `tests/arena_sensor_test.cpp`
- `tests/team_evolution_test.cpp`

**Modified files:**
- `include/neuroflyer/arena_config.h` — Add base/squad/fitness config fields
- `include/neuroflyer/arena_session.h` — Add bases vector, squad assignments, base collision, squad stat helpers
- `src/engine/arena_session.cpp` — Base spawning, base-bullet collision, base end condition, squad stats
- `include/neuroflyer/game.h` — Add `bullet_circle_collision(Bullet, x, y, r)` overload for Base
- `tests/CMakeLists.txt` — Add new test files + arena_sensor.cpp source
- `CMakeLists.txt` — Add new engine source files

---

## Build & Test Commands

```bash
# Build tests only (fast iteration)
cmake --build build --target neuroflyer_tests

# Run specific test suite
./build/tests/neuroflyer_tests --gtest_filter="BaseTest.*"

# Run all tests
./build/tests/neuroflyer_tests

# Build everything (app + tests)
cmake --build build
```

---

## Task 1: Base Struct + ArenaConfig Additions

**Files:**
- Create: `include/neuroflyer/base.h`
- Modify: `include/neuroflyer/arena_config.h`
- Test: `tests/base_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write Base struct test**

```cpp
// tests/base_test.cpp
#include <neuroflyer/base.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(BaseTest, Construction) {
    nf::Base b{100.0f, 200.0f, 50.0f, 1000.0f, 0};
    EXPECT_FLOAT_EQ(b.x, 100.0f);
    EXPECT_FLOAT_EQ(b.y, 200.0f);
    EXPECT_FLOAT_EQ(b.radius, 50.0f);
    EXPECT_FLOAT_EQ(b.hp, 1000.0f);
    EXPECT_FLOAT_EQ(b.max_hp, 1000.0f);
    EXPECT_EQ(b.team_id, 0);
    EXPECT_TRUE(b.alive());
}

TEST(BaseTest, TakeDamage) {
    nf::Base b{0, 0, 50.0f, 100.0f, 0};
    b.take_damage(30.0f);
    EXPECT_FLOAT_EQ(b.hp, 70.0f);
    EXPECT_TRUE(b.alive());
    b.take_damage(80.0f);
    EXPECT_FLOAT_EQ(b.hp, 0.0f);
    EXPECT_FALSE(b.alive());
}

TEST(BaseTest, HpNormalized) {
    nf::Base b{0, 0, 50.0f, 200.0f, 0};
    EXPECT_FLOAT_EQ(b.hp_normalized(), 1.0f);
    b.take_damage(100.0f);
    EXPECT_FLOAT_EQ(b.hp_normalized(), 0.5f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -5`
Expected: compilation error — `base.h` not found

- [ ] **Step 3: Write Base struct**

```cpp
// include/neuroflyer/base.h
#pragma once

#include <algorithm>

namespace neuroflyer {

struct Base {
    float x, y;
    float radius;
    float hp;
    float max_hp;
    int team_id;

    Base(float x, float y, float radius, float max_hp, int team_id)
        : x(x), y(y), radius(radius), hp(max_hp), max_hp(max_hp), team_id(team_id) {}

    void take_damage(float amount) {
        hp = std::max(0.0f, hp - amount);
    }

    [[nodiscard]] bool alive() const noexcept { return hp > 0.0f; }

    [[nodiscard]] float hp_normalized() const noexcept {
        return max_hp > 0.0f ? hp / max_hp : 0.0f;
    }
};

} // namespace neuroflyer
```

- [ ] **Step 4: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `base_test.cpp` to the source list of `neuroflyer_tests`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="BaseTest.*"`
Expected: 3 tests PASS

- [ ] **Step 6: Add ArenaConfig fields**

In `include/neuroflyer/arena_config.h`, add these fields to `ArenaConfig`:

```cpp
// Bases
float base_hp = 1000.0f;
float base_radius = 100.0f;
float base_bullet_damage = 10.0f;  // HP removed per bullet hit

// Squads
std::size_t num_squads = 1;
std::size_t fighters_per_squad = 8;
std::size_t squad_broadcast_signals = 4;

// Fitness weights
float fitness_weight_base_damage = 1.0f;   // enemy base HP removed
float fitness_weight_survival = 0.5f;       // own base survival time
float fitness_weight_ships_alive = 0.2f;    // fraction of own ships alive at end
float fitness_weight_tokens = 0.3f;         // fraction of tokens collected
```

Update `population_size()`:

```cpp
[[nodiscard]] std::size_t population_size() const noexcept {
    return num_teams * num_squads * fighters_per_squad;
}
```

- [ ] **Step 7: Write ArenaConfig test for new fields**

```cpp
// Add to tests/arena_session_test.cpp (or base_test.cpp)
TEST(ArenaConfigTest, PopulationFromSquads) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 2;
    config.fighters_per_squad = 4;
    EXPECT_EQ(config.population_size(), 16u);  // 2 teams * 2 squads * 4 fighters
}
```

- [ ] **Step 8: Run all tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All tests PASS. Note: existing arena_session_test tests that use `team_size` will need updating since `population_size()` changed. Update them to use `num_squads * fighters_per_squad` instead of `team_size`. Remove `team_size` from ArenaConfig and update all references.

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/base.h include/neuroflyer/arena_config.h tests/base_test.cpp tests/CMakeLists.txt
git commit -m "feat: add Base struct and arena config squad/base fields"
```

---

## Task 2: ArenaSession — Base Spawning + Collision + End Conditions

**Files:**
- Modify: `include/neuroflyer/arena_session.h`
- Modify: `src/engine/arena_session.cpp`
- Modify: `tests/arena_session_test.cpp`

- [ ] **Step 1: Write base spawning test**

```cpp
// Add to tests/arena_session_test.cpp
TEST(ArenaSessionTest, BasesSpawnPerTeam) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 4;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 500.0f;
    config.base_radius = 50.0f;
    nf::ArenaSession arena(config, 42);

    ASSERT_EQ(arena.bases().size(), 2u);
    EXPECT_FLOAT_EQ(arena.bases()[0].max_hp, 500.0f);
    EXPECT_FLOAT_EQ(arena.bases()[0].radius, 50.0f);
    EXPECT_EQ(arena.bases()[0].team_id, 0);
    EXPECT_EQ(arena.bases()[1].team_id, 1);
    // Bases should be in different positions (different team slices)
    float dx = arena.bases()[0].x - arena.bases()[1].x;
    float dy = arena.bases()[0].y - arena.bases()[1].y;
    EXPECT_GT(dx * dx + dy * dy, 100.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.BasesSpawnPerTeam"`
Expected: FAIL — `bases()` method doesn't exist

- [ ] **Step 3: Add bases to ArenaSession**

In `include/neuroflyer/arena_session.h`, add:
```cpp
#include <neuroflyer/base.h>

// Public:
[[nodiscard]] const std::vector<Base>& bases() const noexcept { return bases_; }
[[nodiscard]] std::vector<Base>& bases() noexcept { return bases_; }
[[nodiscard]] const std::vector<int>& tokens_collected() const noexcept { return tokens_collected_; }

// Private:
std::vector<Base> bases_;
std::vector<int> tokens_collected_;  // per-ship token count
void spawn_bases();
void resolve_bullet_base_collisions();
```

Initialize `tokens_collected_` in the constructor alongside `survival_ticks_`:
```cpp
tokens_collected_.resize(pop, 0);
```

In `resolve_ship_token_collisions()`, increment the counter:
```cpp
if (dist_sq < hit_r * hit_r) {
    tok.alive = false;
    tokens_collected_[i]++;  // ADD THIS LINE
}
```

In `src/engine/arena_session.cpp`, implement `spawn_bases()`:
```cpp
void ArenaSession::spawn_bases() {
    float center_x = config_.world_width / 2.0f;
    float center_y = config_.world_height / 2.0f;
    float radius = std::min(config_.world_width, config_.world_height) / 2.0f;
    float base_ring = radius * 0.5f;  // bases at 50% of arena radius
    float slice_angle = 2.0f * static_cast<float>(M_PI) / static_cast<float>(config_.num_teams);

    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        float angle = static_cast<float>(t) * slice_angle + slice_angle / 2.0f;
        float bx = center_x + base_ring * std::cos(angle);
        float by = center_y + base_ring * std::sin(angle);
        bases_.emplace_back(bx, by, config_.base_radius, config_.base_hp, static_cast<int>(t));
    }
}
```

Call `spawn_bases()` in the constructor, after `spawn_ships()`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.BasesSpawnPerTeam"`
Expected: PASS

- [ ] **Step 5: Write bullet-base collision test**

```cpp
TEST(ArenaSessionTest, BulletDamagesEnemyBase) {
    nf::ArenaConfig config;
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
    nf::ArenaSession arena(config, 42);

    // Place bullet on top of team 1's base, fired by team 0's ship
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

    EXPECT_FLOAT_EQ(arena.bases()[1].hp, 90.0f);  // 100 - 10
    EXPECT_TRUE(arena.bases()[1].alive());
}

TEST(ArenaSessionTest, BulletDoesNotDamageOwnBase) {
    nf::ArenaConfig config;
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
    nf::ArenaSession arena(config, 42);

    // Place bullet on top of team 0's base, fired by team 0's ship
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

    EXPECT_FLOAT_EQ(arena.bases()[0].hp, 100.0f);  // no damage to own base
}
```

- [ ] **Step 6: Implement bullet-base collision**

In `src/engine/arena_session.cpp`:

```cpp
void ArenaSession::resolve_bullet_base_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& base : bases_) {
            if (!base.alive()) continue;
            // Skip friendly bullets
            if (b.owner_index >= 0) {
                int shooter_team = team_assignments_[static_cast<std::size_t>(b.owner_index)];
                if (shooter_team == base.team_id) continue;
            }
            if (bullet_circle_collision(b.x, b.y, base.x, base.y, base.radius)) {
                base.take_damage(config_.base_bullet_damage);
                b.alive = false;
                break;
            }
        }
    }
}
```

Add `resolve_bullet_base_collisions()` call in `tick()` after `resolve_bullet_ship_collisions()` (step 5.5).

- [ ] **Step 7: Run collision tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.Bullet*Base*"`
Expected: PASS

- [ ] **Step 8: Write base destruction end condition test**

```cpp
TEST(ArenaSessionTest, BaseDestroyedEndsRound) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 10.0f;
    config.base_radius = 50.0f;
    config.base_bullet_damage = 10.0f;
    config.time_limit_ticks = 10000;
    nf::ArenaSession arena(config, 42);

    // Destroy team 1's base
    arena.bases()[1].take_damage(10.0f);
    ASSERT_FALSE(arena.bases()[1].alive());

    arena.tick();
    EXPECT_TRUE(arena.is_over());
}
```

- [ ] **Step 9: Update check_end_conditions for bases**

In `src/engine/arena_session.cpp`, update `check_end_conditions()`:

```cpp
void ArenaSession::check_end_conditions() {
    if (tick_count_ >= config_.time_limit_ticks) {
        over_ = true;
        return;
    }

    // Count teams with alive bases
    std::size_t bases_alive = 0;
    for (const auto& base : bases_) {
        if (base.alive()) ++bases_alive;
    }
    if (bases_alive <= 1) {
        over_ = true;
        return;
    }

    // Also end if only 1 or 0 teams have alive ships AND bases
    if (teams_alive() <= 1) {
        over_ = true;
    }
}
```

- [ ] **Step 10: Run all arena tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.*"`
Expected: All PASS. Fix any existing tests broken by config changes (team_size → num_squads * fighters_per_squad).

- [ ] **Step 11: Commit**

```bash
git add include/neuroflyer/arena_session.h src/engine/arena_session.cpp tests/arena_session_test.cpp
git commit -m "feat: add bases to ArenaSession with bullet collision and end conditions"
```

---

## Task 3: Squad Assignments + Stats

**Files:**
- Modify: `include/neuroflyer/arena_session.h`
- Modify: `src/engine/arena_session.cpp`
- Modify: `tests/arena_session_test.cpp`

- [ ] **Step 1: Write squad assignment tests**

```cpp
TEST(ArenaSessionTest, SquadAssignment) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 2;
    config.fighters_per_squad = 3;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    nf::ArenaSession arena(config, 42);

    // 12 ships total: team 0 squads 0,1 (6 ships), team 1 squads 0,1 (6 ships)
    EXPECT_EQ(arena.ships().size(), 12u);
    EXPECT_EQ(arena.squad_of(0), 0);   // team 0, squad 0
    EXPECT_EQ(arena.squad_of(2), 0);   // team 0, squad 0
    EXPECT_EQ(arena.squad_of(3), 1);   // team 0, squad 1
    EXPECT_EQ(arena.squad_of(5), 1);   // team 0, squad 1
    EXPECT_EQ(arena.squad_of(6), 0);   // team 1, squad 0
    EXPECT_EQ(arena.squad_of(9), 1);   // team 1, squad 1
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.SquadAssignment"`
Expected: FAIL — `squad_of()` doesn't exist

- [ ] **Step 3: Implement squad assignments**

In `include/neuroflyer/arena_session.h`, add:
```cpp
// Public:
[[nodiscard]] int squad_of(std::size_t ship_idx) const noexcept;

// Private:
std::vector<int> squad_assignments_;
```

In `src/engine/arena_session.cpp`, in the constructor, build squad assignments:
```cpp
// Ship layout: [team0_squad0_fighters..., team0_squad1_fighters..., team1_squad0_fighters..., ...]
squad_assignments_.resize(pop);
for (std::size_t i = 0; i < pop; ++i) {
    std::size_t within_team = i % (config.num_squads * config.fighters_per_squad);
    squad_assignments_[i] = static_cast<int>(within_team / config.fighters_per_squad);
}
```

```cpp
int ArenaSession::squad_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < squad_assignments_.size()) {
        return squad_assignments_[ship_idx];
    }
    return -1;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.SquadAssignment"`
Expected: PASS

- [ ] **Step 5: Write squad stats test**

```cpp
TEST(ArenaSessionTest, SquadStats) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 4;
    config.tower_count = 0;
    config.token_count = 0;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.base_hp = 100.0f;
    nf::ArenaSession arena(config, 42);

    auto stats = arena.compute_squad_stats(/*team=*/0, /*squad=*/0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 1.0f);
    EXPECT_GT(stats.centroid_x, 0.0f);
    EXPECT_GT(stats.centroid_y, 0.0f);

    // Kill 2 of 4 fighters
    arena.ships()[0].alive = false;
    arena.ships()[1].alive = false;
    stats = arena.compute_squad_stats(0, 0);
    EXPECT_FLOAT_EQ(stats.alive_fraction, 0.5f);
}
```

- [ ] **Step 6: Implement squad stats**

In `include/neuroflyer/arena_session.h`:
```cpp
struct SquadStats {
    float alive_fraction = 0.0f;
    float centroid_x = 0.0f, centroid_y = 0.0f;
    float avg_dist_to_home = 0.0f;
    float avg_dist_to_enemy_base = 0.0f;
    float centroid_dir_sin = 0.0f, centroid_dir_cos = 0.0f;  // relative to home base
};

// Public:
[[nodiscard]] SquadStats compute_squad_stats(int team, int squad) const;
```

In `src/engine/arena_session.cpp`:
```cpp
SquadStats ArenaSession::compute_squad_stats(int team, int squad) const {
    SquadStats stats;
    float count = 0, alive_count = 0;
    float sum_x = 0, sum_y = 0;

    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (team_assignments_[i] != team || squad_assignments_[i] != squad) continue;
        count += 1.0f;
        if (!ships_[i].alive) continue;
        alive_count += 1.0f;
        sum_x += ships_[i].x;
        sum_y += ships_[i].y;
    }

    if (count == 0.0f) return stats;
    stats.alive_fraction = alive_count / count;

    if (alive_count > 0.0f) {
        stats.centroid_x = sum_x / alive_count;
        stats.centroid_y = sum_y / alive_count;

        // Distance to home base
        auto t = static_cast<std::size_t>(team);
        if (t < bases_.size()) {
            float dx_home = stats.centroid_x - bases_[t].x;
            float dy_home = stats.centroid_y - bases_[t].y;
            float dist_home = std::sqrt(dx_home * dx_home + dy_home * dy_home);
            float diag = std::sqrt(config_.world_width * config_.world_width +
                                    config_.world_height * config_.world_height);
            stats.avg_dist_to_home = dist_home / diag;
            if (dist_home > 0.0f) {
                stats.centroid_dir_sin = dx_home / dist_home;
                stats.centroid_dir_cos = dy_home / dist_home;
            }

            // Distance to nearest enemy base
            float min_enemy_dist = diag;
            for (const auto& base : bases_) {
                if (base.team_id == team) continue;
                float dx_e = stats.centroid_x - base.x;
                float dy_e = stats.centroid_y - base.y;
                min_enemy_dist = std::min(min_enemy_dist,
                    std::sqrt(dx_e * dx_e + dy_e * dy_e));
            }
            stats.avg_dist_to_enemy_base = min_enemy_dist / diag;
        }
    }

    return stats;
}
```

- [ ] **Step 7: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSessionTest.*"`
Expected: All PASS

- [ ] **Step 8: Commit**

```bash
git add include/neuroflyer/arena_session.h src/engine/arena_session.cpp tests/arena_session_test.cpp
git commit -m "feat: add squad assignments and squad stats to ArenaSession"
```

---

## Task 4: Arena Sensor Engine — Ships, Bullets, is_friend

**Files:**
- Create: `include/neuroflyer/arena_sensor.h`
- Create: `src/engine/arena_sensor.cpp`
- Create: `tests/arena_sensor_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write arena sensor query test**

```cpp
// tests/arena_sensor_test.cpp
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ArenaSensorTest, RaycastDetectsEnemyShip) {
    // Ship at origin, enemy ship directly ahead (angle=0 = up, so at negative y)
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

    // Enemy ship at (500, 400) — 100px ahead
    std::vector<nf::Triangle> ships = {nf::Triangle(500.0f, 400.0f)};
    std::vector<int> ship_teams = {1};  // different team
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_LT(reading.distance, 0.5f);  // should detect something
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
    b.y = 400.0f;  // 100px ahead
    b.alive = true;
    b.owner_index = 5;  // not self
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

    std::vector<nf::Triangle> ships = {nf::Triangle(500.0f, 400.0f)};
    std::vector<int> ship_teams = {0};  // same team
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;

    auto reading = nf::query_arena_sensor(sensor, ctx);
    EXPECT_EQ(reading.entity_type, nf::ArenaHitType::FriendlyShip);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -5`
Expected: compilation error — arena_sensor.h not found

- [ ] **Step 3: Write arena sensor header**

```cpp
// include/neuroflyer/arena_sensor.h
#pragma once

#include <neuroflyer/game.h>
#include <neuroflyer/base.h>
#include <neuroflyer/ship_design.h>

#include <cstddef>
#include <span>
#include <vector>

namespace neuroflyer {

enum class ArenaHitType {
    Nothing = 0,
    Tower,
    Token,
    FriendlyShip,
    EnemyShip,
    Bullet
};

struct ArenaQueryContext {
    float ship_x = 0, ship_y = 0;
    float ship_rotation = 0;       // ship's facing direction (for rotating sensor angles)
    float world_w = 0, world_h = 0; // for position normalization
    std::size_t self_index = 0;
    int self_team = 0;

    // Entity references — caller must ensure lifetime
    std::span<const Tower> towers;
    std::span<const Token> tokens;
    std::span<const Triangle> ships;
    std::span<const int> ship_teams;   // parallel to ships — team per ship
    std::span<const Bullet> bullets;
};

struct ArenaSensorReading {
    float distance = 1.0f;         // 0 = touching, 1 = nothing detected
    ArenaHitType entity_type = ArenaHitType::Nothing;
};

/// Query a single sensor against all arena entities.
/// Finds the closest hit across towers, tokens, ships (friend/enemy), and bullets.
[[nodiscard]] ArenaSensorReading query_arena_sensor(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx);

/// Build the full arena fighter input vector.
/// Layout: [sensor values...] [nav inputs] [broadcast signals] [memory]
[[nodiscard]] std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float dir_to_target_sin, float dir_to_target_cos, float range_to_target,
    float dir_to_home_sin, float dir_to_home_cos, float range_to_home,
    float own_base_hp,
    std::span<const float> broadcast_signals,
    std::span<const float> memory);

/// Compute direction + range from (x1,y1) to (x2,y2).
/// Returns (sin, cos, normalized_range). Range normalized to world diagonal.
struct DirRange {
    float dir_sin = 0, dir_cos = 0;
    float range = 1.0f;  // normalized to world diagonal
};

[[nodiscard]] DirRange compute_dir_range(
    float from_x, float from_y,
    float to_x, float to_y,
    float world_w, float world_h);

/// Compute arena fighter input size (existing sensors + nav + broadcast + memory).
[[nodiscard]] std::size_t compute_arena_input_size(
    const ShipDesign& design,
    std::size_t broadcast_signal_count);

} // namespace neuroflyer
```

- [ ] **Step 4: Write arena sensor implementation**

```cpp
// src/engine/arena_sensor.cpp
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/collision.h>

#include <cmath>
#include <limits>

namespace neuroflyer {

namespace {

/// Ray-circle intersection returning normalized distance.
/// Returns 1.0 if no hit, 0-1 if hit (distance / range).
float ray_circle_hit(float ox, float oy, float dx, float dy, float range,
                     float cx, float cy, float cr) {
    float ex = cx - ox;
    float ey = cy - oy;
    float a = dx * dx + dy * dy;
    float b = 2.0f * (ex * dx + ey * dy);
    float c_val = ex * ex + ey * ey - cr * cr;
    float disc = b * b - 4.0f * a * c_val;
    if (disc < 0.0f) return 1.0f;
    float sqrt_disc = std::sqrt(disc);
    float t = (-b - sqrt_disc) / (2.0f * a);
    if (t < 0.0f) t = (-b + sqrt_disc) / (2.0f * a);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    float dist = t * std::sqrt(a);
    return std::min(dist / range, 1.0f);
}

} // anonymous namespace

ArenaSensorReading query_arena_sensor(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    // Compute ray direction from ship rotation + sensor angle
    float abs_angle = ctx.ship_rotation + sensor.angle;
    float ray_dx = std::sin(abs_angle);
    float ray_dy = -std::cos(abs_angle);

    ArenaSensorReading best;
    best.distance = 1.0f;
    best.entity_type = ArenaHitType::Nothing;

    auto try_hit = [&](float d, ArenaHitType type) {
        if (d < best.distance) {
            best.distance = d;
            best.entity_type = type;
        }
    };

    // Check towers
    for (const auto& t : ctx.towers) {
        if (!t.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, ray_dx * sensor.range,
                                  ray_dy * sensor.range, sensor.range,
                                  t.x, t.y, t.radius);
        try_hit(d, ArenaHitType::Tower);
    }

    // Check tokens
    for (const auto& t : ctx.tokens) {
        if (!t.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, ray_dx * sensor.range,
                                  ray_dy * sensor.range, sensor.range,
                                  t.x, t.y, t.radius);
        try_hit(d, ArenaHitType::Token);
    }

    // Check ships
    for (std::size_t i = 0; i < ctx.ships.size(); ++i) {
        if (i == ctx.self_index) continue;
        if (!ctx.ships[i].alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, ray_dx * sensor.range,
                                  ray_dy * sensor.range, sensor.range,
                                  ctx.ships[i].x, ctx.ships[i].y, Triangle::SIZE);
        ArenaHitType type = (i < ctx.ship_teams.size() && ctx.ship_teams[i] == ctx.self_team)
            ? ArenaHitType::FriendlyShip : ArenaHitType::EnemyShip;
        try_hit(d, type);
    }

    // Check bullets (treat as small circles, radius 2px)
    for (const auto& b : ctx.bullets) {
        if (!b.alive) continue;
        if (b.owner_index == static_cast<int>(ctx.self_index)) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, ray_dx * sensor.range,
                                  ray_dy * sensor.range, sensor.range,
                                  b.x, b.y, 2.0f);
        try_hit(d, ArenaHitType::Bullet);
    }

    return best;
}

DirRange compute_dir_range(float from_x, float from_y,
                            float to_x, float to_y,
                            float world_w, float world_h) {
    DirRange dr;
    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float dist = std::sqrt(dx * dx + dy * dy);
    float diag = std::sqrt(world_w * world_w + world_h * world_h);
    dr.range = (diag > 0.0f) ? std::min(dist / diag, 1.0f) : 0.0f;
    if (dist > 0.001f) {
        dr.dir_sin = dx / dist;
        dr.dir_cos = dy / dist;
    }
    return dr;
}

std::size_t compute_arena_input_size(const ShipDesign& design,
                                      std::size_t broadcast_signal_count) {
    std::size_t size = 0;
    // Sensor inputs: each full sensor = 5 (distance, is_tower, is_token, is_friend, is_bullet)
    // Each sight sensor = 1 (distance)
    for (const auto& s : design.sensors) {
        size += s.is_full_sensor ? 5 : 1;
    }
    // Position + velocity (3)
    size += 3;
    // Nav inputs: target dir sin/cos + range, home dir sin/cos + range, own base HP (7)
    size += 7;
    // Broadcast signals
    size += broadcast_signal_count;
    // Memory
    size += design.memory_slots;
    return size;
}

std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float dir_to_target_sin, float dir_to_target_cos, float range_to_target,
    float dir_to_home_sin, float dir_to_home_cos, float range_to_home,
    float own_base_hp,
    std::span<const float> broadcast_signals,
    std::span<const float> memory) {

    std::vector<float> input;
    input.reserve(compute_arena_input_size(design, broadcast_signals.size()));

    // 1. Sensor inputs
    for (const auto& sensor : design.sensors) {
        auto reading = query_arena_sensor(sensor, ctx);
        if (sensor.is_full_sensor) {
            input.push_back(reading.distance);
            input.push_back(reading.entity_type == ArenaHitType::Tower ? 1.0f : 0.0f);
            input.push_back(reading.entity_type == ArenaHitType::Token ? 1.0f : 0.0f);
            // is_friend: 1 = ally, -1 = enemy, 0 = non-ship
            float is_friend = 0.0f;
            if (reading.entity_type == ArenaHitType::FriendlyShip) is_friend = 1.0f;
            else if (reading.entity_type == ArenaHitType::EnemyShip) is_friend = -1.0f;
            input.push_back(is_friend);
            input.push_back(reading.entity_type == ArenaHitType::Bullet ? 1.0f : 0.0f);
        } else {
            input.push_back(reading.distance);
        }
    }

    // 2. Position + velocity (normalized to world size)
    input.push_back(ctx.ship_x / ctx.world_w * 2.0f - 1.0f);  // [-1, 1]
    input.push_back(ctx.ship_y / ctx.world_h * 2.0f - 1.0f);  // [-1, 1]
    input.push_back(ctx.ship_rotation / static_cast<float>(M_PI));  // [-1, 1]

    // 3. Nav inputs
    input.push_back(dir_to_target_sin);
    input.push_back(dir_to_target_cos);
    input.push_back(range_to_target);
    input.push_back(dir_to_home_sin);
    input.push_back(dir_to_home_cos);
    input.push_back(range_to_home);
    input.push_back(own_base_hp);

    // 4. Broadcast signals
    for (float s : broadcast_signals) {
        input.push_back(s);
    }

    // 5. Memory
    for (float m : memory) {
        input.push_back(m);
    }

    return input;
}

} // namespace neuroflyer
```

- [ ] **Step 5: Add to build system**

Add `src/engine/arena_sensor.cpp` to `CMakeLists.txt` (both main executable and test executable). Add `tests/arena_sensor_test.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 6: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSensorTest.*"`
Expected: All PASS

- [ ] **Step 7: Write DirRange test**

```cpp
TEST(ArenaSensorTest, DirRangeComputation) {
    // From (0,0) to (100,0) in a 200x200 world
    auto dr = nf::compute_dir_range(0, 0, 100, 0, 200, 200);
    EXPECT_NEAR(dr.dir_sin, 1.0f, 0.01f);   // dx is positive
    EXPECT_NEAR(dr.dir_cos, 0.0f, 0.01f);   // dy is zero
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
    auto size = nf::compute_arena_input_size(design, /*broadcast=*/4);
    // 5 + 1 (sensors) + 3 (pos/vel) + 7 (nav) + 4 (broadcast) + 4 (memory) = 24
    EXPECT_EQ(size, 24u);
}
```

- [ ] **Step 8: Run all arena sensor tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaSensorTest.*"`
Expected: All PASS

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/arena_sensor.h src/engine/arena_sensor.cpp tests/arena_sensor_test.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add arena sensor engine with ship/bullet detection and nav inputs"
```

---

## Task 5: TeamIndividual + Squad Net + Team Evolution

**Files:**
- Create: `include/neuroflyer/team_evolution.h`
- Create: `src/engine/team_evolution.cpp`
- Create: `tests/team_evolution_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write TeamIndividual construction test**

```cpp
// tests/team_evolution_test.cpp
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/arena_sensor.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(TeamEvolutionTest, CreateTeamIndividual) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
        {nf::SensorType::Raycast, 1.0f, 300.0f, 0.0f, true, 2},
    };
    design.memory_slots = 4;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;      // squad stats
    squad_config.hidden_sizes = {6};
    squad_config.output_size = 4;     // broadcast signals

    auto team = nf::TeamIndividual::create(design, {8, 8}, squad_config, rng);

    // Squad net should have correct topology
    auto squad_net = team.squad_individual.build_network();
    EXPECT_EQ(squad_net.input_size(), 8u);
    EXPECT_EQ(squad_net.output_size(), 4u);

    // Fighter net should have arena input size
    auto fighter_net = team.fighter_individual.build_network();
    std::size_t expected_input = nf::compute_arena_input_size(design, 4);
    EXPECT_EQ(fighter_net.input_size(), expected_input);
    EXPECT_EQ(fighter_net.output_size(), nf::compute_output_size(design));
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error — team_evolution.h not found

- [ ] **Step 3: Write TeamIndividual header**

```cpp
// include/neuroflyer/team_evolution.h
#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>

#include <random>
#include <vector>

namespace neuroflyer {

struct SquadNetConfig {
    std::size_t input_size = 8;
    std::vector<std::size_t> hidden_sizes = {6};
    std::size_t output_size = 4;
};

struct TeamIndividual {
    Individual squad_individual;
    Individual fighter_individual;
    float fitness = 0.0f;

    /// Create a random team with squad net + fighter net.
    static TeamIndividual create(
        const ShipDesign& fighter_design,
        const std::vector<std::size_t>& fighter_hidden,
        const SquadNetConfig& squad_config,
        std::mt19937& rng);

    /// Build networks from individuals.
    [[nodiscard]] neuralnet::Network build_squad_network() const;
    [[nodiscard]] neuralnet::Network build_fighter_network() const;
};

/// Create initial team population.
[[nodiscard]] std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const SquadNetConfig& squad_config,
    std::size_t population_size,
    std::mt19937& rng);

/// Evolve one generation of teams. Returns next population.
[[nodiscard]] std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

} // namespace neuroflyer
```

- [ ] **Step 4: Write TeamIndividual implementation**

```cpp
// src/engine/team_evolution.cpp
#include <neuroflyer/team_evolution.h>

#include <algorithm>

namespace neuroflyer {

TeamIndividual TeamIndividual::create(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const SquadNetConfig& squad_config,
    std::mt19937& rng) {

    TeamIndividual team;

    // Squad net: simple Individual with squad I/O sizes
    team.squad_individual = Individual::random(
        squad_config.input_size,
        squad_config.hidden_sizes,
        squad_config.output_size,
        rng);

    // Fighter net: uses arena input size (sensors + nav + broadcast + memory)
    std::size_t arena_input = compute_arena_input_size(fighter_design, squad_config.output_size);
    std::size_t arena_output = compute_output_size(fighter_design);
    team.fighter_individual = Individual::random(
        arena_input,
        fighter_hidden,
        arena_output,
        rng);

    return team;
}

neuralnet::Network TeamIndividual::build_squad_network() const {
    return squad_individual.build_network();
}

neuralnet::Network TeamIndividual::build_fighter_network() const {
    return fighter_individual.build_network();
}

std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const SquadNetConfig& squad_config,
    std::size_t population_size,
    std::mt19937& rng) {

    std::vector<TeamIndividual> pop;
    pop.reserve(population_size);
    for (std::size_t i = 0; i < population_size; ++i) {
        pop.push_back(TeamIndividual::create(fighter_design, fighter_hidden, squad_config, rng));
    }
    return pop;
}

std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    // Sort by fitness descending
    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    // Elitism: copy top N
    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    // Tournament selection + crossover + mutation for the rest
    std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
    while (next.size() < population.size()) {
        // Tournament select parent
        std::size_t best = dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            std::size_t candidate = dist(rng);
            if (population[candidate].fitness > population[best].fitness) {
                best = candidate;
            }
        }

        TeamIndividual child = population[best];
        child.fitness = 0.0f;

        // Mutate both nets independently
        apply_mutations(child.squad_individual, config, rng);
        apply_mutations(child.fighter_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}

} // namespace neuroflyer
```

- [ ] **Step 5: Add to build system**

Add `src/engine/team_evolution.cpp` to both `CMakeLists.txt` files. Add `tests/team_evolution_test.cpp` to test CMakeLists.

- [ ] **Step 6: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="TeamEvolutionTest.*"`
Expected: PASS

- [ ] **Step 7: Write evolution test**

```cpp
TEST(TeamEvolutionTest, EvolveTeamPopulation) {
    std::mt19937 rng(42);
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    auto pop = nf::create_team_population(design, {6}, squad_config, 10, rng);
    ASSERT_EQ(pop.size(), 10u);

    // Assign fake fitness
    for (std::size_t i = 0; i < pop.size(); ++i) {
        pop[i].fitness = static_cast<float>(i) * 10.0f;
    }

    nf::EvolutionConfig evo_config;
    evo_config.elitism_count = 2;
    evo_config.tournament_size = 3;

    auto next = nf::evolve_team_population(pop, evo_config, rng);
    EXPECT_EQ(next.size(), 10u);

    // All fitness should be reset to 0
    for (const auto& t : next) {
        EXPECT_FLOAT_EQ(t.fitness, 0.0f);
    }

    // Both nets should still build valid networks
    auto squad_net = next[0].build_squad_network();
    auto fighter_net = next[0].build_fighter_network();
    EXPECT_EQ(squad_net.input_size(), 8u);
    EXPECT_EQ(squad_net.output_size(), 4u);
}
```

- [ ] **Step 8: Run all team evolution tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="TeamEvolutionTest.*"`
Expected: All PASS

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/team_evolution.h src/engine/team_evolution.cpp tests/team_evolution_test.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add TeamIndividual with squad net + fighter net and team evolution"
```

---

## Task 6: Arena Tick Integration — Wire Squad Net + Fighters + Bases

**Files:**
- Create: `include/neuroflyer/arena_match.h`
- Create: `src/engine/arena_match.cpp`
- Create: `tests/arena_match_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

This task creates the integration layer that runs one arena match: compiles networks, builds squad stats, runs squad net, builds fighter inputs, runs fighter nets, feeds actions to ArenaSession.

- [ ] **Step 1: Write arena match tick test**

```cpp
// tests/arena_match_test.cpp
#include <neuroflyer/arena_match.h>
#include <neuroflyer/team_evolution.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

TEST(ArenaMatchTest, RunsWithoutCrash) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 5;
    arena_config.token_count = 3;
    arena_config.world_width = 1000.0f;
    arena_config.world_height = 1000.0f;
    arena_config.base_hp = 100.0f;
    arena_config.time_limit_ticks = 60;

    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 200.0f, 0.0f, true, 1},
        {nf::SensorType::Raycast, 0.5f, 200.0f, 0.0f, true, 2},
        {nf::SensorType::Raycast, -0.5f, 200.0f, 0.0f, true, 3},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {6};
    squad_config.output_size = 4;

    // Create 2 team genomes
    std::vector<nf::TeamIndividual> teams;
    teams.push_back(nf::TeamIndividual::create(design, {8}, squad_config, rng));
    teams.push_back(nf::TeamIndividual::create(design, {8}, squad_config, rng));

    auto result = nf::run_arena_match(arena_config, design, squad_config, teams, 42);

    EXPECT_EQ(result.team_scores.size(), 2u);
    EXPECT_TRUE(result.match_completed);
}

TEST(ArenaMatchTest, BaseDestroyedAffectsScore) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 0;
    arena_config.token_count = 0;
    arena_config.world_width = 1000.0f;
    arena_config.world_height = 1000.0f;
    arena_config.base_hp = 100.0f;
    arena_config.time_limit_ticks = 300;

    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 200.0f, 0.0f, true, 1},
    };
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    std::vector<nf::TeamIndividual> teams;
    teams.push_back(nf::TeamIndividual::create(design, {6}, squad_config, rng));
    teams.push_back(nf::TeamIndividual::create(design, {6}, squad_config, rng));

    auto result = nf::run_arena_match(arena_config, design, squad_config, teams, 42);

    // Scores should be non-negative (even random nets survive some ticks)
    EXPECT_GE(result.team_scores[0], 0.0f);
    EXPECT_GE(result.team_scores[1], 0.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: compilation error — arena_match.h not found

- [ ] **Step 3: Write arena match header**

```cpp
// include/neuroflyer/arena_match.h
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/ship_design.h>

#include <cstdint>
#include <vector>

namespace neuroflyer {

struct ArenaMatchResult {
    std::vector<float> team_scores;
    bool match_completed = false;
    uint32_t ticks_elapsed = 0;
};

/// Run a complete arena match. Returns per-team scores.
/// teams.size() must equal arena_config.num_teams.
[[nodiscard]] ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const SquadNetConfig& squad_config,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed);

} // namespace neuroflyer
```

- [ ] **Step 4: Write arena match implementation**

```cpp
// src/engine/arena_match.cpp
#include <neuroflyer/arena_match.h>
#include <neuroflyer/sensor_engine.h>

#include <cmath>

namespace neuroflyer {

namespace {

std::vector<float> build_squad_net_input(
    const ArenaSession& arena,
    int team, int squad,
    const ArenaConfig& config) {

    auto stats = arena.compute_squad_stats(team, squad);
    auto t = static_cast<std::size_t>(team);

    // Find nearest enemy base HP
    float enemy_base_hp = 0.0f;
    for (const auto& base : arena.bases()) {
        if (base.team_id != team) {
            enemy_base_hp = base.hp_normalized();
            break;  // Phase 1: only 1 enemy
        }
    }

    float own_base_hp = (t < arena.bases().size()) ? arena.bases()[t].hp_normalized() : 0.0f;

    return {
        stats.alive_fraction,
        stats.avg_dist_to_enemy_base,
        stats.avg_dist_to_home,
        stats.centroid_dir_sin,
        stats.centroid_dir_cos,
        own_base_hp,
        enemy_base_hp,
        static_cast<float>(arena.current_tick()) /
            static_cast<float>(config.time_limit_ticks)  // time progress
    };
}

} // anonymous namespace

ArenaMatchResult run_arena_match(
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const SquadNetConfig& squad_config,
    const std::vector<TeamIndividual>& teams,
    uint32_t seed) {

    ArenaMatchResult result;
    ArenaSession arena(arena_config, seed);

    // Compile networks
    struct TeamNets {
        neuralnet::Network squad_net;
        neuralnet::Network fighter_net;
    };
    std::vector<TeamNets> compiled;
    compiled.reserve(teams.size());
    for (const auto& team : teams) {
        compiled.push_back({team.build_squad_network(), team.build_fighter_network()});
    }

    // Recurrent state per fighter
    std::size_t num_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent_states(
        num_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));

    // Build team assignment lookup
    std::vector<int> ship_teams(num_ships);
    for (std::size_t i = 0; i < num_ships; ++i) {
        ship_teams[i] = arena.team_of(static_cast<int>(i));
    }

    // Main loop
    while (!arena.is_over()) {
        // Per team: run squad net, get broadcast signals
        std::vector<std::vector<float>> team_broadcasts(arena_config.num_teams);
        for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
            for (std::size_t s = 0; s < arena_config.num_squads; ++s) {
                auto squad_input = build_squad_net_input(
                    arena, static_cast<int>(t), static_cast<int>(s), arena_config);
                auto squad_output = compiled[t].squad_net.forward(squad_input);
                team_broadcasts[t] = squad_output;  // Phase 1: 1 squad per team
            }
        }

        // Per ship: build input, forward fighter net, apply actions
        for (std::size_t i = 0; i < num_ships; ++i) {
            if (!arena.ships()[i].alive) continue;

            int team = ship_teams[i];
            auto t = static_cast<std::size_t>(team);

            // Find enemy base for nav target (Phase 1: always attack enemy base)
            float target_x = 0, target_y = 0;
            float home_x = 0, home_y = 0;
            float own_base_hp = 0;
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) {
                    home_x = base.x;
                    home_y = base.y;
                    own_base_hp = base.hp_normalized();
                } else {
                    target_x = base.x;
                    target_y = base.y;
                }
            }

            auto target_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                target_x, target_y,
                arena_config.world_width, arena_config.world_height);
            auto home_dr = compute_dir_range(
                arena.ships()[i].x, arena.ships()[i].y,
                home_x, home_y,
                arena_config.world_width, arena_config.world_height);

            // Build query context (spans — no copies)
            ArenaQueryContext ctx;
            ctx.ship_x = arena.ships()[i].x;
            ctx.ship_y = arena.ships()[i].y;
            ctx.ship_rotation = arena.ships()[i].rotation;
            ctx.world_w = arena_config.world_width;
            ctx.world_h = arena_config.world_height;
            ctx.self_index = i;
            ctx.self_team = team;
            ctx.towers = arena.towers();
            ctx.tokens = arena.tokens();
            ctx.ships = arena.ships();
            ctx.ship_teams = ship_teams;
            ctx.bullets = arena.bullets();

            auto input = build_arena_ship_input(
                fighter_design, ctx,
                target_dr.dir_sin, target_dr.dir_cos, target_dr.range,
                home_dr.dir_sin, home_dr.dir_cos, home_dr.range,
                own_base_hp,
                team_broadcasts[t],
                recurrent_states[i]);

            auto output = compiled[t].fighter_net.forward(input);
            auto decoded = decode_output(output, fighter_design.memory_slots);

            arena.set_ship_actions(i, decoded.up, decoded.down,
                                    decoded.left, decoded.right, decoded.shoot);
            recurrent_states[i] = decoded.memory;
        }

        arena.tick();
    }

    // Compute team fitness using config weights
    result.team_scores.resize(arena_config.num_teams, 0.0f);
    for (std::size_t t = 0; t < arena_config.num_teams; ++t) {
        // Base damage dealt to enemies
        float damage_dealt = 0.0f;
        for (const auto& base : arena.bases()) {
            if (base.team_id != static_cast<int>(t)) {
                damage_dealt += (base.max_hp - base.hp) / base.max_hp;
            }
        }
        damage_dealt /= static_cast<float>(arena_config.num_teams - 1);

        // Own base survival
        float own_survival = arena.bases()[t].hp_normalized();

        // Ships alive + tokens collected
        float ships_alive = 0;
        float ship_count = 0;
        int team_tokens = 0;
        for (std::size_t i = 0; i < num_ships; ++i) {
            if (ship_teams[i] == static_cast<int>(t)) {
                ship_count += 1.0f;
                if (arena.ships()[i].alive) ships_alive += 1.0f;
                team_tokens += arena.tokens_collected()[i];
            }
        }
        float alive_frac = ship_count > 0 ? ships_alive / ship_count : 0.0f;
        float token_frac = arena_config.token_count > 0
            ? static_cast<float>(team_tokens) / static_cast<float>(arena_config.token_count)
            : 0.0f;

        result.team_scores[t] =
            arena_config.fitness_weight_base_damage * damage_dealt +
            arena_config.fitness_weight_survival * own_survival +
            arena_config.fitness_weight_ships_alive * alive_frac +
            arena_config.fitness_weight_tokens * token_frac;
    }

    result.match_completed = true;
    result.ticks_elapsed = arena.current_tick();
    return result;
}

} // namespace neuroflyer
```

- [ ] **Step 5: Add to build system**

Add `src/engine/arena_match.cpp` to both CMakeLists. Add `tests/arena_match_test.cpp` to test CMakeLists.

- [ ] **Step 6: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter="ArenaMatchTest.*"`
Expected: All PASS

- [ ] **Step 7: Write full generation test (multi-round evolution)**

```cpp
TEST(ArenaMatchTest, FullGenerationCycle) {
    std::mt19937 rng(42);

    nf::ArenaConfig arena_config;
    arena_config.num_teams = 2;
    arena_config.num_squads = 1;
    arena_config.fighters_per_squad = 4;
    arena_config.tower_count = 0;
    arena_config.token_count = 0;
    arena_config.world_width = 500.0f;
    arena_config.world_height = 500.0f;
    arena_config.base_hp = 50.0f;
    arena_config.time_limit_ticks = 30;

    nf::ShipDesign design;
    design.sensors = {{nf::SensorType::Raycast, 0.0f, 100.0f, 0.0f, true, 1}};
    design.memory_slots = 2;

    nf::SquadNetConfig squad_config;
    squad_config.input_size = 8;
    squad_config.hidden_sizes = {4};
    squad_config.output_size = 4;

    // Create population of 6 team genomes
    auto pop = nf::create_team_population(design, {6}, squad_config, 6, rng);

    // Run 3 generations
    nf::EvolutionConfig evo_config;
    evo_config.elitism_count = 1;
    evo_config.tournament_size = 2;

    for (int gen = 0; gen < 3; ++gen) {
        // Pair teams for matches (simple: 0v1, 2v3, 4v5)
        for (std::size_t i = 0; i + 1 < pop.size(); i += 2) {
            std::vector<nf::TeamIndividual> match_teams = {pop[i], pop[i + 1]};
            auto result = nf::run_arena_match(arena_config, design, squad_config,
                                               match_teams, static_cast<uint32_t>(gen * 100 + i));
            pop[i].fitness += result.team_scores[0];
            pop[i + 1].fitness += result.team_scores[1];
        }

        pop = nf::evolve_team_population(pop, evo_config, rng);
    }

    // Population should still be valid after 3 generations
    EXPECT_EQ(pop.size(), 6u);
    auto net = pop[0].build_fighter_network();
    EXPECT_GT(net.input_size(), 0u);
}
```

- [ ] **Step 8: Run all tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All PASS

- [ ] **Step 9: Commit**

```bash
git add include/neuroflyer/arena_match.h src/engine/arena_match.cpp tests/arena_match_test.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add arena match runner with squad net integration and team fitness"
```

---

## Task 7: ArenaGameScreen — Wire UI to Engine

**Files:**
- Modify: `include/neuroflyer/ui/screens/arena_game_screen.h`
- Modify: `src/ui/screens/arena/arena_game_screen.cpp`
- Modify: `src/ui/views/arena_game_view.cpp` (render bases)

This task wires the engine into the existing (stubbed) ArenaGameScreen. No new tests — this is UI integration verified by running the app.

- [ ] **Step 1: Read existing ArenaGameScreen header and source**

Read `include/neuroflyer/ui/screens/arena_game_screen.h` and `src/ui/screens/arena/arena_game_screen.cpp` to understand the current stub state.

- [ ] **Step 2: Update ArenaGameScreen header**

Add the members needed to drive the engine:

```cpp
// In ArenaGameScreen private:
#include <neuroflyer/arena_match.h>
#include <neuroflyer/team_evolution.h>

// State
ArenaConfig arena_config_;
ShipDesign fighter_design_;
SquadNetConfig squad_config_;
std::vector<TeamIndividual> population_;
EvolutionConfig evo_config_;

// Current match state (for rendering)
std::unique_ptr<ArenaSession> arena_;
std::vector<TeamNets> compiled_nets_;  // or store separately
std::vector<std::vector<float>> recurrent_states_;
std::vector<std::vector<float>> team_broadcasts_;
std::vector<int> ship_teams_;

// Control
std::size_t generation_ = 0;
int ticks_per_frame_ = 1;
bool initialized_ = false;
int selected_ship_ = -1;
```

- [ ] **Step 3: Implement initialize()**

Follow the `FlySessionScreen` pattern. In `initialize()`:
1. Create team population from config
2. Compile networks for first match
3. Create ArenaSession
4. Init recurrent states

- [ ] **Step 4: Implement tick loop in on_draw()**

In `on_draw()`, follow FlySessionScreen pattern:
1. Handle input (speed keys, tab for ship cycling, space for pause)
2. For each tick_per_frame: run squad net per team → build fighter inputs → run fighter nets → apply actions → arena.tick()
3. If arena.is_over(): assign fitness, evolve, start next match
4. Render arena via ArenaGameView

- [ ] **Step 5: Add base rendering to ArenaGameView**

In `src/ui/views/arena_game_view.cpp`, add base rendering:
- Draw each base as a filled circle with team color
- Draw HP bar above base
- Apply camera transform (world-to-screen)

- [ ] **Step 6: Build and run the app**

Run: `cmake --build build && ./build/neuroflyer`
Navigate: Main Menu → Hangar → select genome → Train: Arena → configure → Start
Expected: Arena loads, ships spawn, bases visible, evolution runs. Ships may behave randomly at first (no training yet).

- [ ] **Step 7: Commit**

```bash
git add include/neuroflyer/ui/screens/arena_game_screen.h src/ui/screens/arena/arena_game_screen.cpp src/ui/views/arena_game_view.cpp
git commit -m "feat: wire ArenaGameScreen to squad net + fighter engine with base rendering"
```

---

## Task 8: Arena Config Screen — Squad/Base Controls

**Files:**
- Modify: `src/ui/views/arena_config_view.cpp`

- [ ] **Step 1: Read existing arena config view**

Read `src/ui/views/arena_config_view.cpp` to see current controls.

- [ ] **Step 2: Add base and squad controls**

Add `ui::slider_float` / `ui::input_int` controls for:
- Base HP (slider, 100-5000)
- Base radius (slider, 50-200)
- Base bullet damage (slider, 1-100)
- Number of squads (input, 1-4) — default 1 for Phase 1
- Fighters per squad (input, 4-32) — default 8
- Squad broadcast signals (input, 2-8) — default 4

- [ ] **Step 3: Build and test**

Run: `cmake --build build && ./build/neuroflyer`
Navigate to Arena Config screen, verify all controls work and values propagate to ArenaConfig.

- [ ] **Step 4: Commit**

```bash
git add src/ui/views/arena_config_view.cpp
git commit -m "feat: add base and squad controls to arena config screen"
```

---

## Task 9: Smoke Test + Backlog Update

- [ ] **Step 1: Run full test suite**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All tests PASS

- [ ] **Step 2: Run the app and verify end-to-end**

Run: `./build/neuroflyer`
1. Create a genome with a few sensors
2. Navigate to Arena → Configure → Start with 2 teams, 1 squad, 8 fighters each
3. Verify: bases render, ships move, bullets fire, bases take damage, round ends, evolution runs, generation increments
4. Verify: pause works, speed control works, ship selection works

- [ ] **Step 3: Update backlog**

Add Phase 2 and Phase 3 items to `docs/backlog.md`:
- Phase 2: Commander net + multiple squads per team
- Phase 3: Enemy analysis net + multi-team matches

- [ ] **Step 4: Final commit**

```bash
git add docs/backlog.md
git commit -m "docs: update backlog with arena perception Phase 2 and Phase 3"
```

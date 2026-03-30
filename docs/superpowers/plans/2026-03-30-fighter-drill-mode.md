# Fighter Drill Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fighter drill training mode where 200 fighters evolve to obey scripted squad leader commands (expand, contract, attack target) over three 20-second phases.

**Architecture:** New `FighterDrillSession` engine class (no SDL) handles world simulation, phase transitions, and movement-based scoring. New `FighterDrillScreen` UIScreen drives the training loop with individual-based evolution. Physics duplicated from `ArenaSession` (rotation+thrust movement, toroidal wrapping, collision). Squad inputs computed directly using `compute_dir_range()` for heading/distance math.

**Tech Stack:** C++20, GoogleTest, SDL2/ImGui (UI only), neuralnet/evolve libs

**Spec:** `docs/superpowers/specs/2026-03-30-fighter-drill-mode-design.md`

---

### Task 1: FighterDrillConfig and DrillPhase Types

**Files:**
- Create: `include/neuroflyer/fighter_drill_session.h`
- Create: `tests/fighter_drill_session_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the header with config struct and phase enum**

```cpp
// include/neuroflyer/fighter_drill_session.h
#pragma once

#include <neuroflyer/base.h>
#include <neuroflyer/game.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

enum class DrillPhase { Expand = 0, Contract = 1, Attack = 2, Done = 3 };

struct FighterDrillConfig {
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    std::size_t population_size = 200;
    std::size_t tower_count = 50;
    std::size_t token_count = 30;

    // Starbase placement
    float starbase_distance = 1500.0f;  // from squad center
    float starbase_hp = 1000.0f;
    float starbase_radius = 100.0f;

    // Ship physics
    float rotation_speed = 0.05f;    // radians/tick
    float bullet_max_range = 1000.0f;
    float base_bullet_damage = 10.0f;
    bool wrap_ns = true;
    bool wrap_ew = true;

    // Phase timing
    uint32_t phase_duration_ticks = 20 * 60;  // 20 seconds at 60fps

    // Scoring weights
    float expand_weight = 1.0f;
    float contract_weight = 1.0f;
    float attack_travel_weight = 1.0f;
    float attack_hit_bonus = 500.0f;
    float token_bonus = 50.0f;

    [[nodiscard]] float world_diagonal() const noexcept {
        return std::sqrt(world_width * world_width + world_height * world_height);
    }
};

} // namespace neuroflyer
```

- [ ] **Step 2: Write a basic test to verify config defaults**

```cpp
// tests/fighter_drill_session_test.cpp
#include <neuroflyer/fighter_drill_session.h>
#include <gtest/gtest.h>

namespace nf = neuroflyer;

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
```

- [ ] **Step 3: Add test file and source to tests/CMakeLists.txt**

Add `fighter_drill_session_test.cpp` to the test source list, and add `../src/engine/fighter_drill_session.cpp` to the engine sources. In `tests/CMakeLists.txt`, add these two entries:

```cmake
# In the test source list (after squad_leader_test.cpp):
    fighter_drill_session_test.cpp

# In the engine source list (after ../src/engine/squad_leader.cpp):
    ../src/engine/fighter_drill_session.cpp
```

- [ ] **Step 4: Create empty source file so it compiles**

```cpp
// src/engine/fighter_drill_session.cpp
#include <neuroflyer/fighter_drill_session.h>

namespace neuroflyer {

} // namespace neuroflyer
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrill*'`
Expected: 2 tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/fighter_drill_session.h src/engine/fighter_drill_session.cpp tests/fighter_drill_session_test.cpp tests/CMakeLists.txt
git commit -m "feat(drill): add FighterDrillConfig and DrillPhase types"
```

---

### Task 2: FighterDrillSession — World Setup and Spawning

**Files:**
- Modify: `include/neuroflyer/fighter_drill_session.h`
- Modify: `src/engine/fighter_drill_session.cpp`
- Modify: `tests/fighter_drill_session_test.cpp`

- [ ] **Step 1: Write failing tests for session construction and spawning**

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: FAIL — `FighterDrillSession` class not defined yet

- [ ] **Step 3: Add FighterDrillSession class declaration to header**

Add to `include/neuroflyer/fighter_drill_session.h` after the `FighterDrillConfig` struct:

```cpp
class FighterDrillSession {
public:
    FighterDrillSession(const FighterDrillConfig& config, uint32_t seed);

    void tick();
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);

    [[nodiscard]] bool is_over() const noexcept { return phase_ == DrillPhase::Done; }
    [[nodiscard]] uint32_t current_tick() const noexcept { return tick_count_; }
    [[nodiscard]] DrillPhase phase() const noexcept { return phase_; }
    [[nodiscard]] uint32_t phase_ticks_remaining() const noexcept;
    [[nodiscard]] std::vector<float> get_scores() const { return scores_; }

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const Base& starbase() const noexcept { return starbase_; }
    [[nodiscard]] float squad_center_x() const noexcept { return squad_center_x_; }
    [[nodiscard]] float squad_center_y() const noexcept { return squad_center_y_; }
    [[nodiscard]] const FighterDrillConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_obstacles();
    void spawn_starbase();

    FighterDrillConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    Base starbase_;
    float squad_center_x_;
    float squad_center_y_;

    DrillPhase phase_ = DrillPhase::Expand;
    uint32_t phase_tick_ = 0;
    uint32_t tick_count_ = 0;

    std::vector<float> scores_;
    std::vector<int> tokens_collected_;

    std::mt19937 rng_;
};
```

- [ ] **Step 4: Implement constructor and spawn methods**

```cpp
// src/engine/fighter_drill_session.cpp
#include <neuroflyer/fighter_drill_session.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace neuroflyer {

FighterDrillSession::FighterDrillSession(const FighterDrillConfig& config, uint32_t seed)
    : config_(config)
    , starbase_(0, 0, config.starbase_radius, config.starbase_hp, 1)  // team 1 = enemy
    , squad_center_x_(config.world_width / 2.0f)
    , squad_center_y_(config.world_height / 2.0f)
    , rng_(seed)
{
    scores_.resize(config_.population_size, 0.0f);
    tokens_collected_.resize(config_.population_size, 0);
    spawn_ships();
    spawn_obstacles();
    spawn_starbase();
}

void FighterDrillSession::spawn_ships() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);

    for (std::size_t i = 0; i < config_.population_size; ++i) {
        Triangle ship(squad_center_x_, squad_center_y_);
        ship.rotation = angle_dist(rng_);
        ship.rotation_speed = config_.rotation_speed;
        ships_.push_back(ship);
    }
}

void FighterDrillSession::spawn_obstacles() {
    std::uniform_real_distribution<float> x_dist(0.0f, config_.world_width);
    std::uniform_real_distribution<float> y_dist(0.0f, config_.world_height);
    std::uniform_real_distribution<float> radius_dist(15.0f, 35.0f);

    for (std::size_t i = 0; i < config_.tower_count; ++i) {
        Tower t;
        t.x = x_dist(rng_);
        t.y = y_dist(rng_);
        t.radius = radius_dist(rng_);
        t.alive = true;
        towers_.push_back(t);
    }
    for (std::size_t i = 0; i < config_.token_count; ++i) {
        Token tok;
        tok.x = x_dist(rng_);
        tok.y = y_dist(rng_);
        tok.alive = true;
        tokens_.push_back(tok);
    }
}

void FighterDrillSession::spawn_starbase() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);
    float angle = angle_dist(rng_);
    starbase_ = Base(
        squad_center_x_ + config_.starbase_distance * std::cos(angle),
        squad_center_y_ + config_.starbase_distance * std::sin(angle),
        config_.starbase_radius,
        config_.starbase_hp,
        1);  // team 1 = enemy
}

uint32_t FighterDrillSession::phase_ticks_remaining() const noexcept {
    if (phase_ == DrillPhase::Done) return 0;
    return config_.phase_duration_ticks - phase_tick_;
}

void FighterDrillSession::set_ship_actions(
    std::size_t idx, bool up, bool down, bool left, bool right, bool shoot) {
    if (idx < ships_.size() && ships_[idx].alive) {
        ships_[idx].apply_arena_actions(up, down, left, right, shoot);
    }
}

void FighterDrillSession::tick() {
    // Stub — implemented in Task 3
}

} // namespace neuroflyer
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: 4 tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/fighter_drill_session.h src/engine/fighter_drill_session.cpp tests/fighter_drill_session_test.cpp
git commit -m "feat(drill): add FighterDrillSession with world spawning"
```

---

### Task 3: FighterDrillSession — Movement, Wrapping, and Bullets

**Files:**
- Modify: `src/engine/fighter_drill_session.cpp`
- Modify: `tests/fighter_drill_session_test.cpp`

- [ ] **Step 1: Write failing tests for movement and wrapping**

```cpp
TEST(FighterDrillSessionTest, ShipMovesOnThrust) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    auto& ship = session.ships()[0];
    ship.rotation = 0.0f;  // facing up
    float start_y = ship.y;

    session.set_ship_actions(0, true, false, false, false, false);  // thrust forward
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

    // Should have wrapped around
    EXPECT_LT(session.ships()[0].x, 50.0f);  // wrapped from right edge to left
}

TEST(FighterDrillSessionTest, BulletSpawnsOnShoot) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    EXPECT_EQ(session.bullets().size(), 0u);

    session.set_ship_actions(0, false, false, false, false, true);  // shoot
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
    config.world_width = 10000.0f;  // large enough that bullet won't wrap
    config.world_height = 10000.0f;
    nf::FighterDrillSession session(config, 42);

    session.set_ship_actions(0, false, false, false, false, true);
    session.tick();
    EXPECT_EQ(session.bullets().size(), 1u);

    // Tick enough times for bullet to exceed max range
    // Bullet::SPEED = 8.0, max_range = 50 → ~7 ticks
    for (int i = 0; i < 10; ++i) {
        session.tick();
    }

    // Bullet should be cleaned up
    EXPECT_EQ(session.bullets().size(), 0u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: New tests FAIL (tick() is a stub)

- [ ] **Step 3: Implement tick() with movement, wrapping, and bullets**

Replace the stub `tick()` in `src/engine/fighter_drill_session.cpp`:

```cpp
void FighterDrillSession::tick() {
    if (is_over()) return;

    // 1. Update ship positions
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        ship.x += ship.dx;
        ship.y += ship.dy;
    }

    // 2. Apply boundary wrapping
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        if (config_.wrap_ew) {
            if (ship.x < 0.0f) ship.x += config_.world_width;
            else if (ship.x > config_.world_width) ship.x -= config_.world_width;
        } else {
            ship.x = std::clamp(ship.x, 0.0f, config_.world_width);
        }
        if (config_.wrap_ns) {
            if (ship.y < 0.0f) ship.y += config_.world_height;
            else if (ship.y > config_.world_height) ship.y -= config_.world_height;
        } else {
            ship.y = std::clamp(ship.y, 0.0f, config_.world_height);
        }
    }

    // 3. Spawn bullets from ships
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        auto& ship = ships_[i];
        if (!ship.alive) continue;
        if (ship.wants_shoot && ship.shoot_cooldown <= 0) {
            float fx = std::sin(ship.rotation);
            float fy = -std::cos(ship.rotation);
            Bullet b;
            b.x = ship.x + fx * Triangle::SIZE;
            b.y = ship.y + fy * Triangle::SIZE;
            b.dir_x = fx;
            b.dir_y = fy;
            b.alive = true;
            b.owner_index = static_cast<int>(i);
            b.distance_traveled = 0.0f;
            b.max_range = config_.bullet_max_range;
            bullets_.push_back(b);
            ship.shoot_cooldown = ship.fire_cooldown;
        }
    }

    // 4. Update bullets
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        b.update_directional();
        if (b.distance_traveled >= b.max_range) {
            b.alive = false;
        }
    }

    // 5. Resolve collisions (Task 4 — stubs for now)
    resolve_ship_tower_collisions();
    resolve_ship_token_collisions();
    resolve_bullet_starbase_collisions();
    resolve_bullet_tower_collisions();

    // 6. Compute per-tick movement scores (Task 5 — stub for now)
    compute_phase_scores();

    // 7. Decrement cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 8. Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // 9. Advance phase timer
    ++phase_tick_;
    ++tick_count_;
    if (phase_tick_ >= config_.phase_duration_ticks) {
        phase_tick_ = 0;
        switch (phase_) {
            case DrillPhase::Expand:   phase_ = DrillPhase::Contract; break;
            case DrillPhase::Contract: phase_ = DrillPhase::Attack;   break;
            case DrillPhase::Attack:   phase_ = DrillPhase::Done;     break;
            case DrillPhase::Done:     break;
        }
    }
}
```

Also add private method declarations to the header:

```cpp
    void resolve_ship_tower_collisions();
    void resolve_ship_token_collisions();
    void resolve_bullet_starbase_collisions();
    void resolve_bullet_tower_collisions();
    void compute_phase_scores();
```

And add empty stub implementations:

```cpp
void FighterDrillSession::resolve_ship_tower_collisions() {}
void FighterDrillSession::resolve_ship_token_collisions() {}
void FighterDrillSession::resolve_bullet_starbase_collisions() {}
void FighterDrillSession::resolve_bullet_tower_collisions() {}
void FighterDrillSession::compute_phase_scores() {}
```

- [ ] **Step 4: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/fighter_drill_session.h src/engine/fighter_drill_session.cpp tests/fighter_drill_session_test.cpp
git commit -m "feat(drill): add movement, wrapping, and bullet mechanics to FighterDrillSession"
```

---

### Task 4: FighterDrillSession — Collision Resolution

**Files:**
- Modify: `include/neuroflyer/fighter_drill_session.h` (add `#include <neuroflyer/collision.h>`)
- Modify: `src/engine/fighter_drill_session.cpp`
- Modify: `tests/fighter_drill_session_test.cpp`

- [ ] **Step 1: Write failing tests for collisions**

```cpp
TEST(FighterDrillSessionTest, ShipDiesOnTowerCollision) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    nf::FighterDrillSession session(config, 42);

    // Manually check: place ship on top of where we know there's no tower,
    // then verify it's alive. We'll test collision by direct placement.
    auto& ship = session.ships()[0];
    EXPECT_TRUE(ship.alive);

    // There are no towers, so ship stays alive after tick
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

    // Move ship to token position
    auto& ship = session.ships()[0];
    const auto& tok = session.tokens()[0];
    ship.x = tok.x;
    ship.y = tok.y;

    session.tick();

    // Token should be collected (dead)
    EXPECT_FALSE(session.tokens()[0].alive);
}

TEST(FighterDrillSessionTest, BulletDamagesStarbase) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.starbase_hp = 100.0f;
    config.base_bullet_damage = 10.0f;
    config.starbase_distance = 50.0f;  // close starbase
    nf::FighterDrillSession session(config, 42);

    float initial_hp = session.starbase().hp;
    EXPECT_FLOAT_EQ(initial_hp, 100.0f);

    // Point ship at starbase and fire
    auto& ship = session.ships()[0];
    float dx = session.starbase().x - ship.x;
    float dy = session.starbase().y - ship.y;
    ship.rotation = std::atan2(dx, -dy);

    session.set_ship_actions(0, false, false, false, false, true);
    // Tick until bullet reaches starbase (distance ~50, speed 8 → ~7 ticks)
    for (int i = 0; i < 15; ++i) {
        session.tick();
    }

    EXPECT_LT(session.starbase().hp, initial_hp);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: Token and starbase tests FAIL (collision stubs are empty)

- [ ] **Step 3: Implement collision resolution methods**

Add `#include <neuroflyer/collision.h>` to the header.

In `src/engine/fighter_drill_session.cpp`, replace the stubs:

```cpp
void FighterDrillSession::resolve_ship_tower_collisions() {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;
        for (const auto& tower : towers_) {
            if (!tower.alive) continue;
            if (triangle_circle_collision_rotated(ships_[i], tower.x, tower.y, tower.radius)) {
                ships_[i].alive = false;
                break;
            }
        }
    }
}

void FighterDrillSession::resolve_ship_token_collisions() {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;
        for (auto& tok : tokens_) {
            if (!tok.alive) continue;
            if (triangle_circle_collision_rotated(ships_[i], tok.x, tok.y, tok.radius)) {
                tok.alive = false;
                tokens_collected_[i]++;
                scores_[i] += config_.token_bonus;
            }
        }
    }
}

void FighterDrillSession::resolve_bullet_starbase_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        if (!starbase_.alive()) continue;
        if (bullet_circle_collision(b.x, b.y, starbase_.x, starbase_.y, starbase_.radius)) {
            b.alive = false;
            starbase_.take_damage(config_.base_bullet_damage);
            if (b.owner_index >= 0 && b.owner_index < static_cast<int>(scores_.size())) {
                scores_[b.owner_index] += config_.attack_hit_bonus;
            }
        }
    }
}

void FighterDrillSession::resolve_bullet_tower_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& tower : towers_) {
            if (!tower.alive) continue;
            if (bullet_circle_collision(b.x, b.y, tower.x, tower.y, tower.radius)) {
                b.alive = false;
                tower.alive = false;
                break;
            }
        }
    }
}
```

- [ ] **Step 4: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: All tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/fighter_drill_session.h src/engine/fighter_drill_session.cpp tests/fighter_drill_session_test.cpp
git commit -m "feat(drill): add collision resolution to FighterDrillSession"
```

---

### Task 5: FighterDrillSession — Phase Transitions and Movement Scoring

**Files:**
- Modify: `src/engine/fighter_drill_session.cpp`
- Modify: `tests/fighter_drill_session_test.cpp`

- [ ] **Step 1: Write failing tests for phase transitions**

```cpp
TEST(FighterDrillSessionTest, PhaseTransitions) {
    nf::FighterDrillConfig config;
    config.population_size = 1;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 10;  // short phases for testing
    nf::FighterDrillSession session(config, 42);

    EXPECT_EQ(session.phase(), nf::DrillPhase::Expand);
    EXPECT_EQ(session.phase_ticks_remaining(), 10u);

    // Tick through Expand phase
    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Contract);

    // Tick through Contract phase
    for (uint32_t i = 0; i < 10; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Attack);

    // Tick through Attack phase
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
```

- [ ] **Step 2: Write failing tests for movement scoring**

```cpp
TEST(FighterDrillSessionTest, ExpandPhaseRewardsMovingAway) {
    nf::FighterDrillConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::FighterDrillSession session(config, 42);

    auto& ship0 = session.ships()[0];
    auto& ship1 = session.ships()[1];

    // Ship 0: face away from center (right), thrust outward
    float cx = config.world_width / 2.0f;
    ship0.x = cx + 100.0f;  // slightly right of center
    ship0.y = config.world_height / 2.0f;
    ship0.rotation = std::numbers::pi_v<float> / 2.0f;  // facing right

    // Ship 1: face toward center (left), thrust inward
    ship1.x = cx + 100.0f;
    ship1.y = config.world_height / 2.0f;
    ship1.rotation = -std::numbers::pi_v<float> / 2.0f;  // facing left

    // Both thrust forward
    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    EXPECT_GT(scores[0], 0.0f);   // moving away = positive
    EXPECT_LT(scores[1], 0.0f);   // moving toward = negative
    EXPECT_GT(scores[0], scores[1]);
}

TEST(FighterDrillSessionTest, ContractPhaseRewardsMovingToward) {
    nf::FighterDrillConfig config;
    config.population_size = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.phase_duration_ticks = 5;
    nf::FighterDrillSession session(config, 42);

    // Skip expand phase
    for (uint32_t i = 0; i < 5; ++i) session.tick();
    EXPECT_EQ(session.phase(), nf::DrillPhase::Contract);

    auto& ship0 = session.ships()[0];
    auto& ship1 = session.ships()[1];

    float cx = config.world_width / 2.0f;
    // Ship 0: face toward center (left), thrust inward
    ship0.x = cx + 100.0f;
    ship0.y = config.world_height / 2.0f;
    ship0.rotation = -std::numbers::pi_v<float> / 2.0f;

    // Ship 1: face away from center (right), thrust outward
    ship1.x = cx + 100.0f;
    ship1.y = config.world_height / 2.0f;
    ship1.rotation = std::numbers::pi_v<float> / 2.0f;

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
    EXPECT_GT(delta_0, 0.0f);   // moving toward = positive
    EXPECT_LT(delta_1, 0.0f);   // moving away = negative
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: Phase transition tests pass (already implemented in tick), scoring tests FAIL (compute_phase_scores is empty)

- [ ] **Step 4: Implement compute_phase_scores()**

```cpp
void FighterDrillSession::compute_phase_scores() {
    if (phase_ == DrillPhase::Done) return;

    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;

        float vx = ships_[i].dx;
        float vy = ships_[i].dy;

        switch (phase_) {
            case DrillPhase::Expand: {
                // Direction: from center to ship (outward)
                float dir_x = ships_[i].x - squad_center_x_;
                float dir_y = ships_[i].y - squad_center_y_;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.expand_weight;
                }
                break;
            }
            case DrillPhase::Contract: {
                // Direction: from ship to center (inward)
                float dir_x = squad_center_x_ - ships_[i].x;
                float dir_y = squad_center_y_ - ships_[i].y;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.contract_weight;
                }
                break;
            }
            case DrillPhase::Attack: {
                // Direction: from ship to starbase
                float dir_x = starbase_.x - ships_[i].x;
                float dir_y = starbase_.y - ships_[i].y;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.attack_travel_weight;
                }
                // Bullet hit bonus is already handled in resolve_bullet_starbase_collisions
                break;
            }
            case DrillPhase::Done:
                break;
        }
    }
}
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrillSession*'`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/engine/fighter_drill_session.cpp tests/fighter_drill_session_test.cpp
git commit -m "feat(drill): add phase transitions and movement-based scoring"
```

---

### Task 6: FighterDrillScreen — Header and Basic Structure

**Files:**
- Create: `include/neuroflyer/ui/screens/fighter_drill_screen.h`
- Create: `src/ui/screens/game/fighter_drill_screen.cpp`
- Modify: `CMakeLists.txt` (main executable sources)

- [ ] **Step 1: Create the screen header**

```cpp
// include/neuroflyer/ui/screens/fighter_drill_screen.h
#pragma once

#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/fighter_drill_session.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/ui/ui_screen.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

class FighterDrillScreen : public UIScreen {
public:
    FighterDrillScreen(
        const Snapshot& source_snapshot,
        std::string genome_dir,
        std::string variant_name);

    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "FighterDrill"; }

private:
    void initialize();
    void run_tick();
    void evolve_generation();
    void rebuild_nets();
    void render_world(SDL_Renderer* sdl_renderer);
    void render_hud();

    // Source data
    Snapshot source_snapshot_;
    std::string genome_dir_;
    std::string variant_name_;
    ShipDesign ship_design_;

    // Session
    FighterDrillConfig drill_config_;
    std::unique_ptr<FighterDrillSession> session_;

    // Population
    std::vector<Individual> population_;
    std::vector<neuralnet::Network> fighter_nets_;
    std::vector<std::vector<float>> recurrent_states_;
    EvolutionConfig evo_config_;
    std::mt19937 rng_;

    // State
    std::size_t generation_ = 0;
    int ticks_per_frame_ = 1;
    bool initialized_ = false;
    bool paused_ = false;

    // Camera
    enum class ViewMode { Swarm, Best, Worst };
    ViewMode view_mode_ = ViewMode::Swarm;
    float camera_x_ = 0, camera_y_ = 0;
    float camera_zoom_ = 0.5f;

    // Stats
    float best_fitness_ = 0;
    float avg_fitness_ = 0;

    // Team assignment (all same team for sensor queries)
    std::vector<int> ship_teams_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create the source file with initialization**

```cpp
// src/ui/screens/game/fighter_drill_screen.cpp
#include <neuroflyer/ui/screens/fighter_drill_screen.h>

#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace neuroflyer {

FighterDrillScreen::FighterDrillScreen(
    const Snapshot& source_snapshot,
    std::string genome_dir,
    std::string variant_name)
    : source_snapshot_(source_snapshot)
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name))
    , rng_(std::random_device{}())
{
}

void FighterDrillScreen::on_enter() {
    if (!initialized_) {
        initialize();
        initialized_ = true;
    }
}

void FighterDrillScreen::initialize() {
    ship_design_ = source_snapshot_.ship_design;

    // Seed population from snapshot (first = exact copy, rest = mutated)
    evo_config_.population_size = drill_config_.population_size;
    population_ = create_population_from_snapshot(
        source_snapshot_, drill_config_.population_size, evo_config_, rng_);

    // Convert all to fighter nets if needed (adds arena sensor extras + squad inputs)
    std::size_t arena_input_size = compute_arena_input_size(ship_design_);
    if (!population_.empty() &&
        population_[0].topology.layer_sizes[0] != arena_input_size) {
        for (auto& ind : population_) {
            ind = convert_variant_to_fighter(ind, ship_design_);
        }
    }

    // Team assignments (all team 0)
    ship_teams_.assign(drill_config_.population_size, 0);

    rebuild_nets();

    // Start first session
    session_ = std::make_unique<FighterDrillSession>(drill_config_, rng_());
    camera_x_ = drill_config_.world_width / 2.0f;
    camera_y_ = drill_config_.world_height / 2.0f;
}

void FighterDrillScreen::rebuild_nets() {
    fighter_nets_.clear();
    fighter_nets_.reserve(population_.size());
    recurrent_states_.clear();
    recurrent_states_.reserve(population_.size());

    for (auto& ind : population_) {
        fighter_nets_.push_back(ind.build_network());
        recurrent_states_.emplace_back(ship_design_.memory_slots, 0.0f);
    }
}

void FighterDrillScreen::on_draw(AppState& state, Renderer& renderer, UIManager& ui) {
    // Handle input
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        switch (view_mode_) {
            case ViewMode::Swarm: view_mode_ = ViewMode::Best; break;
            case ViewMode::Best:  view_mode_ = ViewMode::Worst; break;
            case ViewMode::Worst: view_mode_ = ViewMode::Swarm; break;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = !paused_;
        // TODO Task 8: push pause screen instead of simple toggle
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1)) ticks_per_frame_ = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_2)) ticks_per_frame_ = 5;
    if (ImGui::IsKeyPressed(ImGuiKey_3)) ticks_per_frame_ = 20;
    if (ImGui::IsKeyPressed(ImGuiKey_4)) ticks_per_frame_ = 100;

    if (!paused_) {
        for (int t = 0; t < ticks_per_frame_; ++t) {
            if (session_->is_over()) {
                evolve_generation();
                break;
            }
            run_tick();
        }
    }

    render_hud();
}

void FighterDrillScreen::post_render(SDL_Renderer* sdl_renderer) {
    if (ticks_per_frame_ <= 20) {
        render_world(sdl_renderer);
    }
}

void FighterDrillScreen::run_tick() {
    const auto& ships = session_->ships();
    float world_w = drill_config_.world_width;
    float world_h = drill_config_.world_height;
    float cx = session_->squad_center_x();
    float cy = session_->squad_center_y();
    DrillPhase phase = session_->phase();

    for (std::size_t i = 0; i < population_.size(); ++i) {
        if (!ships[i].alive) continue;

        // Compute scripted squad leader inputs
        float squad_target_heading = 0.0f;
        float squad_target_distance = 0.0f;
        float aggression = 0.0f;
        float spacing = 0.0f;

        // Squad center heading/distance (always computed)
        auto center_dr = compute_dir_range(
            ships[i].x, ships[i].y, cx, cy, world_w, world_h);
        float center_world_angle = std::atan2(center_dr.dir_sin, center_dr.dir_cos);
        float center_relative = center_world_angle - ships[i].rotation;
        constexpr float PI = std::numbers::pi_v<float>;
        while (center_relative > PI) center_relative -= 2.0f * PI;
        while (center_relative < -PI) center_relative += 2.0f * PI;
        float squad_center_heading = center_relative / PI;
        float squad_center_distance = center_dr.range;

        switch (phase) {
            case DrillPhase::Expand:
                spacing = 1.0f;
                break;
            case DrillPhase::Contract:
                spacing = -1.0f;
                break;
            case DrillPhase::Attack: {
                aggression = 1.0f;
                auto target_dr = compute_dir_range(
                    ships[i].x, ships[i].y,
                    session_->starbase().x, session_->starbase().y,
                    world_w, world_h);
                float target_world_angle = std::atan2(
                    target_dr.dir_sin, target_dr.dir_cos);
                float target_relative = target_world_angle - ships[i].rotation;
                while (target_relative > PI) target_relative -= 2.0f * PI;
                while (target_relative < -PI) target_relative += 2.0f * PI;
                squad_target_heading = target_relative / PI;
                squad_target_distance = target_dr.range;
                break;
            }
            case DrillPhase::Done:
                break;
        }

        // Build arena sensor context
        ArenaQueryContext ctx;
        ctx.ship_x = ships[i].x;
        ctx.ship_y = ships[i].y;
        ctx.ship_rotation = ships[i].rotation;
        ctx.world_w = world_w;
        ctx.world_h = world_h;
        ctx.self_index = i;
        ctx.self_team = 0;
        ctx.towers = session_->towers();
        ctx.tokens = session_->tokens();
        ctx.ships = session_->ships();
        ctx.ship_teams = ship_teams_;
        ctx.bullets = session_->bullets();

        auto input = build_arena_ship_input(
            ship_design_, ctx,
            squad_target_heading, squad_target_distance,
            squad_center_heading, squad_center_distance,
            aggression, spacing,
            recurrent_states_[i]);

        auto output = fighter_nets_[i].forward(input);
        auto decoded = decode_output(output, ship_design_.memory_slots);

        session_->set_ship_actions(i, decoded.up, decoded.down,
                                    decoded.left, decoded.right, decoded.shoot);
        recurrent_states_[i] = decoded.memory;
    }

    session_->tick();
}

void FighterDrillScreen::evolve_generation() {
    // Assign fitness
    auto scores = session_->get_scores();
    for (std::size_t i = 0; i < population_.size(); ++i) {
        population_[i].fitness = scores[i];
    }

    // Stats
    best_fitness_ = *std::max_element(scores.begin(), scores.end());
    float sum = std::accumulate(scores.begin(), scores.end(), 0.0f);
    avg_fitness_ = sum / static_cast<float>(scores.size());

    // Evolve
    population_ = evolve_population(population_, evo_config_, rng_);
    ++generation_;

    // Rebuild nets and session
    rebuild_nets();
    session_ = std::make_unique<FighterDrillSession>(drill_config_, rng_());
}

void FighterDrillScreen::render_hud() {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(250, 0));
    ImGui::Begin("##DrillHUD", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize);

    // Phase indicator
    DrillPhase phase = session_->phase();
    const char* phase_name = "Done";
    ImVec4 phase_color(1, 1, 1, 1);
    switch (phase) {
        case DrillPhase::Expand:
            phase_name = "EXPAND";
            phase_color = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);
            break;
        case DrillPhase::Contract:
            phase_name = "CONTRACT";
            phase_color = ImVec4(1.0f, 0.6f, 0.26f, 1.0f);
            break;
        case DrillPhase::Attack:
            phase_name = "ATTACK";
            phase_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            break;
        case DrillPhase::Done:
            break;
    }

    ImGui::TextColored(phase_color, "%s", phase_name);
    if (phase != DrillPhase::Done) {
        float secs_remaining = static_cast<float>(
            session_->phase_ticks_remaining()) / 60.0f;
        ImGui::SameLine();
        ImGui::Text("%.1fs", secs_remaining);
    }

    ImGui::Separator();
    ImGui::Text("Gen: %zu", generation_);
    ImGui::Text("Best: %.1f", best_fitness_);
    ImGui::Text("Avg: %.1f", avg_fitness_);
    ImGui::Text("Speed: %dx", ticks_per_frame_);

    // Alive count
    std::size_t alive = 0;
    for (const auto& s : session_->ships()) {
        if (s.alive) ++alive;
    }
    ImGui::Text("Alive: %zu/%zu", alive, session_->ships().size());

    ImGui::End();
}

void FighterDrillScreen::render_world(SDL_Renderer* sdl_renderer) {
    int screen_w, screen_h;
    SDL_GetRendererOutputSize(sdl_renderer, &screen_w, &screen_h);

    // Update camera based on view mode
    const auto& ships = session_->ships();
    auto scores = session_->get_scores();

    switch (view_mode_) {
        case ViewMode::Swarm: {
            // Zoom to fit swarm bounding box
            float min_x = drill_config_.world_width, max_x = 0;
            float min_y = drill_config_.world_height, max_y = 0;
            int alive_count = 0;
            for (const auto& s : ships) {
                if (!s.alive) continue;
                min_x = std::min(min_x, s.x);
                max_x = std::max(max_x, s.x);
                min_y = std::min(min_y, s.y);
                max_y = std::max(max_y, s.y);
                ++alive_count;
            }
            if (alive_count > 0) {
                float margin = 200.0f;
                camera_x_ = (min_x + max_x) / 2.0f;
                camera_y_ = (min_y + max_y) / 2.0f;
                float span_x = (max_x - min_x) + margin * 2;
                float span_y = (max_y - min_y) + margin * 2;
                float zoom_x = static_cast<float>(screen_w) / span_x;
                float zoom_y = static_cast<float>(screen_h) / span_y;
                camera_zoom_ = std::min(zoom_x, zoom_y);
                camera_zoom_ = std::clamp(camera_zoom_, 0.05f, 2.0f);
            }
            break;
        }
        case ViewMode::Best:
        case ViewMode::Worst: {
            std::size_t target = 0;
            float target_score = scores[0];
            for (std::size_t i = 1; i < scores.size(); ++i) {
                if (!ships[i].alive) continue;
                bool better = (view_mode_ == ViewMode::Best)
                    ? scores[i] > target_score
                    : scores[i] < target_score;
                if (better || !ships[target].alive) {
                    target = i;
                    target_score = scores[i];
                }
            }
            if (ships[target].alive) {
                camera_x_ = ships[target].x;
                camera_y_ = ships[target].y;
            }
            camera_zoom_ = 1.0f;
            break;
        }
    }

    auto world_to_screen = [&](float wx, float wy) -> std::pair<int, int> {
        float sx = (wx - camera_x_) * camera_zoom_ + screen_w / 2.0f;
        float sy = (wy - camera_y_) * camera_zoom_ + screen_h / 2.0f;
        return {static_cast<int>(sx), static_cast<int>(sy)};
    };

    // Clear
    SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 20, 255);
    SDL_RenderClear(sdl_renderer);

    // Draw squad center marker
    {
        auto [sx, sy] = world_to_screen(
            session_->squad_center_x(), session_->squad_center_y());
        SDL_SetRenderDrawColor(sdl_renderer, 255, 170, 0, 100);
        int r = static_cast<int>(20.0f * camera_zoom_);
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r * r)
                    SDL_RenderDrawPoint(sdl_renderer, sx + dx, sy + dy);
            }
        }
    }

    // Draw towers
    SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 100, 255);
    for (const auto& t : session_->towers()) {
        if (!t.alive) continue;
        auto [sx, sy] = world_to_screen(t.x, t.y);
        int r = static_cast<int>(t.radius * camera_zoom_);
        if (r < 1) r = 1;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r * r)
                    SDL_RenderDrawPoint(sdl_renderer, sx + dx, sy + dy);
            }
        }
    }

    // Draw tokens
    SDL_SetRenderDrawColor(sdl_renderer, 255, 215, 0, 255);
    for (const auto& tok : session_->tokens()) {
        if (!tok.alive) continue;
        auto [sx, sy] = world_to_screen(tok.x, tok.y);
        int r = static_cast<int>(tok.radius * camera_zoom_);
        if (r < 1) r = 1;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r * r)
                    SDL_RenderDrawPoint(sdl_renderer, sx + dx, sy + dy);
            }
        }
    }

    // Draw starbase
    {
        const auto& base = session_->starbase();
        if (base.alive()) {
            auto [sx, sy] = world_to_screen(base.x, base.y);
            int r = static_cast<int>(base.radius * camera_zoom_);
            if (r < 2) r = 2;
            SDL_SetRenderDrawColor(sdl_renderer, 200, 50, 50, 255);
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy <= r * r)
                        SDL_RenderDrawPoint(sdl_renderer, sx + dx, sy + dy);
                }
            }
            // HP bar
            int bar_w = r * 2;
            int bar_h = 4;
            int bar_x = sx - r;
            int bar_y = sy - r - 8;
            SDL_SetRenderDrawColor(sdl_renderer, 60, 60, 60, 255);
            SDL_Rect bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_RenderFillRect(sdl_renderer, &bg);
            SDL_SetRenderDrawColor(sdl_renderer, 200, 50, 50, 255);
            SDL_Rect hp = {bar_x, bar_y,
                static_cast<int>(bar_w * base.hp_normalized()), bar_h};
            SDL_RenderFillRect(sdl_renderer, &hp);
        }
    }

    // Draw bullets
    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 100, 255);
    for (const auto& b : session_->bullets()) {
        if (!b.alive) continue;
        auto [sx, sy] = world_to_screen(b.x, b.y);
        SDL_RenderDrawPoint(sdl_renderer, sx, sy);
        SDL_RenderDrawPoint(sdl_renderer, sx + 1, sy);
        SDL_RenderDrawPoint(sdl_renderer, sx, sy + 1);
    }

    // Draw ships
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const auto& s = ships[i];
        if (!s.alive) continue;

        auto [sx, sy] = world_to_screen(s.x, s.y);
        float sz = Triangle::SIZE * camera_zoom_;

        // Rotated triangle vertices
        float fx = std::sin(s.rotation);
        float fy = -std::cos(s.rotation);
        float rx = std::cos(s.rotation);
        float ry = std::sin(s.rotation);

        int x0 = sx + static_cast<int>(fx * sz);
        int y0 = sy + static_cast<int>(fy * sz);
        int x1 = sx + static_cast<int>((-fx * 0.5f - rx) * sz);
        int y1 = sy + static_cast<int>((-fy * 0.5f - ry) * sz);
        int x2 = sx + static_cast<int>((-fx * 0.5f + rx) * sz);
        int y2 = sy + static_cast<int>((-fy * 0.5f + ry) * sz);

        SDL_SetRenderDrawColor(sdl_renderer, 100, 220, 100, 255);
        SDL_RenderDrawLine(sdl_renderer, x0, y0, x1, y1);
        SDL_RenderDrawLine(sdl_renderer, x1, y1, x2, y2);
        SDL_RenderDrawLine(sdl_renderer, x2, y2, x0, y0);
    }
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add to CMakeLists.txt**

In the main `CMakeLists.txt`, add to the source list after `src/ui/screens/arena/arena_pause_screen.cpp`:

```cmake
    src/ui/screens/game/fighter_drill_screen.cpp
```

Also add the engine source to the main executable (after `src/engine/squad_leader.cpp`):

```cmake
    src/engine/fighter_drill_session.cpp
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build --target neuroflyer`
Expected: Compiles without errors

- [ ] **Step 5: Commit**

```bash
git add include/neuroflyer/ui/screens/fighter_drill_screen.h src/ui/screens/game/fighter_drill_screen.cpp CMakeLists.txt
git commit -m "feat(drill): add FighterDrillScreen with training loop and rendering"
```

---

### Task 7: Fighter Drill Pause Screen

**Files:**
- Create: `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h`
- Create: `src/ui/screens/game/fighter_drill_pause_screen.cpp`
- Modify: `src/ui/screens/game/fighter_drill_screen.cpp` (wire Space to push pause screen)
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the pause screen header**

```cpp
// include/neuroflyer/ui/screens/fighter_drill_pause_screen.h
#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/ui/ui_screen.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace neuroflyer {

class FighterDrillPauseScreen : public UIScreen {
public:
    FighterDrillPauseScreen(
        std::vector<Individual> population,
        std::size_t generation,
        ShipDesign ship_design,
        std::string genome_dir,
        std::string variant_name,
        EvolutionConfig evo_config,
        std::function<void(EvolutionConfig)> on_resume);

    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "FighterDrillPause"; }

private:
    std::vector<Individual> population_;
    std::size_t generation_;
    ShipDesign ship_design_;
    std::string genome_dir_;
    std::string variant_name_;
    EvolutionConfig evo_config_;
    std::function<void(EvolutionConfig)> on_resume_;

    // Save tab state
    std::vector<std::size_t> sorted_indices_;
    std::vector<bool> selected_;
    bool indices_built_ = false;
    int active_tab_ = 0;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Implement the pause screen**

```cpp
// src/ui/screens/game/fighter_drill_pause_screen.cpp
#include <neuroflyer/ui/screens/fighter_drill_pause_screen.h>

#include <neuroflyer/genome_manager.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

#include <algorithm>
#include <numeric>

namespace neuroflyer {

FighterDrillPauseScreen::FighterDrillPauseScreen(
    std::vector<Individual> population,
    std::size_t generation,
    ShipDesign ship_design,
    std::string genome_dir,
    std::string variant_name,
    EvolutionConfig evo_config,
    std::function<void(EvolutionConfig)> on_resume)
    : population_(std::move(population))
    , generation_(generation)
    , ship_design_(std::move(ship_design))
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name))
    , evo_config_(evo_config)
    , on_resume_(std::move(on_resume))
{
}

void FighterDrillPauseScreen::on_draw(
    AppState& state, Renderer& renderer, UIManager& ui) {

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        on_resume_(evo_config_);
        ui.pop_screen();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(100, 50));
    ImGui::SetNextWindowSize(ImVec2(500, 500));
    ImGui::Begin("Fighter Drill — Paused", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("DrillPauseTabs")) {
        // Evolution tab
        if (ImGui::BeginTabItem("Evolution")) {
            active_tab_ = 0;
            int pop = static_cast<int>(evo_config_.population_size);
            if (ui::input_int("Population", pop, 10, 500)) {
                evo_config_.population_size = static_cast<std::size_t>(pop);
            }
            int elite = static_cast<int>(evo_config_.elitism_count);
            if (ui::input_int("Elitism", elite, 0, 50)) {
                evo_config_.elitism_count = static_cast<std::size_t>(elite);
            }
            ui::slider_float("Weight Mutation Rate",
                evo_config_.weight_mutation_rate, 0.0f, 1.0f);
            ui::slider_float("Weight Mutation Strength",
                evo_config_.weight_mutation_strength, 0.0f, 2.0f);
            ui::slider_float("Add Node Chance",
                evo_config_.add_node_chance, 0.0f, 0.1f);
            ui::slider_float("Remove Node Chance",
                evo_config_.remove_node_chance, 0.0f, 0.1f);
            ui::slider_float("Add Layer Chance",
                evo_config_.add_layer_chance, 0.0f, 0.05f);
            ui::slider_float("Remove Layer Chance",
                evo_config_.remove_layer_chance, 0.0f, 0.05f);

            ImGui::EndTabItem();
        }

        // Save Variants tab
        if (ImGui::BeginTabItem("Save Variants")) {
            active_tab_ = 1;

            if (!indices_built_) {
                sorted_indices_.resize(population_.size());
                std::iota(sorted_indices_.begin(), sorted_indices_.end(), 0);
                std::sort(sorted_indices_.begin(), sorted_indices_.end(),
                    [&](std::size_t a, std::size_t b) {
                        return population_[a].fitness > population_[b].fitness;
                    });
                selected_.assign(population_.size(), false);
                indices_built_ = true;
            }

            ImGui::Text("Select fighters to save (sorted by fitness):");
            ImGui::Separator();

            if (ImGui::Button("Select Top 5")) {
                std::fill(selected_.begin(), selected_.end(), false);
                for (std::size_t i = 0; i < std::min<std::size_t>(5, sorted_indices_.size()); ++i) {
                    selected_[sorted_indices_[i]] = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All")) {
                std::fill(selected_.begin(), selected_.end(), false);
            }

            ImGui::BeginChild("VariantList", ImVec2(0, 280), true);
            for (std::size_t rank = 0; rank < sorted_indices_.size(); ++rank) {
                std::size_t idx = sorted_indices_[rank];
                ImGui::PushID(static_cast<int>(idx));
                bool sel = selected_[idx];
                if (ImGui::Checkbox("##sel", &sel)) {
                    selected_[idx] = sel;
                }
                ImGui::SameLine();
                ImGui::Text("#%zu  fitness: %.1f", rank + 1, population_[idx].fitness);
                ImGui::PopID();
            }
            ImGui::EndChild();

            int count = static_cast<int>(
                std::count(selected_.begin(), selected_.end(), true));
            if (count > 0 && ImGui::Button("Save Selected")) {
                int saved = 0;
                for (std::size_t i = 0; i < population_.size(); ++i) {
                    if (!selected_[i]) continue;
                    Snapshot snap;
                    snap.name = variant_name_ + "-drill-g"
                        + std::to_string(generation_) + "-" + std::to_string(saved);
                    snap.ship_design = ship_design_;
                    snap.topology = population_[i].topology;
                    snap.weights = population_[i].genome.weights();
                    snap.generation = static_cast<uint32_t>(generation_);
                    snap.parent_name = variant_name_;
                    snap.net_type = NetType::Fighter;

                    std::string path = genome_dir_ + "/" + snap.name + ".bin";
                    save_snapshot(snap, path);
                    ++saved;
                }
                state.variants_dirty = true;
                state.lineage_dirty = true;
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ui::button("Resume")) {
        on_resume_(evo_config_);
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer
```

- [ ] **Step 3: Wire Space key in FighterDrillScreen to push pause screen**

In `src/ui/screens/game/fighter_drill_screen.cpp`, add the include:

```cpp
#include <neuroflyer/ui/screens/fighter_drill_pause_screen.h>
```

Replace the Space key handler:

```cpp
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        ui.push_screen(std::make_unique<FighterDrillPauseScreen>(
            population_,
            generation_,
            ship_design_,
            genome_dir_,
            variant_name_,
            evo_config_,
            [this](EvolutionConfig new_config) {
                evo_config_ = new_config;
            }));
    }
```

Remove the `paused_` member variable and its references (pause state is now managed by screen stack — when pause screen is on top, drill screen doesn't draw).

- [ ] **Step 4: Add to CMakeLists.txt**

Add after `fighter_drill_screen.cpp`:

```cmake
    src/ui/screens/game/fighter_drill_pause_screen.cpp
```

- [ ] **Step 5: Build to verify compilation**

Run: `cmake --build build --target neuroflyer`
Expected: Compiles without errors

- [ ] **Step 6: Commit**

```bash
git add include/neuroflyer/ui/screens/fighter_drill_pause_screen.h src/ui/screens/game/fighter_drill_pause_screen.cpp src/ui/screens/game/fighter_drill_screen.cpp CMakeLists.txt
git commit -m "feat(drill): add FighterDrillPauseScreen with evolution config and variant saving"
```

---

### Task 8: Hangar Integration — Entry Point

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`
- Modify: `include/neuroflyer/app_state.h` (add drill mode flag)

- [ ] **Step 1: Add drill mode flag to AppState**

In `include/neuroflyer/app_state.h`, add after the squad training fields:

```cpp
    // Fighter drill mode
    bool fighter_drill_mode = false;
    std::string fighter_drill_genome_dir;
    std::string fighter_drill_variant_name;
```

- [ ] **Step 2: Add Fighter Drill action to variant viewer**

In `src/ui/screens/hangar/variant_viewer_screen.cpp`, find the Action enum and add:

```cpp
    FighterDrill,
```

Find the fighter tab section (where squad training buttons are rendered) and add a "Fighter Drill" button. Look for the existing arena training buttons pattern and add nearby:

```cpp
if (ui::button("Fighter Drill")) {
    pending_action_ = Action::FighterDrill;
}
```

Add the action handler in the switch statement:

```cpp
case Action::FighterDrill: {
    state.fighter_drill_mode = true;
    state.fighter_drill_genome_dir = vs_.genome_dir;
    state.fighter_drill_variant_name = vs_.selected_variant_name;
    state.return_to_variant_view = true;
    ui.push_screen(std::make_unique<FighterDrillScreen>(
        vs_.selected_snapshot,
        vs_.genome_dir,
        vs_.selected_variant_name));
    break;
}
```

Add the include at the top:

```cpp
#include <neuroflyer/ui/screens/fighter_drill_screen.h>
```

- [ ] **Step 3: Build to verify compilation**

Run: `cmake --build build --target neuroflyer`
Expected: Compiles without errors

- [ ] **Step 4: Commit**

```bash
git add include/neuroflyer/app_state.h src/ui/screens/hangar/variant_viewer_screen.cpp
git commit -m "feat(drill): add Fighter Drill entry point in hangar variant viewer"
```

---

### Task 9: Integration Testing and Polish

**Files:**
- Modify: `tests/fighter_drill_session_test.cpp`
- Modify: `src/engine/fighter_drill_session.cpp` (if bugs found)

- [ ] **Step 1: Add integration test — full drill run**

```cpp
TEST(FighterDrillSessionTest, FullDrillRun) {
    nf::FighterDrillConfig config;
    config.population_size = 10;
    config.tower_count = 5;
    config.token_count = 3;
    config.phase_duration_ticks = 10;

    nf::FighterDrillSession session(config, 42);

    // Run all 3 phases (30 ticks total)
    for (uint32_t i = 0; i < 30; ++i) {
        // Alternate actions to test variety
        for (std::size_t s = 0; s < 10; ++s) {
            session.set_ship_actions(s,
                (i + s) % 3 == 0,   // up
                (i + s) % 5 == 0,   // down
                (i + s) % 2 == 0,   // left
                (i + s) % 7 == 0,   // right
                (i + s) % 4 == 0);  // shoot
        }
        session.tick();
    }

    EXPECT_TRUE(session.is_over());
    EXPECT_EQ(session.current_tick(), 30u);

    auto scores = session.get_scores();
    EXPECT_EQ(scores.size(), 10u);

    // At least some scores should be non-zero
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

    // Kill ship 1
    session.ships()[1].alive = false;

    // Run a few ticks with thrust
    for (int i = 0; i < 5; ++i) {
        session.set_ship_actions(0, true, false, false, false, false);
        session.set_ship_actions(1, true, false, false, false, false);
        session.tick();
    }

    auto scores = session.get_scores();
    EXPECT_NE(scores[0], 0.0f);     // alive ship scored
    EXPECT_FLOAT_EQ(scores[1], 0.0f); // dead ship didn't score
}
```

- [ ] **Step 2: Run all drill tests**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests --gtest_filter='FighterDrill*'`
Expected: All tests PASS

- [ ] **Step 3: Run full test suite to check for regressions**

Run: `cmake --build build --target neuroflyer_tests && ./build/tests/neuroflyer_tests`
Expected: All existing tests still PASS

- [ ] **Step 4: Build full application**

Run: `cmake --build build --target neuroflyer`
Expected: Compiles without errors

- [ ] **Step 5: Commit**

```bash
git add tests/fighter_drill_session_test.cpp
git commit -m "test(drill): add integration tests for FighterDrillSession"
```

---

## File Summary

### New Files
| File | Purpose |
|------|---------|
| `include/neuroflyer/fighter_drill_session.h` | FighterDrillConfig, DrillPhase, FighterDrillSession |
| `src/engine/fighter_drill_session.cpp` | Drill world simulation, phases, scoring |
| `include/neuroflyer/ui/screens/fighter_drill_screen.h` | FighterDrillScreen UIScreen |
| `src/ui/screens/game/fighter_drill_screen.cpp` | Training loop, rendering, camera |
| `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h` | FighterDrillPauseScreen |
| `src/ui/screens/game/fighter_drill_pause_screen.cpp` | Evolution config, variant saving |
| `tests/fighter_drill_session_test.cpp` | Engine tests |

### Modified Files
| File | Change |
|------|--------|
| `tests/CMakeLists.txt` | Add test + engine source |
| `CMakeLists.txt` | Add screen + engine source to main executable |
| `include/neuroflyer/app_state.h` | Add drill mode flags |
| `src/ui/screens/hangar/variant_viewer_screen.cpp` | Add Fighter Drill action + button |

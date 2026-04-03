# Friendly Fire Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a configurable friendly fire toggle (default off) so bullets pass through teammate ships, eliminating fratricide in skirmish and arena modes.

**Architecture:** Add `bool friendly_fire = false` to `ArenaConfig` and `SkirmishConfig`. Guard the existing bullet-ship collision loop in `ArenaSession::resolve_bullet_ship_collisions()` with a same-team skip when friendly fire is disabled. Propagate the flag through `SkirmishConfig::to_arena_config()`. Add a UI checkbox to `SkirmishConfigScreen`.

**Tech Stack:** C++20, Google Test, ImGui (via `ui::` widgets)

---

### Task 1: Add `friendly_fire` to ArenaConfig

**Files:**
- Modify: `include/neuroflyer/arena_config.h:39` (after `base_bullet_damage`)

- [ ] **Step 1: Add the field**

In `include/neuroflyer/arena_config.h`, add this line after `float base_bullet_damage = 10.0f;` (line 39):

```cpp
    bool friendly_fire = false;  // when false, bullets pass through teammate ships
```

- [ ] **Step 2: Verify the project still compiles**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/arena_config.h
git commit -m "feat(arena): add friendly_fire flag to ArenaConfig (default false)"
```

---

### Task 2: Guard bullet-ship collisions with friendly fire check

**Files:**
- Modify: `src/engine/arena_session.cpp:234-264` (`resolve_bullet_ship_collisions`)
- Test: `tests/arena_session_test.cpp`

- [ ] **Step 1: Write failing test — friendly fire OFF skips same-team collision**

Add this test at the end of `tests/arena_session_test.cpp`:

```cpp
TEST(ArenaSessionTest, FriendlyFireOffBulletsPassThroughTeammates) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.friendly_fire = false;  // bullets should pass through teammates
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build && cd build && ctest --test-dir tests -R FriendlyFireOffBulletsPassThroughTeammates -V 2>&1 | tail -10`
Expected: FAIL — ship 1 is killed because the guard doesn't exist yet.

- [ ] **Step 3: Write failing test — friendly fire ON still kills teammates**

Add this test after the previous one:

```cpp
TEST(ArenaSessionTest, FriendlyFireOnBulletsKillTeammates) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.friendly_fire = true;  // bullets SHOULD kill teammates
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
```

- [ ] **Step 4: Run the test to verify it passes (existing behavior)**

Run: `cmake --build build && cd build && ctest --test-dir tests -R FriendlyFireOnBulletsKillTeammates -V 2>&1 | tail -10`
Expected: PASS — this is the existing behavior.

- [ ] **Step 5: Write failing test — friendly fire OFF still allows cross-team kills**

Add this test:

```cpp
TEST(ArenaSessionTest, FriendlyFireOffEnemyBulletsStillKill) {
    nf::ArenaConfig config;
    config.num_teams = 2;
    config.num_squads = 1;
    config.fighters_per_squad = 2;
    config.tower_count = 0;
    config.token_count = 0;
    config.time_limit_ticks = 1000;
    config.world_width = 1000.0f;
    config.world_height = 1000.0f;
    config.friendly_fire = false;
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
```

- [ ] **Step 6: Implement the friendly fire guard**

In `src/engine/arena_session.cpp`, in `resolve_bullet_ship_collisions()`, add the team check after the self-hit skip (line 240). The function should become:

```cpp
void ArenaSession::resolve_bullet_ship_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (!ships_[i].alive) continue;
            // Skip self-hits
            if (b.owner_index == static_cast<int>(i)) continue;
            // Skip friendly fire when disabled
            if (!config_.friendly_fire && b.owner_index >= 0) {
                auto killer = static_cast<std::size_t>(b.owner_index);
                if (killer < team_assignments_.size() &&
                    team_assignments_[killer] == team_assignments_[i]) {
                    continue;  // bullet passes through teammate
                }
            }
            // Check rotation-aware vertex-based collision plus center proximity.
            bool hit = bullet_triangle_collision_rotated(b.x, b.y, ships_[i]);
            if (!hit) {
                float dx = b.x - ships_[i].x;
                float dy = b.y - ships_[i].y;
                hit = (dx * dx + dy * dy) < (Triangle::SIZE * Triangle::SIZE);
            }
            if (hit) {
                ships_[i].alive = false;
                b.alive = false;
                // Track kill: compare killer's team to victim's team
                auto killer = static_cast<std::size_t>(b.owner_index);
                if (killer < team_assignments_.size()) {
                    if (team_assignments_[killer] == team_assignments_[i]) {
                        ally_kills_[killer]++;
                    } else {
                        enemy_kills_[killer]++;
                    }
                }
                break;
            }
        }
    }
}
```

- [ ] **Step 7: Run all three new tests plus the existing AllyKill test**

Run: `cmake --build build && cd build && ctest --test-dir tests -R "FriendlyFire|AllyKill" -V 2>&1 | tail -15`
Expected: All 4 tests PASS.

- [ ] **Step 8: Run the full test suite**

Run: `cmake --build build && cd build && ctest --test-dir tests -V 2>&1 | tail -10`
Expected: All tests pass. No regressions.

- [ ] **Step 9: Commit**

```bash
git add src/engine/arena_session.cpp tests/arena_session_test.cpp
git commit -m "feat(arena): skip same-team bullet collisions when friendly_fire is off"
```

---

### Task 3: Add `friendly_fire` to SkirmishConfig and propagate

**Files:**
- Modify: `include/neuroflyer/skirmish.h:15-67`

- [ ] **Step 1: Add the field and propagate in `to_arena_config()`**

In `include/neuroflyer/skirmish.h`, add this line after `bool wrap_ew = true;` (line 31):

```cpp
    bool friendly_fire = false;  // when false, bullets pass through teammate ships
```

Then in `to_arena_config()`, add this line after `ac.wrap_ew = wrap_ew;` (line 62):

```cpp
        ac.friendly_fire = friendly_fire;
```

- [ ] **Step 2: Verify the project compiles**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add include/neuroflyer/skirmish.h
git commit -m "feat(skirmish): add friendly_fire to SkirmishConfig, propagate to ArenaConfig"
```

---

### Task 4: Add UI checkbox to SkirmishConfigScreen

**Files:**
- Modify: `src/ui/screens/game/skirmish_config_screen.cpp:92-99` (Physics section)

- [ ] **Step 1: Add the checkbox**

In `src/ui/screens/game/skirmish_config_screen.cpp`, in the Physics section, add this line after the `ui::checkbox("Wrap E/W", &config_.wrap_ew);` line (line 98):

```cpp
    ui::checkbox("Friendly Fire", &config_.friendly_fire);
```

- [ ] **Step 2: Verify the project compiles**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/ui/screens/game/skirmish_config_screen.cpp
git commit -m "feat(ui): add Friendly Fire checkbox to SkirmishConfigScreen"
```

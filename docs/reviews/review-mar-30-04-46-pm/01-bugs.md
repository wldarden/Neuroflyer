# Bug Hunt

> **Scope:** Arena mode (ArenaSession, arena_sensor, team_evolution), Fighter Drill (FighterDrillSession, FighterDrillScreen, FighterDrillPauseScreen), collision helpers, evolution engine, snapshot I/O, game session, sensor engine, net viewer, variant net render, variant viewer screen. Focus on files changed in last ~15 commits.
> **Findings:** 1 critical, 3 high, 4 medium, 3 low

---

## [CRITICAL] Potential out-of-bounds access in ArenaSession::resolve_bullet_ship_collisions when owner_index is invalid

**ID:** `BUG-001`

**Location:** `src/engine/arena_session.cpp:252-256`

**Description:** After a bullet kills a ship, the code accesses `team_assignments_[killer]` where `killer = static_cast<std::size_t>(b.owner_index)`. If `b.owner_index` is negative (the default is -1 in `Bullet`), the cast to `std::size_t` wraps to a very large number, causing out-of-bounds access on `team_assignments_`, `ally_kills_`, and `enemy_kills_`. While `spawn_bullets_from_ships` always sets `owner_index` to the ship index (which is valid), `add_bullet()` is a public method that accepts any `Bullet`, and the default `Bullet::owner_index` is -1. An externally-added bullet with `owner_index = -1` would trigger undefined behavior.

**Impact:** Undefined behavior (out-of-bounds vector access) if a bullet with invalid `owner_index` hits a ship. Currently `add_bullet` is called from arena game screen logic, but the public API has no guard.

**Fix plan:** Add a guard before the kill tracking block: `if (killer < team_assignments_.size())` before accessing `team_assignments_[killer]`, `ally_kills_[killer]`, and `enemy_kills_[killer]`.

---

## [HIGH] FighterDrillScreen population/session size mismatch after pause screen edit

**ID:** `BUG-002`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:153-175, 362-404`

**Description:** The population is created with `drill_config_.population_size` individuals (line 153-156), and the FighterDrillSession is created with the same config (line 175). After evolution (`evolve_generation`, line 386), the new population size comes from `evo_config_.population_size` (passed through `evolve_population`). However, the new `FighterDrillSession` on line 404 is still created with `drill_config_`, which retains the original population size. If the user changes population size via the pause screen (which edits `evo_config_.population_size` on line 94-98 of the pause screen), the new population will have a different count than the session's ship count. In `run_tick()`, line 349 does `nets_[i].forward(input)` where `i` iterates over `session_->ships().size()`, which could exceed `nets_.size()`.

**Impact:** Out-of-bounds vector access and crash if the user changes population size in the pause screen and then resumes training.

**Fix plan:** In `evolve_generation()`, sync `drill_config_.population_size = population_.size()` before creating the new session on line 404.

---

## [HIGH] FighterDrillScreen drill_ship_teams_ never resized after evolution

**ID:** `BUG-003`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:171, 334, 362-404`

**Description:** `drill_ship_teams_` is initialized once in `initialize()` (line 171: `drill_ship_teams_.assign(population_.size(), 0)`) and never updated afterward. After evolution, if the population size changes (due to BUG-002's scenario), `drill_ship_teams_` retains the old size. When passed to `ctx.ship_teams = drill_ship_teams_` (line 334), the span size no longer matches `ctx.ships.size()`. In `query_arena_raycast` (arena_sensor.cpp:77), the guard `i < ctx.ship_teams.size()` will cause ships beyond the old team array size to be treated as enemies (the condition fails, `is_friend` defaults to false). Even without a population size change, if the session ships vector and `drill_ship_teams_` are out of sync for any reason, sensor readings become incorrect.

**Impact:** Ships may be misclassified as enemies/friends in sensor queries, producing incorrect neural net inputs and degrading training quality. If population size shrinks, excess team entries are harmless; if it grows, new ships have no team entry.

**Fix plan:** Resize `drill_ship_teams_` in `evolve_generation()` alongside `recurrent_states_`: `drill_ship_teams_.assign(population_.size(), 0);`

---

## [HIGH] Division by zero risk in arena ray_circle_hit when sensor.range is zero

**ID:** `BUG-004`

**Location:** `src/engine/arena_sensor.cpp:16-33`

**Description:** The function `ray_circle_hit` normalizes the hit distance by dividing by `range` (line 32: `return std::min(dist / range, 1.0f)`). The `range` parameter comes directly from `sensor.range` on the SensorDef. While sensor genes have `min_val = 30.0f` in `build_genome_skeleton`, the `Individual::random` and `convert_variant_to_fighter` paths create sensors from the ShipDesign directly without enforcing this minimum. A degenerate ShipDesign with `sensor.range = 0.0f` would cause division by zero, producing `inf` which propagates as NaN through the neural net forward pass and corrupts all ship actions.

**Impact:** NaN propagation through the neural net causes all ship actions to become nonsensical. Silent data corruption bug rather than a crash, because IEEE 754 `inf` and `NaN` are valid float values.

**Fix plan:** Add a guard at the top of `ray_circle_hit`: `if (range < 1e-6f) return 1.0f;`. Also consider clamping `sensor.range` in `query_arena_sensor` before passing it through.

---

## [MEDIUM] Arena sensor occulus distance normalization inconsistent with raycast

**ID:** `BUG-005`

**Location:** `src/engine/arena_sensor.cpp:121-137`

**Description:** In `query_arena_occulus`, the distance normalization computes `edge_dist / max_reach` where `max_reach = SHIP_GAP + major_radius * 2.0f` (line 121). `edge_dist` is the distance from the ship to the object edge. In `query_arena_raycast`, the distance is `t * sqrt(a) / range` where `range = sensor.range`. For the same physical distance to a target, the two sensor types return different normalized values because `max_reach` and `sensor.range` are different quantities. Specifically, `max_reach` depends on `SHIP_GAP` (15.0f) and `major_radius` (range * 0.5f), so `max_reach = 15 + range`. The raycast normalizes by `range` alone. For `range = 200`, occulus max_reach is 215 while raycast max_reach is 200 -- a 7.5% discrepancy.

**Impact:** Mixed-sensor ShipDesigns receive inconsistent distance signals. The neural net can compensate, but it reduces training efficiency.

**Fix plan:** Normalize both sensor types by `sensor.range` for consistency, or document the difference.

---

## [MEDIUM] FighterDrillSession::update_bullets does not destroy out-of-bounds bullets

**ID:** `BUG-006`

**Location:** `src/engine/fighter_drill_session.cpp:176-181`

**Description:** Unlike `ArenaSession::update_bullets` (lines 221-232) which destroys bullets that leave the world boundary, `FighterDrillSession::update_bullets` only calls `b.update_directional()`. Bullets that exit the world rectangle continue traveling until they exhaust `max_range`. In a wrapping world, ships wrap but bullets do not, so bullets can accumulate outside the playable area. With default `bullet_max_range = 1000` and a 4000x4000 world, bullets can travel up to 1000 units past each boundary.

**Impact:** Performance: wasted collision checks against out-of-bounds bullets. Functionally harmless since no entities exist outside the world.

**Fix plan:** Add boundary destruction in `update_bullets`, matching ArenaSession's approach.

---

## [MEDIUM] ArenaSession bullets die at world boundary instead of wrapping

**ID:** `BUG-007`

**Location:** `src/engine/arena_session.cpp:221-232`

**Description:** `ArenaSession::update_bullets` destroys bullets at world boundaries even though `config_.wrap_ew` and `config_.wrap_ns` are both true. Ships wrap around the world, but bullets are destroyed at the edge. A ship near the world boundary shooting outward will have its bullet immediately destroyed, while the wrap target is unreachable by fire. This creates a dead zone along all four world boundaries where ships cannot engage targets across the seam.

**Impact:** Gameplay asymmetry: ships near world boundaries cannot attack across the wrap seam, creating safe zones and penalizing edge-positioned ships during evolution.

**Fix plan:** Either wrap bullets the same way as ships when wrapping is enabled, or document this as intentional. If wrapping bullets, collision detection must also handle cross-boundary checks.

---

## [MEDIUM] Net viewer creates a full Network copy via shared_ptr every frame

**ID:** `BUG-008`

**Location:** `src/ui/views/net_viewer_view.cpp:90`

**Description:** `draw_net_viewer_view` executes `std::make_shared<neuralnet::Network>(*state.network)` every frame, deep-copying the entire Network object including all weights and topology. This exists to guarantee ownership through the two-phase render (draw phase stores config, flush phase renders). The copy is released each frame in `flush_net_viewer_view`.

**Impact:** At 60fps, this is 60 heap allocations and full network copies per second. For networks with thousands of weights, this creates measurable allocator pressure and GC churn.

**Fix plan:** Cache the shared_ptr and only recreate it when `state.network` changes (compare pointer identity). Add a `last_network_ptr` field to `NetViewerViewState` to track this.

---

## [LOW] compute_dir_range does not account for world wrapping

**ID:** `BUG-009`

**Location:** `src/engine/arena_sensor.cpp:198-216`

**Description:** `compute_dir_range` computes straight-line direction and distance between two points without considering world wrapping. In a toroidal world, the shortest path between two points near opposite edges goes through the wrap boundary. For example, with `world_width = 81920` and points at x=100 and x=81820, the Euclidean distance is 81720 but the wrapped distance is only 200. This function feeds squad leader fighter inputs (heading and distance to targets and squad center).

**Impact:** Squad leader inputs provide misleading heading and distance when targets are closer through the wrap boundary. Training quality is affected. The large default world (81920x51200) makes this relatively rare for most ship positions.

**Fix plan:** Add wrap-aware distance computation that considers both direct and wrapped paths, selecting the shorter one.

---

## [LOW] Arena sensor raycast does not detect entities across world wrap boundary

**ID:** `BUG-010`

**Location:** `src/engine/arena_sensor.cpp:36-93, 98-181`

**Description:** Both `query_arena_raycast` and `query_arena_occulus` check entity positions at their absolute world coordinates. In a wrapping world, an entity near `x = 0` is adjacent to a ship near `x = world_width`, but the sensor will not detect it because it doesn't consider wrapped ghost positions. Sensor range (typically 300) is tiny compared to the default world size (81920), so this only affects ships within ~300 units of a boundary.

**Impact:** Blind spots at world boundaries. Negligible with the default large world, but significant if the world is small (e.g., FighterDrill uses 4000x4000, where sensor range is 7.5% of the world width).

**Fix plan:** For sensors near world edges, additionally test against wrapped ghost positions. Or document this as acceptable for large worlds and fix only for drill-sized worlds.

---

## [LOW] FighterDrillPauseScreen copies entire population by value

**ID:** `BUG-011`

**Location:** `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h:16-23`, `src/ui/screens/game/fighter_drill_screen.cpp:237-239`

**Description:** `FighterDrillPauseScreen` takes `std::vector<Individual> population` by value in its constructor, deep-copying all 200 individuals (each containing a `StructuredGenome` with potentially hundreds of float values). This copy happens every time the user presses Space to pause.

**Impact:** Noticeable frame hitch when opening the pause screen during drill training with large populations.

**Fix plan:** Accept `const std::vector<Individual>&` or `std::span<const Individual>` instead. The pause screen only reads the population for display and saving; it does not modify it. Saving creates snapshots from the const data, so no mutation is needed.

---

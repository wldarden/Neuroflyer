# Dead Code

> **Scope:** All source in `include/neuroflyer/`, `src/engine/`, `src/ui/`, `tests/`. Focused on functions defined but never called, orphaned declarations, and config fields declared but never read.
> **Findings:** 1 critical, 2 high, 3 medium, 2 low

---

## [HIGH] `render_variant_net()` is never called externally

**ID:** `DEAD-001`

**Location:** `src/ui/renderers/variant_net_render.cpp:130-133`, `include/neuroflyer/renderers/variant_net_render.h:33`

**Description:** The public function `render_variant_net()` is declared in the header and defined in the source file, but it is never called from any other translation unit. The only external caller is `net_viewer_view.cpp`, which calls `build_variant_net_config()` directly (line 91) and then invokes `neuralnet_ui::render_neural_net()` itself. The `render_variant_net()` function simply wraps those two steps, but nobody uses the wrapper anymore.

**Impact:** Dead code in the public API. Developers may assume it is the correct entry point and use it instead of `build_variant_net_config()`, missing the deferred-rendering pattern that `net_viewer_view.cpp` requires.

**Fix plan:**
1. Remove `render_variant_net()` from `variant_net_render.h` and `variant_net_render.cpp`.
2. Verify no other callers exist (grep confirmed zero external call sites).

---

## [HIGH] `ArenaConfig` NTM/SquadLeader topology fields are never read

**ID:** `DEAD-002`

**Location:** `include/neuroflyer/arena_config.h:50-57`

**Description:** Six fields in `ArenaConfig` -- `ntm_input_size`, `ntm_hidden_sizes`, `ntm_output_size`, `squad_leader_input_size`, `squad_leader_hidden_sizes`, `squad_leader_output_size` -- are declared with defaults but are **never read** from any source file or test. The actual topology configuration is done via `NtmNetConfig` and `SquadLeaderNetConfig` in `team_evolution.h`, which have their own independent defaults. The `ArenaConfig` fields are orphaned remnants from the original arena design spec that were superseded before being wired up.

**Impact:** Misleading API surface. A developer modifying arena config through the UI or code would expect these fields to control NTM/squad leader topology, but they have zero effect. The real config structs are `NtmNetConfig` and `SquadLeaderNetConfig`.

**Fix plan:**
1. Remove the six fields from `ArenaConfig`.
2. If the intent is to expose these in the arena config UI later, create a TODO/backlog entry instead of keeping dead fields.

---

## [CRITICAL] `ArenaConfig::world_diagonal()` is never called in production code

**ID:** `DEAD-003`

**Location:** `include/neuroflyer/arena_config.h:73-75`

**Description:** `ArenaConfig::world_diagonal()` is defined but never called from any source file. The only call site is `FighterDrillConfig::world_diagonal()` in a test (`tests/fighter_drill_session_test.cpp:21`). The arena session and arena match code compute the diagonal inline: `std::sqrt(config_.world_width * config_.world_width + config_.world_height * config_.world_height)` (e.g., `arena_session.cpp:375-376`, `arena_match.cpp` via `compute_dir_range()`). The method exists but is consistently bypassed.

**Impact:** Not harmful per se, but the inline computations should be replaced with calls to `world_diagonal()`. Classified as critical because the function exists specifically to prevent this duplication, yet the duplication persists, making the function dead and the code less maintainable.

**Fix plan:**
1. Replace inline diagonal computations in `arena_session.cpp` and `arena_match.cpp` with `config_.world_diagonal()`.
2. Or remove the method if the inline pattern is intentional and add it to the backlog.

---

## [MEDIUM] `cast_rays()` and `cast_rays_with_endpoints()` in `ray.h`/`ray.cpp` are only called from tests

**ID:** `DEAD-004`

**Location:** `include/neuroflyer/ray.h:38-51`, `src/engine/ray.cpp:9-103`

**Description:** The legacy `cast_rays()` and `cast_rays_with_endpoints()` functions are declared and defined, but are **not called from any production code**. All production ray-casting goes through `sensor_engine.cpp` (`query_sensor` -> `query_raycast`). The only call sites for `cast_rays` are in `tests/ray_test.cpp`. The entire `ray.cpp` file is effectively dead production code.

The `ray_range_multiplier()` inline function from `ray.h` IS still used (by `sensor_engine.cpp:263`), and the `HitType`/`RayResult`/`RayEndpoint` types are used by `sensor_engine.h` and `game_view.cpp`. But the two main functions in `ray.cpp` are legacy dead code.

**Impact:** ~100 lines of dead code that must be maintained and compiled. The test file `ray_test.cpp` tests these dead functions rather than the actual sensor engine path.

**Fix plan:**
1. Remove `cast_rays()` and `cast_rays_with_endpoints()` from `ray.h` and `ray.cpp`.
2. Remove `tests/ray_test.cpp` or rewrite it to test `query_sensor()` directly.
3. Keep `ray_range_multiplier()`, `HitType`, `RayResult`, `RayEndpoint` in `ray.h` since they are still referenced.

---

## [MEDIUM] `bullet_triangle_collision()` (non-rotated) is dead for arena/drill contexts

**ID:** `DEAD-005`

**Location:** `include/neuroflyer/collision.h:43-51`

**Description:** The non-rotated `bullet_triangle_collision()` is defined but is **not called from any source file**. The scroller game (`game.cpp`) does not have bullet-vs-ship collisions (bullets only hit towers). The arena session uses `bullet_triangle_collision_rotated()` (line 242). The fighter drill session does not have bullet-ship collisions. The only call sites for the non-rotated version are in `tests/game_test.cpp:134,140`.

The function was added in the arena mode design but was immediately superseded by the rotated variant. It now exists only as dead code tested by a stale test.

**Impact:** Low runtime impact, but the tests give a false sense of coverage -- they test a function that is never called in the real game.

**Fix plan:**
1. Remove `bullet_triangle_collision()` from `collision.h`.
2. Update `tests/game_test.cpp` to test `bullet_triangle_collision_rotated()` instead.
3. Keep `triangle_circle_collision()` (non-rotated) since it IS called from `game.cpp:48` for the scroller mode.

---

## [MEDIUM] `check_collision()` and `check_bullet_hit()` wrapper functions in `game.h` are thin wrappers with minimal value

**ID:** `DEAD-006`

**Location:** `include/neuroflyer/game.h:64,67`, `src/engine/game.cpp:46-54`

**Description:** `check_collision(Triangle, Tower)` simply calls `triangle_circle_collision()` after checking `tower.alive`. `check_bullet_hit(Bullet, Tower)` simply calls `bullet_circle_collision()` after checking `bullet.alive && tower.alive`. These are called from `game.cpp` internally and from `tests/game_test.cpp`. They add a thin alive-check layer over the collision functions. While not strictly dead, the alive checks are already performed by callers in the game loop (the scroller session checks alive before calling collision functions), making the wrappers redundant.

**Impact:** Not harmful but adds indirection. Minor.

**Fix plan:**
1. Inline the alive checks at call sites and call the collision functions directly, or keep as-is (low priority).

---

## [LOW] Legacy `go_to_screen()` and `Screen` enum bridge still exist

**ID:** `DEAD-007`

**Location:** `include/neuroflyer/app_state.h:80`, `include/neuroflyer/ui/legacy_screen.h`, `src/ui/main.cpp:112`

**Description:** The `go_to_screen()` function and the legacy `Screen` enum are part of the migration bridge from the old navigation system to `UIManager`. The CLAUDE.md notes this is "being phased out." All new screens use `ui.push_screen()` / `ui.pop_screen()`. The bridge still exists and is synced in `main.cpp:112`, but new code should not use it.

**Impact:** Not causing bugs, but adds complexity to the main loop. Should be removed when all legacy screens are migrated.

**Fix plan:**
1. Audit whether any code path still calls `go_to_screen()`.
2. If none, remove the function, the `Screen` enum, and the bridge sync in `main.cpp`.

---

## [LOW] `FighterDrillConfig::world_diagonal()` tested but `ArenaConfig::world_diagonal()` is not

**ID:** `DEAD-008`

**Location:** `include/neuroflyer/fighter_drill_session.h:46-48` vs `include/neuroflyer/arena_config.h:73-75`

**Description:** Both `FighterDrillConfig` and `ArenaConfig` define identical `world_diagonal()` methods. `FighterDrillConfig::world_diagonal()` is tested (`tests/fighter_drill_session_test.cpp:21`) but never called from production code. `ArenaConfig::world_diagonal()` is neither tested nor called from production code (see DEAD-003). Both are effectively dead convenience methods.

**Impact:** Misleading test coverage. The test suggests the method is important, but it is unused.

**Fix plan:**
1. Either wire up these methods in production code (replace inline diagonal computations) or remove them.

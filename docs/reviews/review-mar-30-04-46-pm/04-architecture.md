# Architecture Review

> **Scope:** Recent arena mode and fighter drill additions: `fighter_drill_screen.cpp`, `fighter_drill_session.cpp`, `fighter_drill_pause_screen.cpp`, `arena_game_screen.cpp`, `arena_session.cpp`, `arena_sensor.cpp`, `sensor_engine.cpp`, `collision.h`, `variant_net_render.cpp`, `net_viewer_view.cpp`, plus headers and `CMakeLists.txt`.
> **Findings:** 2 critical, 3 high, 5 medium, 3 low

---

## [CRITICAL] Engine code imports UI header (theme.h)

**ID:** `ARCH-001`

**Location:** `src/engine/sensor_engine.cpp:3`

**Description:** `sensor_engine.cpp` lives in `src/engine/` (the pure-logic layer with zero SDL/ImGui dependency) but includes `<neuroflyer/ui/theme.h>`. The theme header is used by `build_input_colors()` (line 327-354) to get `theme::node_sight`, `theme::node_sensor`, `theme::node_system`, and `theme::node_memory` color constants. This violates the project's documented engine/UI separation rule: "Engine code (`src/engine/`) never imports SDL or ImGui."

**Impact:** Breaks the clean build boundary. Any future change to theme.h (or its transitive includes) forces a rebuild of the engine. If theme.h ever gains SDL/ImGui includes, the engine layer will silently acquire UI dependencies.

**Fix plan:**
1. Move the four `node_*` color constants out of `theme.h` into a non-UI header (e.g., `include/neuroflyer/ship_design.h` alongside `NodeStyle`, or a new `include/neuroflyer/sensor_colors.h`).
2. Have `theme.h` re-export or reference those constants if the UI layer also needs them.
3. Remove the `#include <neuroflyer/ui/theme.h>` from `sensor_engine.cpp` and include the new header instead.
4. Verify no other engine files import UI headers: `grep -r 'ui/' src/engine/`.

---

## [CRITICAL] FighterDrillScreen mixes game logic into a UI screen (~800 lines)

**ID:** `ARCH-002`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:254-358`

**Description:** `FighterDrillScreen::run_tick()` contains 100+ lines of game logic that belongs in the engine layer: it computes scripted squad inputs per phase (expand/contract/attack), normalizes headings, builds `ArenaQueryContext`, calls `build_arena_ship_input`, runs neural net forward passes, and calls `decode_output`. This logic is structurally identical to what `arena_game_screen.cpp::tick_arena()` does and what `arena_match.cpp` already does in the engine layer.

The screen class also owns the population, evolution config, networks, and recurrent states as member variables, then orchestrates evolution in `evolve_generation()`. The on_draw method handles initialize, input, tick, evolve, and render all in one call chain inside a UI file.

**Impact:** The engine/UI split is violated: game logic lives in `src/ui/`. This prevents headless testing of the fighter drill tick loop, forces the logic to remain coupled to SDL/ImGui rendering, and makes it impossible to reuse the drill tick for automated evaluation.

**Fix plan:**
1. Create a `FighterDrillMatch` class (or extend `FighterDrillSession`) in `src/engine/` that owns the population, networks, recurrent states, and tick loop. It should expose a `tick()` method that computes inputs, runs nets, and calls session tick internally.
2. The `FighterDrillScreen` should own a `FighterDrillMatch` instance, call `match.tick()`, and only handle rendering and input.
3. This mirrors the pattern in `arena_match.cpp` which already extracts the arena tick logic from the screen.

---

## [HIGH] FighterDrillPauseScreen copies entire population by value

**ID:** `ARCH-003`

**Location:** `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h:29`, `src/ui/screens/game/fighter_drill_pause_screen.cpp:27`

**Description:** `FighterDrillPauseScreen` accepts `std::vector<Individual> population` by value in its constructor and stores a full copy as member `population_`. Each `Individual` contains a `StructuredGenome` (vector of gene groups with float vectors) plus a `NetworkTopology`. With a population of 200 individuals, this is a potentially large allocation that happens every time the user presses Space to pause.

**Impact:** Unnecessary memory allocation and data copy on every pause. The pause screen only reads the population (for display and optional save). It never modifies the originals.

**Fix plan:**
1. Change the constructor to accept `std::span<const Individual>` or `const std::vector<Individual>&` and store a pointer/reference.
2. If the screen needs to outlive the population (unlikely since it is pushed on top of the drill screen which owns the population), use `std::shared_ptr<const std::vector<Individual>>`.
3. The same pattern applies to `ShipDesign` and `EvolutionConfig` parameters, though those are small enough to be negligible.

---

## [HIGH] Duplicate session mechanics between ArenaSession and FighterDrillSession

**ID:** `ARCH-004`

**Location:** `src/engine/arena_session.cpp:176-325` vs `src/engine/fighter_drill_session.cpp:134-244`

**Description:** `FighterDrillSession` and `ArenaSession` have nearly identical implementations of:
- `apply_boundary_rules()` (identical logic, lines 134-152 vs 176-196)
- `spawn_bullets_from_ships()` (identical logic, lines 154-174 vs 198-219)
- `resolve_ship_tower_collisions()` (identical: both use `triangle_circle_collision_rotated`)
- `resolve_ship_token_collisions()` (identical center-to-center proximity check)
- `resolve_bullet_tower_collisions()` (identical)
- `spawn_obstacles()` (identical tower/token random placement with same 15-35 radius range)

These are not minor similarities; they are verbatim copies.

**Impact:** Any bug fix or behavioral change (e.g., changing collision detection) must be applied in two places. This has already diverged slightly: `ArenaSession::update_bullets()` destroys bullets at world boundaries (line 227-229), while `FighterDrillSession::update_bullets()` does not.

**Fix plan:**
1. Extract shared session mechanics into free functions in a new `src/engine/session_utils.cpp` (or a base class `SessionBase`).
2. Both `ArenaSession` and `FighterDrillSession` call these shared functions.
3. The boundary-rule bullet divergence should be explicitly documented or unified.

---

## [HIGH] arena_sensor.cpp duplicates occulus overlap math from sensor_engine.cpp

**ID:** `ARCH-005`

**Location:** `src/engine/arena_sensor.cpp:98-181` vs `src/engine/sensor_engine.cpp:79-137`

**Description:** `query_arena_occulus()` in `arena_sensor.cpp` duplicates the entire ellipse overlap algorithm from `query_occulus()` in `sensor_engine.cpp`. Both:
- Call `compute_sensor_shape()` to get ellipse geometry.
- Define an identical `check_overlap` lambda with the same normalized distance formula.
- Iterate entities checking for overlap.

The arena version extends this to also check ships and bullets (which the solo sensor engine doesn't need), but the core ellipse math is copy-pasted.

Similarly, `ray_circle_hit()` in `arena_sensor.cpp` (lines 16-33) is a local re-implementation of the same math as `ray_circle_intersect()` in `collision.h` (lines 12-31), just with a different return type (normalized distance vs raw parameter t). The comment on line 18 even says "same convention as collision.h."

**Impact:** The documented architecture says sensor_engine.h is "the single source of truth" for sensor detection. Having a parallel implementation in arena_sensor.cpp undermines this. If the ellipse math changes, it must be updated in both files.

**Fix plan:**
1. Factor the core ellipse overlap check into a shared function in `sensor_engine.cpp` (or a new `sensor_math.h`).
2. Have both `query_occulus` and `query_arena_occulus` call the shared function.
3. For raycasting, extend `ray_circle_intersect` to return a normalized result, or add a thin wrapper.

---

## [MEDIUM] SDL drawing helpers duplicated in fighter_drill_screen.cpp

**ID:** `ARCH-006`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:28-90` vs `src/ui/views/arena_game_view.cpp:36-106`

**Description:** Three SDL drawing functions -- `draw_rotated_triangle`, `draw_filled_circle`, and `draw_circle_outline` -- are copy-pasted verbatim from `arena_game_view.cpp` into `fighter_drill_screen.cpp`. The comment on line 26 explicitly acknowledges this: "SDL drawing helpers (same as arena_game_view.cpp)".

**Impact:** `fighter_drill_screen.cpp` does its own inline world rendering instead of using `ArenaGameView` (which was purpose-built for this). Any rendering improvement (e.g., adding fill patterns or GPU acceleration) must be duplicated.

**Fix plan:**
1. Move the three drawing primitives into a shared utility (e.g., `src/ui/renderers/sdl_draw_utils.h`).
2. Have both `arena_game_view.cpp` and `fighter_drill_screen.cpp` use the shared versions.
3. Better yet, refactor `fighter_drill_screen.cpp::render_world()` to use `ArenaGameView` directly, adapting its render method to accept the drill session data.

---

## [MEDIUM] Heading normalization code repeated inline

**ID:** `ARCH-007`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:293-297` and `src/ui/screens/game/fighter_drill_screen.cpp:313-317`

**Description:** The angle normalization pattern (relative heading from absolute heading, normalized to [-pi, pi] then to [-1, 1]) is hand-inlined twice in `run_tick()`:
```
float abs_heading = std::atan2(dr.dir_sin, dr.dir_cos);
float rel = abs_heading - ships[i].rotation;
while (rel > pi) rel -= 2*pi;
while (rel < -pi) rel += 2*pi;
result = rel / pi;
```

The same math exists in `squad_leader.cpp::compute_squad_leader_fighter_inputs()` (which the arena screen uses). The drill screen reimplements it instead of calling that function.

**Impact:** The drill screen uses a different code path for heading computation than the arena screen, creating a risk of behavioral drift. The `while` loops for normalization are also technically unsafe for extreme values (though unlikely in practice).

**Fix plan:**
1. Use `std::remainder(rel, 2*pi)` or `std::fmod` for normalization (single operation, no loop).
2. Extract a `normalize_relative_heading(abs_heading, ship_rotation) -> float` utility.
3. Have the drill screen call `compute_squad_leader_fighter_inputs()` or the extracted utility.

---

## [MEDIUM] FighterDrillSession::get_scores() returns by value

**ID:** `ARCH-008`

**Location:** `include/neuroflyer/fighter_drill_session.h:63`

**Description:** `get_scores()` is declared as `std::vector<float> get_scores() const { return scores_; }` which copies the entire scores vector each call. It is called in the hot path: once per frame in `render_hud()` (line 741) for the leaderboard, plus once per generation in `evolve_generation()` (line 364). With a population of 200, this is a 800-byte copy per call.

`ArenaSession::get_scores()` (line 418-430) has a different but related issue: it computes and returns a new vector each call, but that's by design since it aggregates per-team scores.

**Impact:** Minor but unnecessary allocations in a hot path. The scores vector is stable between ticks.

**Fix plan:**
1. Change `FighterDrillSession::get_scores()` to return `const std::vector<float>&`.
2. Update `render_hud()` and `evolve_generation()` to use `const auto&` when capturing the result.

---

## [MEDIUM] ArenaQueryContext constructed with 12+ lines of boilerplate in 3 places

**ID:** `ARCH-009`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:322-335`, `src/ui/screens/arena/arena_game_screen.cpp:366-378`, `src/engine/arena_match.cpp:140-152`

**Description:** Building an `ArenaQueryContext` requires setting 12 fields in 3 separate locations with nearly identical code. The struct has no constructor or factory function.

**Impact:** Boilerplate that risks omitting a field (currently all 12 are always set, but adding a new field requires updating 3+ locations).

**Fix plan:**
1. Add a static factory method on `ArenaQueryContext`, e.g.:
   `static ArenaQueryContext for_ship(size_t idx, int team, const ArenaSession& session, const vector<int>& ship_teams)`.
2. Replace the 3 manual construction sites with the factory call.

---

## [MEDIUM] collision.h non-rotated variants still exist alongside rotated versions

**ID:** `ARCH-010`

**Location:** `include/neuroflyer/collision.h:43-61`

**Description:** `collision.h` contains both `bullet_triangle_collision` (non-rotated, lines 43-51) and `bullet_triangle_collision_rotated` (rotated, lines 87-96), as well as `triangle_circle_collision` (non-rotated, lines 54-61) and `triangle_circle_collision_rotated` (rotated, lines 99-107). The non-rotated versions assume the triangle is axis-aligned (pointing straight up).

The non-rotated `bullet_triangle_collision` is never called anywhere in the codebase. The non-rotated `triangle_circle_collision` is only called in the legacy `game.cpp:48` (solo fly mode where ships don't rotate). The arena and drill modes exclusively use the rotated variants.

**Impact:** The non-rotated functions are confusing API surface. A developer might accidentally use the non-rotated version in new code (especially since the names don't hint at the difference without reading docs).

**Fix plan:**
1. Rename the non-rotated versions to `_axis_aligned` or add a deprecation comment.
2. If the legacy solo mode ever adopts rotation, remove the non-rotated variants entirely.
3. Consider making the rotated versions the default names.

---

## [LOW] std::cout debug logging in UI screens

**ID:** `ARCH-011`

**Location:** `src/ui/screens/game/fighter_drill_screen.cpp:190-191,377-379`, `src/ui/screens/arena/arena_game_screen.cpp:411-412`

**Description:** `FighterDrillScreen` and `ArenaGameScreen` use raw `std::cout` for generation statistics and initialization messages. There is no logging level or way to disable this output.

**Impact:** Console noise during production use. Cannot filter or redirect logs.

**Fix plan:**
1. Introduce a minimal logging utility (or use existing if one exists) with level control.
2. Replace `std::cout` with conditional logging that can be disabled in release builds.

---

## [LOW] Snapshot stored by value in FighterDrillScreen

**ID:** `ARCH-012`

**Location:** `include/neuroflyer/ui/screens/fighter_drill_screen.h:41`

**Description:** `FighterDrillScreen` stores `source_snapshot_` by value. `Snapshot` contains vectors for topology, weights, and ship design. The snapshot is only used during `initialize()` (line 141-156) and is never read again after initialization.

**Impact:** The snapshot occupies memory for the lifetime of the screen even though it's only needed for setup.

**Fix plan:**
1. After `initialize()` completes, clear the snapshot: `source_snapshot_ = Snapshot{};` or use `std::optional<Snapshot>`.
2. Alternatively, pass the snapshot only to `initialize()` and don't store it as a member.

---

## [LOW] Build system fetches library repos from GitHub

**ID:** `ARCH-013`

**Location:** `CMakeLists.txt:14-25`

**Description:** `FetchContent_Declare` for `evolve`, `neuralnet`, and `neuralnet-ui` all use `GIT_TAG main`. This means every clean build fetches the latest `main` commit, which could introduce breaking changes without notice. The project docs say these repos are expected at `../libs/` but the CMake doesn't check for local copies first.

**Impact:** Builds are not reproducible. A breaking change in any library repo will silently break the NeuroFlyer build.

**Fix plan:**
1. Pin `GIT_TAG` to specific commit hashes or version tags.
2. Add `FETCHCONTENT_SOURCE_DIR_*` overrides that prefer local `../libs/` paths when available.
3. Use a CI lock file or dependency manifest to track exact versions.

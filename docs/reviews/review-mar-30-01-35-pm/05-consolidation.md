# Consolidation Opportunities

**Date:** 2026-03-30
**Scope:** Full NeuroFlyer repo, C++20 / SDL2 / ImGui
**Focus:** Magic numbers, duplicate code, copy-paste drift, repeated boilerplate, misplaced utilities

---

## [HIGH] Squad leader input computation duplicated across three files

**ID:** `CONS-001`
**Location:**
- `src/ui/screens/arena/arena_game_screen.cpp:268-297` (tick_arena)
- `src/engine/arena_match.cpp:90-121` (run_arena_match)
- The same inline `home_dx/home_dy/world_diag/home_heading_sin/cos/cmd_heading_sin/cos` pattern

**Description:** The 20-line block that computes squad leader input values (home heading, command target heading, world_diag normalization) is copy-pasted identically in `arena_game_screen.cpp` and `arena_match.cpp`. The code computes `world_diag`, then derives `home_heading_sin`, `home_heading_cos`, `home_distance`, `cmd_heading_sin`, `cmd_heading_cos`, `cmd_target_distance` using raw `sqrt`/division. Both copies are character-for-character identical except for variable naming (`config_` vs `arena_config`).

Meanwhile, `compute_dir_range()` in `arena_sensor.cpp` already encapsulates the sin/cos/range computation as a reusable function and is used elsewhere (e.g., `squad_leader.cpp:194-215`). The duplicated blocks reinvent what `compute_dir_range()` does.

**Impact:** Any change to the normalization formula (e.g., different world_diag semantics) must be applied in three places. The `compute_dir_range()` function exists specifically to eliminate this pattern.

**Fix plan:**
1. Create a `compute_squad_leader_inputs()` function in `squad_leader.h/cpp` that takes `(SquadStats, bases, team, world_w, world_h)` and returns a struct with all 9 pre-computed inputs.
2. Call it from `arena_game_screen.cpp::tick_arena()` and `arena_match.cpp::run_arena_match()`.
3. Internally, use `compute_dir_range()` instead of inline math.

---

## [HIGH] evolve_team_population and evolve_squad_only share 90% identical code

**ID:** `CONS-002`
**Location:** `src/engine/team_evolution.cpp:76-153`
**Description:** `evolve_team_population()` (lines 76-114) and `evolve_squad_only()` (lines 116-153) are structurally identical: sort by fitness, copy elites, tournament selection loop, construct child, mutate, push. The ONLY difference is lines 106-108 vs 146-147: the full version mutates all three sub-nets (`ntm + squad + fighter`), while the squad-only version mutates just two (`ntm + squad`).

**Impact:** Adding a new evolution behavior (e.g., evolve only fighters, or add crossover) requires duplicating the entire function again. Bug fixes to the tournament selection or elitism logic must be applied in both places.

**Fix plan:**
1. Create a single `evolve_team_population_impl()` that takes a `mutation_fn` callback:
   ```cpp
   using MutateFn = std::function<void(TeamIndividual&, const EvolutionConfig&, std::mt19937&)>;
   std::vector<TeamIndividual> evolve_team_population_impl(
       std::vector<TeamIndividual>& pop, const EvolutionConfig& cfg,
       std::mt19937& rng, MutateFn mutate_fn);
   ```
2. `evolve_team_population()` passes a lambda that mutates all three nets.
3. `evolve_squad_only()` passes a lambda that mutates only ntm + squad.

---

## [HIGH] Fitness scoring logic duplicated between arena_match.cpp and arena_game_screen.cpp

**ID:** `CONS-003`
**Location:**
- `src/engine/arena_match.cpp:186-237`
- `src/ui/screens/arena/arena_game_screen.cpp:562-604`

**Description:** The end-of-round fitness calculation (damage dealt, own survival, alive fraction, token fraction, weighted sum) is implemented twice. The `arena_match.cpp` version is the "headless" match runner; the `arena_game_screen.cpp` version is the interactive version. Both compute the same four metrics with the same formula and the same weight application. Minor drift already exists: `arena_match.cpp` uses `ships_per_team = num_squads * fighters_per_squad` for the alive fraction denominator, while `arena_game_screen.cpp` counts ships by iterating and summing. This produces different denominators if a team has ships killed before the first tick.

**Impact:** Formula changes must be applied in two places. The subtle denominator difference is a latent bug where headless and interactive training may produce slightly different fitness landscapes.

**Fix plan:**
1. Extract a `compute_team_fitness(const ArenaSession&, const ArenaConfig&, const std::vector<int>& ship_teams, int team)` function into `arena_match.cpp` (or a new `arena_scoring.h/cpp`).
2. Call it from both `run_arena_match()` and `ArenaGameScreen::on_draw()`.
3. The function computes and returns a single float fitness score for one team.

---

## [MEDIUM] SectorGrid rebuilt from scratch every tick

**ID:** `CONS-004`
**Location:**
- `src/ui/screens/arena/arena_game_screen.cpp:235-245` (tick_arena)
- `src/engine/arena_match.cpp:54-67` (run_arena_match loop body)

**Description:** Every tick, a new `SectorGrid` is constructed (allocating the grid cells vector), all alive ships and bases are inserted, and the grid is used once for NTM threat gathering. The grid is then discarded. For the default arena (2 teams x 8 fighters + 2 bases = 18 entities on a grid that may have hundreds of cells), the construction cost is:
- `SectorGrid` constructor allocates `rows_ * cols_` vectors (world 81920x51200 / sector 2000 = ~41 x 26 = 1066 cells).
- Each tick: allocate 1066 empty vectors, insert 18 entities, query once, destroy 1066 vectors.

**Impact:** Unnecessary heap allocation churn every tick. For fast-forward at 100 ticks/frame, this is 100 grid constructions per frame.

**Fix plan:**
1. Make `SectorGrid` a member of `ArenaGameScreen` (and a local in `run_arena_match` initialized once before the loop).
2. Call `grid.clear()` at the start of each tick instead of constructing a new grid.
3. The `clear()` method already exists and reuses the existing allocated vectors.

---

## [MEDIUM] BULLET_RADIUS magic number in arena_sensor.cpp

**ID:** `CONS-005`
**Location:** `src/engine/arena_sensor.cpp:86`
**Description:** `constexpr float BULLET_RADIUS = 2.0f;` is defined as a local constant in `query_arena_sensor()`. This value is used for sensor ray-circle intersection against bullets. However, the bullet collision radius in `arena_session.cpp` uses `Triangle::SIZE` for bullet-ship collision (line 247), and bullet rendering in `arena_game_view.cpp:312` uses `std::max(2.0f, 3.0f * camera.zoom)`. There is no shared constant for the bullet's physical radius.

**Impact:** The sensor "sees" bullets as 2px radius circles, but collision detection uses `Triangle::SIZE` (12px). This means sensors detect bullets at a much closer range than the actual collision envelope, giving fighters less reaction time than the physics warrants.

**Fix plan:**
1. Add `static constexpr float RADIUS = 2.0f;` to the `Bullet` struct in `game.h`.
2. Use `Bullet::RADIUS` in `arena_sensor.cpp`, `arena_session.cpp` (for the bullet-ship distance check), and `arena_game_view.cpp`.
3. Decide whether the collision radius and sensor radius should actually match, and document the choice.

---

## [MEDIUM] world_diag recomputed in 4+ locations

**ID:** `CONS-006`
**Location:**
- `src/engine/arena_sensor.cpp:106` (compute_dir_range)
- `src/engine/arena_match.cpp:103` (per-team loop)
- `src/ui/screens/arena/arena_game_screen.cpp:279` (per-team loop)
- `src/engine/arena_session.cpp:376,398` (compute_squad_stats, twice)

**Description:** `std::sqrt(world_w * world_w + world_h * world_h)` is computed from scratch at every call site. In `compute_squad_stats()`, it is computed twice in the same function (lines 376 and 398) because the two blocks that need it are separated.

**Impact:** Minor performance cost (sqrt is cheap), but the real issue is readability and the risk of inconsistency. If the normalization strategy changes (e.g., divide by max dimension instead of diagonal), every site must be found and updated.

**Fix plan:**
1. Add `[[nodiscard]] float world_diagonal() const noexcept` to `ArenaConfig`.
2. Replace all inline `sqrt(w*w + h*h)` computations with `config.world_diagonal()`.
3. In `compute_dir_range()`, accept the pre-computed diagonal as a parameter (or the `ArenaConfig&`).

---

## [MEDIUM] ArenaQueryContext constructed inline with 11 field assignments

**ID:** `CONS-007`
**Location:**
- `src/ui/screens/arena/arena_game_screen.cpp:332-345`
- `src/engine/arena_match.cpp:140-152`

**Description:** Both `tick_arena()` and `run_arena_match()` construct an `ArenaQueryContext` by assigning all 11 fields one-by-one in a block. The two blocks are identical except for the source variable names (`arena_->` vs `arena.`). This is 13 lines of pure boilerplate per call site.

**Impact:** Adding a new field to `ArenaQueryContext` requires updating both sites identically.

**Fix plan:**
1. Add a factory function to `ArenaQueryContext`:
   ```cpp
   static ArenaQueryContext for_ship(
       const ArenaSession& arena, std::size_t ship_idx,
       const std::vector<int>& ship_teams);
   ```
2. Or add an `ArenaSession::query_context_for(size_t ship_idx)` method.
3. Replace both 13-line blocks with a one-line call.

---

## [MEDIUM] Duplicate NTM/leader/fighter network compilation in arena_match.cpp and arena_game_screen.cpp

**ID:** `CONS-008`
**Location:**
- `src/engine/arena_match.cpp:27-38`
- `src/ui/screens/arena/arena_game_screen.cpp:131-137`

**Description:** Both `run_arena_match()` and `ArenaGameScreen::start_new_match()` contain the same loop pattern: iterate teams, call `build_ntm_network()`, `build_squad_network()`, `build_fighter_network()`, push into three parallel vectors. The two copies differ only in whether they use `arena_config.num_teams` or `current_team_indices_`.

**Impact:** If a new sub-net type is added (e.g., a commander net), both sites need updating.

**Fix plan:**
1. Add a `struct CompiledTeamNets { Network ntm, leader, fighter; }` and a function `compile_team_nets(const TeamIndividual&) -> CompiledTeamNets`.
2. Or add a `compile_all_teams(const vector<TeamIndividual>&) -> tuple<vector<Network>, vector<Network>, vector<Network>>` utility.
3. Use it in both locations.

---

## [LOW] Recurrent state initialization duplicated

**ID:** `CONS-009`
**Location:**
- `src/ui/screens/arena/arena_game_screen.cpp:156-158`
- `src/engine/arena_match.cpp:42-43`

**Description:** Both sites initialize recurrent states the same way:
```cpp
std::vector<std::vector<float>>(total_ships, std::vector<float>(memory_slots, 0.0f))
```
This is a minor duplication, but it couples both sites to the initialization convention (zeros).

**Impact:** Low. If recurrent state initialization changes (e.g., to random values), both sites need updating.

**Fix plan:** Create `make_recurrent_states(size_t num_ships, size_t memory_slots)` in `arena_sensor.h` or a new `arena_utils.h`.

---

## [LOW] Per-team enemy base lookup duplicated

**ID:** `CONS-010`
**Location:**
- `src/ui/screens/arena/arena_game_screen.cpp:270-277`
- `src/engine/arena_match.cpp:93-100`

**Description:** Both files find the nearest enemy base with the same min-distance-squared loop:
```cpp
float min_dist_sq = max; for (base : bases) { if (same team) continue; compute dsq; if (dsq < min) update; }
```

**Impact:** Low individually, but this is part of the larger CONS-001 duplication.

**Fix plan:** Subsumed by CONS-001 fix. The `compute_squad_leader_inputs()` function would encapsulate this lookup.

---

## [LOW] Triangle::SIZE used as ship collision radius across arena code

**ID:** `CONS-011`
**Location:**
- `src/engine/arena_sensor.cpp:78` (ship-to-ship sensor)
- `src/engine/arena_session.cpp:206-207` (bullet spawn offset)
- `src/engine/arena_session.cpp:247` (bullet-ship collision fallback)
- `src/engine/arena_session.cpp:319` (ship-token collision)

**Description:** `Triangle::SIZE` (12.0f) is used as both the visual size AND the collision radius for ships. In the scroller mode, `collision.h` checks vertex positions at offsets of `SIZE`. In arena mode, line 247 uses `SIZE * SIZE` as a squared distance threshold. The same constant serves three different geometric roles: visual half-width, vertex offset, and collision circle radius.

**Impact:** If ship rendering scale is ever decoupled from collision radius (e.g., zoom-dependent scaling), all these sites would need disentangling. The conceptual overloading makes it unclear what changing `SIZE` would do.

**Fix plan:**
1. Add `static constexpr float COLLISION_RADIUS = SIZE;` to `Triangle`.
2. Use `COLLISION_RADIUS` for all collision/sensor checks.
3. Use `SIZE` only for rendering vertex positions.
4. Currently they are equal, but the named constants document intent.

---

## [LOW] Team color definitions only in arena_game_view.cpp anonymous namespace

**ID:** `CONS-012`
**Location:** `src/ui/views/arena_game_view.cpp:15-33`
**Description:** The 8 team colors are defined in an anonymous namespace local to `arena_game_view.cpp`. If any other view or screen needs team colors (e.g., the info panel, a future minimap, or team labels), it would need to duplicate the color table.

**Impact:** Low today (only one consumer). If a second consumer appears, duplication is guaranteed.

**Fix plan:** Move the `TEAM_COLORS` array and `team_color()` function to `include/neuroflyer/ui/theme.h` alongside the existing sensor colors, under a `theme::team_color()` function.

---

## [LOW] compute_arena_input_size counts full_sensor as 5 but build_arena_ship_input only adds 5 values conditionally

**ID:** `CONS-013`
**Location:**
- `src/engine/arena_sensor.cpp:117-123` (compute_arena_input_size)
- `src/engine/arena_sensor.cpp:136-154` (build_arena_ship_input)

**Description:** `compute_arena_input_size()` adds 5 per full sensor and 1 per non-full sensor. `build_arena_ship_input()` pushes `distance` (1 value) always, then conditionally pushes 4 more if `is_full_sensor`. So full sensors contribute 1 + 4 = 5 values. The count function says `s.is_full_sensor ? 5 : 1`, which matches. However, the scroller version `compute_input_size()` in `ship_design.h:42-49` uses `s.is_full_sensor ? 4 : 1` (not including the distance in the count because it is always present). This inconsistency in how the count is expressed (even though the final sizes are correct) could mislead someone reading both functions.

**Impact:** Low. Both produce correct sizes. But the different counting conventions (4 extra vs 5 total) across scroller and arena code is a readability trap.

**Fix plan:** Add a comment to both functions documenting the convention: "per-sensor values: 1 distance + (4 type flags if full_sensor)". Or add `SensorDef::input_count()` as a single source.

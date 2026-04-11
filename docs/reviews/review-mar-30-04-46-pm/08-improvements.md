# Improvements

> **Scope:** Full engine (arena_session, fighter_drill_session, arena_sensor, sensor_engine, evolution, team_evolution, squad_leader, game, collision, sector_grid, snapshot_io, genome_manager, mrca_tracker, config, paths), all test files in tests/, UI screens (arena_game_screen, arena_pause_screen, fighter_drill_screen, fly_session_screen, variant_viewer_screen, settings_screen), renderers (variant_net_render), views (net_viewer_view).
> **Findings:** 20 total suggestions

---

## [HIGH] Arena sensor queries are O(ships * entities) per tick with no spatial culling

**ID:** `IMP-001`

**Location:** `src/engine/arena_sensor.cpp:36-93` (raycast), `src/engine/arena_sensor.cpp:98-181` (occulus), called from `src/ui/screens/arena/arena_game_screen.cpp:353-398`

**Description:** Every alive fighter queries every sensor against every tower, token, ship, and bullet in the arena. With 16 ships, each having ~8 sensors, and 200 towers + 100 tokens + 16 ships + N bullets, this is roughly `16 * 8 * (200 + 100 + 16 + N)` ray-circle or ellipse-point tests per tick. At higher speeds (100 ticks/frame), this becomes the dominant bottleneck.

The SectorGrid already exists and is used for NTM threat gathering, but arena_sensor queries bypass it entirely. Each sensor has a known max range, so entities far away can be trivially skipped.

**Impact:** At 100x speed, frame time is dominated by sensor queries. A spatial pre-filter could eliminate 80-90% of intersection tests.

**Fix plan:**
1. Before the per-fighter loop in `tick_arena()`, insert all towers/tokens/bullets into the SectorGrid (ships are already inserted).
2. Pass the grid into `ArenaQueryContext` or add a helper `query_arena_sensor_with_grid()`.
3. For each sensor query, compute the sensor's bounding sector range from its max reach, and only test entities returned by `entities_in_diamond()`.
4. This is a pure performance change with no behavioral difference.

---

## [HIGH] Bullet-ship collision is O(bullets * ships) with no early-out for dead bullets

**ID:** `IMP-002`

**Location:** `src/engine/arena_session.cpp:234-262`

**Description:** `resolve_bullet_ship_collisions()` iterates all bullets against all ships. When a bullet kills a ship and `break`s, it moves to the next bullet, but the outer loop still iterates bullets that were killed in a previous inner iteration (the `!b.alive` check catches this, but the iteration happens). More importantly, there is no spatial partitioning -- every bullet checks every ship.

In a 2-team match with 16 ships, bullet counts can grow to 50-100+. This is manageable now but will scale poorly if population sizes increase.

**Impact:** Modest improvement now; significant at larger population sizes or with more teams.

**Fix plan:**
1. Use the SectorGrid already built in `tick_arena()` for the bullet-ship collision phase.
2. For each bullet, only check ships in nearby sectors (bullet radius is ~2px, so a 1-sector diamond suffices).
3. Alternatively, if SectorGrid is not passed into ArenaSession, a simple broad-phase AABB check (skip ships > max_distance) would help at low cost.

---

## [HIGH] Duplicated snapshot-to-individual deserialization logic

**ID:** `IMP-003`

**Location:** `src/engine/evolution.cpp:752-780` (`snapshot_to_individual`), `src/engine/evolution.cpp:782-852` (`create_population_from_snapshot` lines 797-820)

**Description:** The logic to fill weight/bias/activation genes from a flat snapshot weight vector is copy-pasted between `snapshot_to_individual()` and the first individual in `create_population_from_snapshot()`. Both perform the same offset-tracking loop over layers. If the format changes (e.g., new gene types), both must be updated in lockstep.

**Impact:** Reduces maintenance burden and risk of deserialization bugs when the format evolves.

**Fix plan:**
`create_population_from_snapshot()` should call `snapshot_to_individual()` to build its first individual, rather than duplicating the loop:
```cpp
Individual first = snapshot_to_individual(snapshot);
pop.push_back(first);
```
The rest of the function (mutated copies) stays unchanged.

---

## [HIGH] Arena sensor queries allocate a new vector per ship per tick via build_arena_ship_input

**ID:** `IMP-004`

**Location:** `src/engine/arena_sensor.cpp:226-269` (`build_arena_ship_input`), called from `src/ui/screens/arena/arena_game_screen.cpp:380-385`

**Description:** `build_arena_ship_input()` allocates and returns a `std::vector<float>` every call. With 16 fighters and 100 ticks/frame at high speed, that is 1600 heap allocations per frame just for input vectors. The input size is known at compile time (from `compute_arena_input_size()`), so a pre-allocated buffer could be reused.

**Impact:** Reduces GC pressure and allocation overhead in the hot loop. Particularly impactful at high simulation speeds.

**Fix plan:**
1. Add a `build_arena_ship_input_into(std::span<float> out, ...)` variant that writes into a caller-owned buffer.
2. In `tick_arena()`, allocate the buffer once before the loop and reuse it each iteration.
3. Keep the existing allocating version for non-hot paths (test bench, etc.).

---

## [MEDIUM] SectorGrid::entities_in_diamond returns a new vector every call

**ID:** `IMP-005`

**Location:** `src/engine/sector_grid.cpp:34-49`

**Description:** `entities_in_diamond()` allocates and returns a `std::vector<std::size_t>` each call. In `gather_near_threats()`, this is called once per team per tick, which is fine. But if SectorGrid is adopted for sensor queries (IMP-001), it would be called 16 * 8 = 128 times per tick.

**Impact:** Prevents allocation-heavy usage if spatial indexing is adopted more broadly.

**Fix plan:**
Add an overload that appends to a caller-provided vector:
```cpp
void entities_in_diamond(SectorCoord center, int radius,
                         std::vector<std::size_t>& out) const;
```
The caller clears and reuses a single scratch vector across calls.

---

## [MEDIUM] compute_squad_stats recomputes world diagonal twice

**ID:** `IMP-006`

**Location:** `src/engine/arena_session.cpp:349-416`

**Description:** `compute_squad_stats()` computes `std::sqrt(config_.world_width^2 + config_.world_height^2)` in two separate places (line 375 and line 397). `ArenaConfig` already has a `world_diagonal()` method. The function should use it instead of recomputing.

**Impact:** Minor performance (two sqrt calls per squad per tick), but more importantly improves readability and uses the canonical accessor.

**Fix plan:** Replace both inline diagonal computations with `config_.world_diagonal()`.

---

## [MEDIUM] teams_alive() uses std::set for a 2-4 element count

**ID:** `IMP-007`

**Location:** `src/engine/arena_session.cpp:440-448`

**Description:** `teams_alive()` inserts team IDs into a `std::set<int>` to count unique alive teams, then returns `alive_teams.size()`. With 2-4 teams and 16 ships, this allocates a red-black tree for at most 4 elements. A bitfield or small array would be faster and allocation-free.

**Impact:** Called every tick in `check_end_conditions()`. Removing the heap allocation is a small but easy win.

**Fix plan:**
Replace with a fixed-size boolean array:
```cpp
std::array<bool, 16> seen{}; // max 16 teams
for (std::size_t i = 0; i < ships_.size(); ++i) {
    if (ships_[i].alive) seen[team_assignments_[i]] = true;
}
return std::count(seen.begin(), seen.begin() + config_.num_teams, true);
```

---

## [MEDIUM] FighterDrillSession duplicates boundary, bullet, and tower collision logic from ArenaSession

**ID:** `IMP-008`

**Location:** `src/engine/fighter_drill_session.cpp:134-244` vs `src/engine/arena_session.cpp:176-308`

**Description:** `apply_boundary_rules()`, `spawn_bullets_from_ships()`, `update_bullets()`, `resolve_ship_tower_collisions()`, `resolve_bullet_tower_collisions()`, and `resolve_ship_token_collisions()` are nearly identical between FighterDrillSession and ArenaSession. Any bug fix in one must be manually replicated in the other.

**Impact:** Reduces maintenance burden and ensures consistency between modes.

**Fix plan:**
Extract shared collision/physics logic into free functions in a new `session_physics.h`/`.cpp`:
```cpp
void apply_boundary_rules(std::span<Triangle> ships, float w, float h, bool wrap_ns, bool wrap_ew);
void spawn_bullets(std::span<Triangle> ships, std::vector<Bullet>& bullets, float max_range);
```
Both session classes call these helpers instead of maintaining their own copies.

---

## [MEDIUM] No test coverage for team_evolution.cpp crossover or squad-only evolution

**ID:** `IMP-009`

**Location:** `tests/team_evolution_test.cpp` (exists but limited), `src/engine/team_evolution.cpp:81-158`

**Description:** `evolve_team_population()` and `evolve_squad_only()` have no tests verifying that elitism is preserved, that fitness ordering is correct, or that squad-only mode actually freezes fighter weights. The existing `team_evolution_test.cpp` tests focus on construction and basic creation.

**Impact:** Critical evolution logic goes untested. A regression in squad-only freezing would silently corrupt training.

**Fix plan:**
Add tests for:
1. `evolve_team_population()` preserves top-N elites (compare genomes pre/post).
2. `evolve_squad_only()` does not modify `fighter_individual` weights.
3. Fitness assignment propagates correctly through tournament selection.
4. Population size is maintained after evolution.

---

## [MEDIUM] No test for snapshot round-trip with v7 net_type field

**ID:** `IMP-010`

**Location:** `src/engine/snapshot_io.cpp:97-99` (write), `src/engine/snapshot_io.cpp:163-165` (read)

**Description:** Snapshot format v7 added the `net_type` field, but the existing snapshot_io tests likely do not cover round-tripping a snapshot with `net_type = Fighter` or `net_type = SquadLeader`. If serialization or deserialization of this field has a bug, it would silently default to the wrong type.

**Impact:** Prevents regressions in the arena snapshot format, which is actively evolving.

**Fix plan:**
Add a test that creates a snapshot with `net_type = NetType::Fighter`, saves it to a stream, loads it back, and asserts `loaded.net_type == NetType::Fighter`. Repeat for `SquadLeader`.

---

## [MEDIUM] evolution.cpp activation gene extraction is duplicated three times

**ID:** `IMP-011`

**Location:** `src/engine/evolution.cpp:182-196` (`build_network`), `src/engine/evolution.cpp:687-699` (`create_random_snapshot`), `src/engine/evolution.cpp:732-744` (`best_as_snapshot`)

**Description:** The loop that reads activation gene values, rounds them to ints, clamps them, and assigns them to `node_activations` is copy-pasted in three places. If the clamping logic or activation mapping changes, all three must be updated.

**Impact:** DRY violation that increases the risk of inconsistency.

**Fix plan:**
Extract a helper:
```cpp
void populate_node_activations(NetworkTopology& topo, const StructuredGenome& genome);
```
Call it from all three sites.

---

## [MEDIUM] ArenaGameScreen::tick_arena rebuilds SectorGrid from scratch every tick

**ID:** `IMP-012`

**Location:** `src/ui/screens/arena/arena_game_screen.cpp:266-277`

**Description:** A new `SectorGrid` is constructed and populated every tick. The constructor allocates the cell vectors, and `insert()` pushes to them. Between ticks, only a few entities move. A persistent grid with `clear()` + `insert()` would avoid the constructor overhead.

**Impact:** Small allocation savings per tick, but compounds at high speed.

**Fix plan:**
Make `SectorGrid` a member of `ArenaGameScreen`. Call `grid_.clear()` at the start of `tick_arena()` instead of constructing a new one. The grid dimensions never change within a match.

---

## [MEDIUM] Bullet::update_directional computes sqrt every tick for distance tracking

**ID:** `IMP-013`

**Location:** `src/engine/game.cpp:56-65`

**Description:** `update_directional()` computes `std::sqrt(move_x * move_x + move_y * move_y)` every tick to track `distance_traveled`. Since `dir_x` and `dir_y` are normalized and `SPEED` is constant, the distance per tick is always exactly `SPEED`. The sqrt is unnecessary.

**Impact:** With potentially hundreds of bullets, eliminating a sqrt per bullet per tick adds up.

**Fix plan:**
Replace:
```cpp
distance_traveled += std::sqrt(move_x * move_x + move_y * move_y);
```
With:
```cpp
distance_traveled += SPEED;
```
This assumes `dir_x` and `dir_y` are unit vectors, which they are (set from `sin/cos` in spawn_bullets_from_ships).

---

## [MEDIUM] No crossover in team evolution -- all reproduction is asexual

**ID:** `IMP-014`

**Location:** `src/engine/team_evolution.cpp:81-119` (`evolve_team_population`), `src/engine/team_evolution.cpp:121-158` (`evolve_squad_only`)

**Description:** Unlike `evolve_population()` in solo mode which attempts same-topology crossover, team evolution only does tournament selection + mutation (asexual reproduction). This means the NTM, squad leader, and fighter nets never exchange genetic material between team genomes, reducing genetic diversity.

**Impact:** Crossover is a key driver of evolutionary search efficiency. Adding it to team evolution could significantly accelerate convergence.

**Fix plan:**
1. Tournament-select two parents.
2. For each sub-net (NTM, squad leader, fighter), if they have the same topology, apply `evolve::crossover()`.
3. Fall back to asexual copy if topologies differ.
4. This mirrors the pattern already in `evolve_population()`.

---

## [LOW] gmtime_r used in genome_manager.cpp is POSIX-only

**ID:** `IMP-015`

**Location:** `src/engine/genome_manager.cpp:31`

**Description:** `gmtime_r()` is a POSIX function and is not available on MSVC/Windows. While the project currently targets macOS, this would break a future Windows port.

**Impact:** Portability improvement for potential Windows builds.

**Fix plan:**
Replace with C++20 `<chrono>` formatting:
```cpp
auto tp = std::chrono::system_clock::now();
auto dp = std::chrono::floor<std::chrono::days>(tp);
std::chrono::year_month_day ymd{dp};
// format to string
```
Or use `std::gmtime()` with a note about thread safety (acceptable for a single-user desktop app).

---

## [LOW] snprintf used for label generation instead of std::format

**ID:** `IMP-016`

**Location:** `src/engine/arena_sensor.cpp:279-308` (`build_arena_fighter_input_labels`), `src/ui/renderers/variant_net_render.cpp:76-89`

**Description:** Multiple places use C-style `snprintf` with fixed-size `char buf[32]` for building strings like "SNS 0 D" or "M3". C++20 `std::format` would be cleaner and type-safe.

**Impact:** Code clarity and type safety. No functional change.

**Fix plan:**
Replace `snprintf` calls with `std::format`:
```cpp
labels.push_back(std::format("SNS {} D", sensor_idx));
```
This requires C++20 `<format>` support, which is available in the project's compiler.

---

## [LOW] ArenaConfig has hardcoded NTM/squad-leader topology sizes that could drift from actual usage

**ID:** `IMP-017`

**Location:** `include/neuroflyer/arena_config.h:50-57`

**Description:** `ArenaConfig` stores `ntm_input_size = 7`, `squad_leader_input_size = 14`, etc. as plain constants. These must match the actual input vectors built in `squad_leader.cpp`. If someone adds an NTM input field without updating ArenaConfig, the mismatch would cause a silent neural net dimension error at runtime.

**Impact:** Prevents subtle bugs from config/code drift.

**Fix plan:**
Add a static assertion or runtime check in `run_ntm_threat_selection()` and `run_squad_leader()`:
```cpp
assert(input.size() == config.ntm_input_size && "NTM input size mismatch");
```
Or better, derive the input sizes from the actual construction logic rather than hardcoding them.

---

## [LOW] No way to tune arena fitness weights from the UI

**ID:** `IMP-018`

**Location:** `include/neuroflyer/arena_config.h:63-66`, `src/ui/screens/arena/arena_game_screen.cpp:601-638`

**Description:** Arena fitness weights (`fitness_weight_base_damage`, `fitness_weight_survival`, `fitness_weight_ships_alive`, `fitness_weight_tokens`) are set in `ArenaConfig` defaults and never exposed in a pause screen or config UI. Solo mode has a fitness editor (`fitness_editor.cpp`) but arena mode does not. Users cannot experiment with different fitness landscapes without recompiling.

**Impact:** Significant UX improvement for arena mode experimentation.

**Fix plan:**
1. Add a "Fitness" tab to `ArenaPauseScreen` (following the pattern in `PauseConfigScreen`).
2. Use `ui::slider_float()` widgets for each weight.
3. Apply changes to the `config_` member on the `ArenaGameScreen`.

---

## [LOW] FighterDrillSession has no dead-ship penalty in scoring

**ID:** `IMP-019`

**Location:** `src/engine/fighter_drill_session.cpp:246-293`

**Description:** If a ship dies during a drill phase (e.g., collides with a tower), it simply stops accumulating score. There is no penalty for dying -- a ship that dies early with a positive score will keep that score. This can lead to evolution favoring reckless strategies that score quickly and then die, rather than sustained performance.

**Impact:** Better training signal for evolution, discouraging fragile strategies.

**Fix plan:**
Add a configurable `death_penalty` to `FighterDrillConfig` (default 0 for backward compat). In `resolve_ship_tower_collisions()`, when a ship dies, subtract the penalty:
```cpp
if (ships_[i].alive == false) {
    scores_[i] -= config_.death_penalty;
}
```

---

## [LOW] Feature idea: replay/recording system for arena matches

**ID:** `IMP-020`

**Location:** N/A (new feature)

**Description:** The architecture already supports deterministic replay: ArenaSession is seeded with a uint32_t, and all ship actions are set externally. Recording the seed + per-tick action vectors would enable match replay without storing every frame. This would allow:
- Reviewing best matches from a generation
- Comparing evolution progress across generations
- Sharing interesting matches

The existing `Snapshot` format could be extended with a `MatchReplay` struct containing the seed and action log.

**Impact:** Significant feature for understanding and debugging evolutionary progress in arena mode.

**Fix plan:**
1. Define `MatchReplay { uint32_t seed; ArenaConfig config; std::vector<std::vector<ActionSet>> tick_actions; }`.
2. In `tick_arena()`, optionally record each tick's actions to a buffer.
3. Add a "Replay" button to the arena pause screen that replays a saved match by feeding recorded actions back into a fresh ArenaSession.
4. Store replays as compact binary (seed + config + delta-encoded actions).

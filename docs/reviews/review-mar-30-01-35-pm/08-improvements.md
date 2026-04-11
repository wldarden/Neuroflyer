# Improvements & Ideas

Review date: 2026-03-30

---

## [HIGH] Cache `world_diag` once per tick instead of recomputing everywhere

**ID:** `IMP-001`
**Description:** `std::sqrt(world_width * world_width + world_height * world_height)` is computed independently in at least four places per tick: `compute_dir_range()` in `arena_sensor.cpp`, the NTM + squad leader block in `arena_match.cpp`, the identical block in `arena_game_screen.cpp`, and `compute_squad_stats()` in `arena_session.cpp`. Since the world dimensions never change during a match, the diagonal should be computed once and stored.
**Motivation:** This is not a performance concern in isolation (sqrt is cheap), but a correctness and maintainability concern. If someone ever introduces a resizable arena, having the value scattered across five call sites is a bug factory. A single `ArenaSession::world_diag()` accessor computed at construction would collapse all of these.
**Complexity:** Small
**Relevant code:** `src/engine/arena_match.cpp:103`, `src/engine/arena_sensor.cpp:106`, `src/ui/screens/arena/arena_game_screen.cpp:279`, `src/engine/arena_session.cpp:376,398`

---

## [HIGH] Add context to "Input size mismatch" exceptions

**ID:** `IMP-002`
**Description:** When `Network::forward()` throws "Input size mismatch: expected N got M", the crash is terminal and the error message gives no indication of *which* net type failed (NTM, squad leader, or fighter), which team it belongs to, or what the design parameters were. Wrap each `forward()` call site with a try/catch that appends context before rethrowing.
**Motivation:** This exact crash has already been reported in `docs/save-review-1.md`. When it happens in arena mode with three net types per team, the debugging surface area is enormous. Something like `"Fighter net forward failed for team 0 (expected 13 got 14): Input size mismatch"` would save significant time.
**Complexity:** Small -- add a helper or try/catch around the six `forward()` call sites in `arena_match.cpp`, `arena_game_screen.cpp`, and `squad_leader.cpp`.
**Relevant code:** `src/engine/arena_match.cpp:163`, `src/engine/squad_leader.cpp:89,137`, `src/ui/screens/arena/arena_game_screen.cpp:358`

---

## [HIGH] Incremental SectorGrid instead of full rebuild every tick

**ID:** `IMP-003`
**Description:** Both `arena_match.cpp` (headless) and `arena_game_screen.cpp` (visual) construct a fresh `SectorGrid`, clear it, and re-insert every alive entity every tick. The grid dimensions never change. Instead, make the grid a persistent member of `ArenaSession` and add an `update()` method that calls `clear()` + re-inserts. Even better, track entity sector membership and only move entities whose sector changed.
**Motivation:** With the default arena config (2 teams, 8 fighters each = 16 ships + 2 bases + 200 towers + 100 tokens = 318 entities), the full rebuild is fine. But the config UI lets users scale to 8 teams with 10 squads of 32 fighters each = 2560 fighters plus obstacles. At that scale, `SectorGrid` construction + 2500+ inserts per tick becomes real cost. Making the grid persistent eliminates the allocation and allows incremental movement tracking.
**Complexity:** Medium -- move `SectorGrid` to `ArenaSession` member, add `rebuild_grid()` method, remove per-tick construction from `arena_match.cpp` and `arena_game_screen.cpp`.
**Relevant code:** `src/engine/arena_match.cpp:54-67`, `src/ui/screens/arena/arena_game_screen.cpp:234-245`, `src/engine/sector_grid.cpp`

---

## [HIGH] Use the SectorGrid for arena collision detection

**ID:** `IMP-004`
**Description:** Collision resolution in `ArenaSession::resolve_bullet_ship_collisions()` is O(bullets * ships), `resolve_ship_tower_collisions()` is O(ships * towers), and `resolve_ship_token_collisions()` is O(ships * tokens). The SectorGrid exists but is only used for NTM threat gathering. These collision methods should query the grid for nearby candidates instead of iterating all entities.
**Motivation:** With 200 towers and 16 ships, the tower collision loop runs 3200 checks per tick. With 100 bullets, bullet-ship collision is 1600 checks. These are small numbers now, but the architecture already pays for the grid -- using it for collisions is nearly free and prepares for larger arenas. The real win is bullet-ship collisions, which can grow explosively (all ships shooting = hundreds of bullets).
**Complexity:** Medium -- extend SectorGrid to store entity type (ship/tower/token/base) or maintain separate grids per type. Refactor collision methods to query grid first.
**Relevant code:** `src/engine/arena_session.cpp:233-326`

---

## [HIGH] Snapshot round-trip test for v7 NetType field

**ID:** `IMP-005`
**Description:** The snapshot format was extended to v7 with a `NetType` field, but there is no test that verifies `NetType` survives a save/load round-trip. The existing tests cover v6 `paired_fighter_name` thoroughly, but v7 is untested. A snapshot saved with `NetType::Fighter` or `NetType::SquadLeader` should load back with the same value.
**Motivation:** Format version bugs are silent until someone loses data. The test takes five minutes to write and prevents a class of bug that is painful to debug in the field.
**Complexity:** Small -- add one test to `snapshot_io_test.cpp` that sets `snap.net_type = NetType::Fighter`, round-trips it, and asserts the value.
**Relevant code:** `tests/snapshot_io_test.cpp`, `src/engine/snapshot_io.cpp:99,163-165`

---

## [HIGH] Deduplicate the NTM + squad leader tick logic

**ID:** `IMP-006`
**Description:** The per-team NTM + squad leader computation block in `arena_game_screen.cpp:252-316` is a near-exact copy of the same block in `arena_match.cpp:74-122`. Both compute squad stats, gather threats, run NTM, find enemy base, compute heading/distance, and call `run_squad_leader()`. This is a maintenance hazard: any fix to one must be manually propagated to the other.
**Motivation:** This is the most important deduplication opportunity in the codebase. The headless match runner and the visual game screen must produce identical results, and divergence between them means evolution selects for behavior that differs from what the user observes. Extract a `compute_team_orders()` function.
**Complexity:** Medium -- extract the shared logic into a free function in a new or existing engine file.
**Relevant code:** `src/engine/arena_match.cpp:74-122`, `src/ui/screens/arena/arena_game_screen.cpp:252-316`

---

## [MEDIUM] Use `std::span` for arena sensor queries instead of full entity vectors

**ID:** `IMP-007`
**Description:** `query_arena_sensor()` already receives entities via `ArenaQueryContext` spans, but it checks ALL towers, ALL tokens, ALL ships, and ALL bullets for every sensor for every ship. With 16 ships and 4 sensors each, that is 64 sensor queries per tick, each iterating all entities. The SectorGrid could pre-filter entities to those within sensor range.
**Motivation:** For 200 towers, each sensor query does 200 ray-circle intersection tests. With 64 queries per tick, that is 12,800 tests on towers alone. Pre-filtering with the SectorGrid (which already exists) would reduce this to only nearby towers. The sensor range (typically 200-300 units) maps to a small grid diamond at 2000-unit sectors.
**Complexity:** Medium -- pass a pre-filtered entity list per ship (from grid query) instead of the full arena entity lists. Needs `ArenaQueryContext` to accept smaller spans.
**Relevant code:** `src/engine/arena_sensor.cpp:33-96`, `include/neuroflyer/arena_sensor.h`

---

## [MEDIUM] Avoid copying parent TeamIndividual during evolution

**ID:** `IMP-008`
**Description:** In `evolve_team_population()` and `evolve_squad_only()`, the child is created by copying the selected parent: `TeamIndividual child = population[best]`. Each `TeamIndividual` contains three `Individual` objects, each with a `StructuredGenome` (vectors of genes). This is a deep copy. The parent is never used after copying.
**Motivation:** With a population of 20 teams and 3 nets each, each evolution step makes ~18 deep copies (20 minus 2 elites). The genomes contain vectors of vectors. Using `std::move` is not straightforward since the parent needs to remain valid in the population for potential re-selection in later tournament rounds. However, after sorting and selection, the parent vector could be consumed. Alternatively, store indices first, then move.
**Complexity:** Medium -- refactor to collect child indices first, then move parents. Or accept the copy cost as insignificant for population size 20.
**Relevant code:** `src/engine/team_evolution.cpp:102,148`

---

## [MEDIUM] `constexpr` for SectorGrid dimension calculations

**ID:** `IMP-009`
**Description:** `ArenaConfig::population_size()` is already `constexpr`-ready but the config itself is not constexpr. More usefully, several computations in arena code use magic formulas that could be `constexpr` functions: the sensor input size calculation (`compute_arena_input_size`), output size, and NTM input size. These are compile-time-knowable for fixed designs.
**Motivation:** Making these `constexpr` would allow compile-time validation of network sizes and potentially static array sizing. More importantly, it documents that these values are deterministic functions of their inputs.
**Complexity:** Small -- mark `compute_arena_input_size()` and `compute_output_size()` as `constexpr` (they only use arithmetic and loop over sensors).
**Relevant code:** `src/engine/arena_sensor.cpp:117-123`, `include/neuroflyer/ship_design.h`

---

## [MEDIUM] Arena training feedback metrics

**ID:** `IMP-010`
**Description:** The arena info panel shows basic stats (generation, time, ships alive, per-team scores). For understanding training progress, the user needs more: best fitness over generations, average fitness trend, a fitness graph over time. The scroller mode has generation stats (`GenStats` in `AppState`); arena mode has no equivalent history tracking.
**Motivation:** Without a fitness curve, the user cannot tell if evolution is making progress, stagnating, or regressing. This is the single most important UX gap for arena mode. A simple rolling window of `(generation, best_fitness, avg_fitness, worst_fitness)` stored on `ArenaGameScreen` and rendered as a sparkline in the info panel would be transformative.
**Complexity:** Medium -- add a `std::vector<GenerationStats>` to `ArenaGameScreen`, push stats after each evolution step, render as ImGui sparkline or mini-chart in the info panel.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:371-399`, `src/ui/views/arena_game_info_view.cpp`

---

## [MEDIUM] NTM test for edge cases: all-same-score threats

**ID:** `IMP-011`
**Description:** `run_ntm_threat_selection()` picks the threat with the highest score. If all threats produce the same score (possible with certain weight initializations or when threats are equidistant), the function returns the last one iterated. This is deterministic but order-dependent. There are no tests for this edge case or for the single-threat case (trivially correct but worth asserting).
**Motivation:** The NTM is the most novel architectural piece. Edge cases in threat selection directly affect squad behavior: if two threats tie, the "winner" depends on iteration order, which depends on SectorGrid layout. Adding a test that asserts deterministic behavior for equal scores prevents subtle regressions.
**Complexity:** Small -- add 2 test cases to `squad_leader_test.cpp`: single threat (verify it is selected) and N identical threats at the same position (verify consistent selection).
**Relevant code:** `src/engine/squad_leader.cpp:61-105`, `tests/squad_leader_test.cpp`

---

## [MEDIUM] Visualize squad leader orders on the game view

**ID:** `IMP-012`
**Description:** When following a ship in arena mode, the net viewer shows the raw neural net activations, but there is no visual representation of what the squad leader is ordering. Draw an arrow from the squad centroid to the target position (attack target or home base), color-coded by tactical order (red for attack, blue for defend). Show the spacing order as a contracting/expanding circle around the centroid.
**Motivation:** The hierarchical brain architecture is the core differentiator of arena mode, but it is invisible to the user. You can see the fighter's sensors and outputs, but the strategic layer is a black box. Visualizing squad orders would make the evolved behavior legible and the training process dramatically more interesting to watch.
**Complexity:** Medium -- add a `render_squad_orders()` method to `ArenaGameView` that takes `SquadLeaderOrder` and squad centroid. Draw in world coordinates.
**Relevant code:** `src/ui/views/arena_game_view.cpp`, `include/neuroflyer/squad_leader.h`

---

## [MEDIUM] Speed slider instead of keyboard-only controls

**ID:** `IMP-013`
**Description:** Speed controls in arena mode are keyboard-only (1-4 keys for 1x/5x/20x/100x). Add an ImGui slider in the info panel that allows continuous speed control (1-200x) and shows the current speed value. Keep the keyboard shortcuts as accelerators.
**Motivation:** The 1x-to-5x jump is too large for observing specific behaviors. The 20x-to-100x jump misses the sweet spot for fast training while still being able to see what happens. A slider gives fine-grained control and makes the current speed visible.
**Complexity:** Small -- add `ui::slider_int("Speed", &ticks_per_frame_, 1, 200)` to the info panel.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:210-214`, `src/ui/views/arena_game_info_view.cpp`

---

## [MEDIUM] Add optional per-generation logging for evolution diagnostics

**ID:** `IMP-014`
**Description:** `evolve_team_population()` and `evolve_squad_only()` are pure functions that return the next generation with no diagnostic output. Add an optional stats struct that reports: fitness distribution (min/max/mean/median), number of topology mutations that occurred, number of unique topologies in the population, and genome diversity (measured by weight variance).
**Motivation:** When training stalls, the user has no way to diagnose why. Is the population converging too fast (diversity collapsed)? Are topology mutations producing non-viable nets? Is the fitness landscape flat? These diagnostics would answer those questions and guide config tuning.
**Complexity:** Medium -- add an optional `EvolutionStats*` parameter to the evolve functions, or return a struct alongside the population.
**Relevant code:** `src/engine/team_evolution.cpp:76-153`, `src/engine/evolution.cpp:604+`

---

## [MEDIUM] Arena replay system: save match state for analysis

**ID:** `IMP-015`
**Description:** Implement a tick-by-tick state recorder that captures ship positions, alive states, bullet positions, team orders, and fitness components for each tick of a match. Save as a compact binary or JSON file. Implement a replay viewer that plays back the recording at variable speed.
**Motivation:** Evolved behavior is emergent and often surprising. Being able to replay a specific match -- especially a match where one team discovered an interesting strategy -- is essential for understanding what evolution found. The current system is ephemeral: you watch it live or not at all. This is the single most impactful new feature for research and understanding.
**Complexity:** Large -- requires a serialization format for per-tick state, a recording class, a replay viewer screen, and file I/O. The ArenaSession already exposes all the needed state via const accessors.
**Relevant code:** `include/neuroflyer/arena_session.h` (state accessors), `src/ui/screens/arena/arena_game_screen.cpp` (tick loop)

---

## [MEDIUM] Structured bindings for coordinate pairs

**ID:** `IMP-016`
**Description:** The codebase already uses structured bindings in some places (e.g., `auto [sx, sy] = camera.world_to_screen(...)` in `arena_game_view.cpp`) but not in others. Several places manually extract x/y from pair-like returns or construct DirRange results field-by-field. Adopt structured bindings more consistently.
**Motivation:** Code like `float home_dx = own_base_x - stats.centroid_x, home_dy = own_base_y - stats.centroid_y` followed by distance calculation is a recurring 5-line pattern. A `direction_and_distance()` helper returning a structured result with structured binding at the call site would reduce the NTM+squad leader block from ~30 lines to ~15.
**Complexity:** Small -- this is a refactoring pass, no behavior change.
**Relevant code:** `src/engine/arena_match.cpp:95-114`, `src/ui/screens/arena/arena_game_screen.cpp:268-290`

---

## [MEDIUM] Debug mode sector grid visualization

**ID:** `IMP-017`
**Description:** Add an optional debug overlay in the arena game view that draws the sector grid lines and highlights sectors that contain entities. Color sectors by entity density. Show the NTM diamond radius around the followed ship's squad centroid.
**Motivation:** The sector grid is invisible but critical to NTM behavior. If the grid is too coarse, the NTM sees too many threats and wastes compute. If too fine, it misses nearby threats. Visualizing the grid helps tune `sector_size` and `ntm_sector_radius`. A debug hotkey (e.g., G) to toggle this would be sufficient.
**Complexity:** Medium -- add a `render_debug_grid()` method to `ArenaGameView` that draws grid lines in world coordinates via the camera transform.
**Relevant code:** `src/ui/views/arena_game_view.cpp`, `include/neuroflyer/sector_grid.h`

---

## [MEDIUM] Indicate which NTM threat is selected in the net viewer

**ID:** `IMP-018`
**Description:** When viewing the squad leader net in follow mode, the NTM threat selection result flows into the squad leader as 4 inputs (active, heading_sin, heading_cos, distance, threat_score). But the user cannot see *where* the selected threat is on the map or *which* entity was chosen. Draw a marker on the selected threat entity in the game view and annotate the NTM inputs in the net viewer.
**Motivation:** The NTM is the most interesting evolved component -- it learns to score threats. But the output is invisible: you see 4 numbers in the squad leader input, with no spatial reference. A red crosshair on the selected threat entity would instantly communicate what the NTM decided.
**Complexity:** Medium -- capture the `NtmResult` for the followed ship's team (already partially captured as `last_leader_input_`) and pass `target_x/target_y` to the game view for rendering.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:263-266,300-315`, `src/ui/views/arena_game_view.cpp`

---

## [LOW] Use `std::ranges::sort` and range algorithms

**ID:** `IMP-019`
**Description:** The codebase uses `std::sort(container.begin(), container.end(), ...)` in many places. C++20 ranges allow `std::ranges::sort(container, ...)` which is more concise and harder to misuse (no iterator pair mismatch).
**Motivation:** Purely a modernization pass. No behavior change. Makes the code marginally more readable.
**Complexity:** Small -- find-and-replace across ~15 call sites.
**Relevant code:** All `std::sort` calls listed in grep results across `src/engine/` and `src/ui/`

---

## [LOW] `partial_sort` for evolution elitism

**ID:** `IMP-020`
**Description:** `evolve_team_population()` and `evolve_squad_only()` call `std::sort()` on the entire population, but only the top `elitism_count` (default 2-3) individuals need to be identified in sorted order. The rest of the population is accessed randomly by tournament selection. `std::partial_sort(begin, begin + elitism_count, end, ...)` would be sufficient and theoretically faster.
**Motivation:** For population size 20, this is irrelevant. For larger populations (which the config allows), `partial_sort` is O(N log K) vs O(N log N). More importantly, it documents the intent: "we only care about the top K."
**Complexity:** Small -- replace `std::sort` with `std::partial_sort` in 3 places. Verify tournament selection still works correctly (it indexes into the full population, which is fine -- partial_sort puts the top K first but the rest are still there).
**Relevant code:** `src/engine/team_evolution.cpp:81,121`, `src/engine/evolution.cpp:604`

---

## [LOW] Squad leader decision log for analysis

**ID:** `IMP-021`
**Description:** Record the squad leader's tactical and spacing orders over time as a time series: `(tick, team, tactical_order, spacing_order, target_x, target_y)`. Display as a timeline in the info panel showing when a team switched between attack/defend, expanded/contracted. Optionally save to disk for post-hoc analysis.
**Motivation:** Understanding *when* a team transitions between strategies is critical for evaluating evolved behavior. Did the squad leader learn to attack when healthy and retreat when damaged? The current system shows only the instantaneous order -- no history. A simple circular buffer of the last N=300 orders (5 seconds at 60fps) rendered as a color-coded timeline would suffice.
**Complexity:** Medium -- add a ring buffer to `ArenaGameScreen`, populate during tick, render as ImGui colored rectangles in the info panel.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:292-297`, `src/ui/views/arena_game_info_view.cpp`

---

## [LOW] Comparative fitness charts across generations

**ID:** `IMP-022`
**Description:** Track and display best/average fitness for each team across generations as a line chart. Use ImGui's `PlotLines` or a custom sparkline renderer. This would appear in the info panel alongside the per-team scores.
**Motivation:** A fitness chart is the standard tool for monitoring neuroevolution progress. The scroller mode implicitly tracks this via generation stats. Arena mode needs the same, but per-team. Seeing two team fitness curves diverge or converge tells you whether coevolution is working.
**Complexity:** Medium -- store `std::vector<float>` per team for best/avg fitness, push after each generation, render with `ImGui::PlotLines()`.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:371-399`, `src/ui/views/arena_game_info_view.cpp`

---

## [LOW] Multiple squad support in arena game screen

**ID:** `IMP-023`
**Description:** The architecture supports `num_squads > 1` (ArenaConfig, team/squad assignment, squad stats), but `arena_game_screen.cpp` and `arena_match.cpp` only compute squad stats and run the squad leader for squad 0 of each team: `compute_squad_stats(team, 0)`. Supporting multiple squads would require running NTM + squad leader per squad rather than per team, and assigning different orders to fighters in different squads.
**Motivation:** Multiple squads per team is the key architectural advantage of the hierarchical brain system. A team with 2 squads could have one attacking and one defending simultaneously. The data structures are ready; only the tick loop logic needs generalization.
**Complexity:** Large -- requires looping over squads within teams for NTM/leader computation, routing the correct leader orders to the correct fighters, and potentially separate squad leader nets per squad (or shared weights with different inputs).
**Relevant code:** `src/engine/arena_match.cpp:74-122`, `src/ui/screens/arena/arena_game_screen.cpp:252-316`, `include/neuroflyer/arena_config.h:42-43`

---

## [LOW] Arena matchmaking: round-robin or Swiss pairing

**ID:** `IMP-024`
**Description:** Currently `arena_game_screen.cpp` always pits team genomes 0 vs 1 (`current_team_indices_ = {0, 1}`). With a population of 20 team genomes, 18 never get match experience each generation. Implement round-robin pairing so all genomes compete, or Swiss pairing so similar-fitness genomes face each other for better selection pressure.
**Motivation:** Without proper matchmaking, evolution selects for performance against a single opponent each generation, which encourages overspecialization and fragile strategies. Round-robin or Swiss pairing would dramatically improve generalization. This is listed in the backlog as item #11.
**Complexity:** Medium -- implement a matchmaking scheduler that generates pairs for each round, track cumulative fitness across rounds, run the configured `rounds_per_generation` with different pairs.
**Relevant code:** `src/ui/screens/arena/arena_game_screen.cpp:123,561-612`

---

## [LOW] `std::expected` or error codes for snapshot loading

**ID:** `IMP-025`
**Description:** Snapshot load errors currently throw `std::runtime_error`. The error messages are decent for file-level issues (path included for file-not-found) but the stream-based overloads produce generic messages like "Unexpected end of stream" with no context about *where* in the parse it failed or which field was being read. Consider using `std::expected<Snapshot, SnapshotError>` (C++23) or a `LoadResult` struct to provide structured error information.
**Motivation:** When a snapshot fails to load, the user sees a crash. If the error were returned as a value, the UI could show a dialog with the specific error rather than terminating. The stream-based overloads are called from the file-based overloads, which do include the path -- but the inner parse errors lose that context in the rethrow chain.
**Complexity:** Medium -- requires changing the return type of `load_snapshot()` and all call sites. Could be done incrementally by wrapping the file-based overloads first.
**Relevant code:** `src/engine/snapshot_io.cpp:271-341`, `include/neuroflyer/snapshot_io.h`

---

## [LOW] Bullet-triangle collision uses non-rotated vertices

**ID:** `IMP-026`
**Description:** `bullet_triangle_collision()` in `collision.h` checks the bullet against three vertex positions computed from `tri.x, tri.y` with fixed offsets (top, bottom-left, bottom-right). These are the unrotated vertices. In arena mode, ships have rotation (the render code correctly draws rotated triangles), but the collision code uses the axis-aligned hitbox. `arena_session.cpp:243-248` adds a center-distance fallback to compensate, but this makes the hitbox effectively circular.
**Motivation:** This is a known compromise (the center-distance fallback is documented in the code), but it means the collision shape does not match the visual shape. Fighters that learn to dodge based on sensor readings may develop strategies that exploit the mismatch. Using rotated vertex positions would make collisions match visuals, improving both fairness and legibility.
**Complexity:** Medium -- modify `bullet_triangle_collision()` and `triangle_circle_collision()` to accept rotation and compute rotated vertex positions. Requires changing the calling code to pass rotation.
**Relevant code:** `include/neuroflyer/collision.h:43-61`, `src/engine/arena_session.cpp:233-263`

---

## [LOW] NTM forward reuse: cache threat scores across calls

**ID:** `IMP-027`
**Description:** `run_ntm_threat_selection()` calls `ntm_net.forward()` once per threat. If the same threat entity appears in multiple squads' diamond queries (which happens when squads of the same team overlap spatially), the same NTM evaluation is performed redundantly. A cache keyed on `(threat_entity_id, squad_center)` could eliminate duplicates.
**Motivation:** Currently with 1 squad per team, there is no overlap. With multiple squads per team (backlog item #9/23), overlapping diamonds would be common. This optimization becomes relevant only when multi-squad support lands, but designing for it now would simplify the later implementation.
**Complexity:** Small (if applied at the NTM level) -- add a `std::unordered_map<size_t, float>` per tick for threat scores. Clear each tick. Check before calling forward.
**Relevant code:** `src/engine/squad_leader.cpp:73-91`

---

## [LOW] Snapshot load error messages should include file path consistently

**ID:** `IMP-028`
**Description:** The file-based `load_snapshot(const std::string& path)` overload does include the path in the "Cannot open file" error, but if the stream-based parse fails (bad CRC, unexpected end of stream), the path is lost because the exception is thrown from the inner stream-based function. The file-based wrapper should catch and rethrow with path context.
**Motivation:** When loading variants in the hangar, a corrupt file produces "Snapshot CRC mismatch: file is corrupted" with no indication of *which* file. There could be dozens of variant files. Wrapping with the path would immediately identify the problem file.
**Complexity:** Small -- add try/catch in the file-based overload that catches runtime_error and rethrows with path prepended.
**Relevant code:** `src/engine/snapshot_io.cpp:327-333`

---

## [LOW] `convert_variant_to_fighter` -- test sight-only sensor designs

**ID:** `IMP-029`
**Description:** The existing tests for `convert_variant_to_fighter` cover full sensors and a mix of full+sight sensors, but none test a design with only sight sensors (1 value per sensor instead of 4-5). The weight mapping logic has different paths for `is_full_sensor` vs not, and the sight-only path is simpler but untested in isolation.
**Motivation:** Sight-only designs are a valid configuration. Testing them ensures the column arithmetic (which is the most bug-prone part of the conversion) is correct for both paths independently.
**Complexity:** Small -- add one test to `evolution_test.cpp` with a design containing 3 sight sensors and no full sensors.
**Relevant code:** `tests/evolution_test.cpp`, `src/engine/evolution.cpp:241-317`

---

## [LOW] Arena pause screen / mid-match configuration

**ID:** `IMP-030`
**Description:** The scroller mode has a rich pause screen (`PauseConfigScreen`) with tabs for training settings, evolution parameters, analysis, and saving variants. Arena mode has only Space-to-pause with no configuration screen. There is a design spec for this at `docs/superpowers/specs/2026-03-30-arena-pause-screen-design.md`, but the implementation is not started.
**Motivation:** Without a pause screen, the user cannot adjust evolution parameters, save promising team genomes, or inspect detailed match statistics mid-training. This is a significant UX gap between scroller and arena modes.
**Complexity:** Large -- requires a new screen following the UIScreen pattern, with tabs for arena-specific settings (fitness weights, matchmaking, evolution params) and a save mechanism for team genomes.
**Relevant code:** `docs/superpowers/specs/2026-03-30-arena-pause-screen-design.md`, `src/ui/screens/arena/arena_game_screen.cpp:217-219`

---

## [LOW] `teams_alive()` uses `std::set` per call

**ID:** `IMP-031`
**Description:** `ArenaSession::teams_alive()` constructs a `std::set<int>` and iterates all ships to find unique alive teams. This is called from `check_end_conditions()` every tick. With small team counts (2-8), a bitset or simple counter would be more cache-friendly and avoid heap allocation.
**Motivation:** `std::set` is a red-black tree with per-node allocation. For 2-8 teams, a `uint8_t` bitmask or `std::bitset<8>` checked with a single pass over ships would be faster and allocation-free. Not a bottleneck, but a simple cleanup.
**Complexity:** Small -- replace `std::set<int>` with a local `uint8_t bits = 0` and set `bits |= (1 << team)` per alive ship.
**Relevant code:** `src/engine/arena_session.cpp:441-449`

# New Feature Suggestions — Mar 30, 01:35 PM

> Ideas that emerged during the overnight review. None of these are urgent.
> Review at your leisure and pick up anything that sounds interesting.

---

## 1. Cache world_diagonal for Performance (IMP-001)

**Motivation:** `sqrt(w*w + h*h)` computed in 4+ locations every tick. Trivial to cache once on ArenaConfig.

**What it would involve:** Add `world_diagonal()` method to ArenaConfig. Replace all inline computations.

**Complexity:** Small

**Relevant existing code:** `arena_config.h`, `arena_sensor.cpp`, `arena_match.cpp`, `arena_game_screen.cpp`, `arena_session.cpp`

---

## 2. Better Error Messages for Input Size Mismatch (IMP-002)

**Motivation:** The "Input size mismatch: expected X got Y" crash is the #1 reported bug. Adding context (which net type, which team, actual input layout) would make debugging dramatically faster.

**What it would involve:** Wrap `network.forward()` calls with try/catch that adds context. Or add a pre-check before forward() that prints a diagnostic.

**Complexity:** Small

**Relevant existing code:** `arena_game_screen.cpp:358`, neuralnet library `network.cpp`

---

## 3. Incremental SectorGrid Updates (IMP-003)

**Motivation:** Grid rebuilt from scratch every tick (1066 vector allocations). With incremental update, only moved entities need reinsertion.

**What it would involve:** Add `update(entity_id, old_x, old_y, new_x, new_y)` to SectorGrid. Track per-entity sector assignments.

**Complexity:** Medium

**Relevant existing code:** `sector_grid.h/cpp`, `arena_game_screen.cpp:235-245`

---

## 4. Use SectorGrid for Arena Collisions (IMP-004)

**Motivation:** Arena sensor queries check ALL entities linearly (O(ships + towers + tokens + bullets) per sensor per ship). The sector grid already exists — it could be used to narrow the search space for sensor queries and collision detection too.

**What it would involve:** Separate grids (or entity-type-aware grid) for ships, towers, tokens, bullets. Modify `query_arena_sensor()` to query nearby sectors instead of iterating all entities.

**Complexity:** Medium-Large

**Relevant existing code:** `sector_grid.h/cpp`, `arena_sensor.cpp`

---

## 5. Snapshot v7 Round-Trip Test (IMP-005)

**Motivation:** Snapshot format v7 added `net_type` field. No round-trip test verifies it survives save/load.

**What it would involve:** Add test: create snapshot with `net_type = NetType::SquadLeader`, save, load, verify.

**Complexity:** Small

**Relevant existing code:** `tests/snapshot_io_test.cpp`, `src/engine/snapshot_io.cpp`

---

## 6. Deduplicate NTM+Squad Leader Tick Logic (IMP-006)

**Motivation:** The 130-line arena tick loop (NTM → squad leader → fighter inputs → forward → decode) exists in both `arena_game_screen.cpp` and `arena_match.cpp`. They will drift.

**What it would involve:** Extract into a shared `tick_arena_frame()` function. The screen version adds visualization callbacks.

**Complexity:** Medium

**Relevant existing code:** `arena_game_screen.cpp:229-367`, `arena_match.cpp:12-240`

---

## 7. Arena Training Fitness Charts (IMP-010)

**Motivation:** Users can't see if training is making progress. A rolling fitness graph (best/avg/worst per generation) is essential for understanding training dynamics.

**What it would involve:** Store per-generation fitness stats. Render a line chart in the arena info panel or a new analysis tab.

**Complexity:** Medium

**Relevant existing code:** `arena_game_screen.cpp` (do_arena_evolution), `arena_game_info_view.cpp`

---

## 8. Squad Leader Order Visualization (IMP-012)

**Motivation:** During arena follow mode, users can't see what the squad leader is ordering. Visualizing the target arrow and spacing order on the game view would make the hierarchical brain observable.

**What it would involve:** Draw an arrow from squad center to the order target. Color-code by tactical order (red=attack, blue=defend). Show expand/contract indicator.

**Complexity:** Small-Medium

**Relevant existing code:** `arena_game_view.cpp`, `squad_leader.h` (SquadLeaderOrder)

---

## 9. NTM Threat Marker on Map (IMP-018)

**Motivation:** The NTM selects one "active threat" but there's no visual indicator of which enemy is being tracked. A highlight or marker on the active threat would help users understand the squad leader's decision-making.

**What it would involve:** Pass `NtmResult.target_x/y` to the game view renderer. Draw a marker circle around the active threat entity.

**Complexity:** Small

**Relevant existing code:** `arena_game_view.cpp`, `squad_leader.h` (NtmResult)

---

## 10. Speed Slider Instead of Keyboard Shortcuts (IMP-013)

**Motivation:** Speed controls (1x/5x/20x/100x) are keyboard-only. A slider in the UI would be more discoverable and allow fine-grained control.

**What it would involve:** Add an ImGui slider to the arena info panel. Map to `ticks_per_frame_`.

**Complexity:** Small

**Relevant existing code:** `arena_game_screen.cpp:211-214`, `arena_game_info_view.cpp`

---

## 11. Evolution Diagnostics Per Generation (IMP-014)

**Motivation:** No logging of population diversity, topology distribution, or fitness statistics per generation. Hard to diagnose when evolution is stuck or diverging.

**What it would involve:** Log best/avg/worst fitness, topology histogram, NTM/squad/fighter weight magnitude stats. Optional verbose mode.

**Complexity:** Small-Medium

**Relevant existing code:** `arena_game_screen.cpp:371-398` (do_arena_evolution), `StructuralHistogram` in `evolution.h`

---

## 12. Arena Replay System (IMP-015)

**Motivation:** Interesting arena matches can't be reviewed after they happen. A replay system would enable post-hoc analysis of evolved behavior.

**What it would involve:** Record per-tick ship positions, orders, NTM selections, fitness events. Save to file. Add replay playback mode.

**Complexity:** Large

**Relevant existing code:** `arena_session.cpp` (tick state), `arena_game_screen.cpp`

---

## 13. Debug Sector Grid Visualization (IMP-017)

**Motivation:** The sector grid is invisible. Visualizing grid lines and entity assignments in debug mode would help verify NTM spatial indexing is working correctly.

**What it would involve:** Draw grid lines in arena_game_view when a debug flag is toggled. Highlight the NTM diamond around each squad center.

**Complexity:** Small

**Relevant existing code:** `arena_game_view.cpp`, `sector_grid.h`

---

## 14. NTM Edge Case Tests (IMP-011)

**Motivation:** Current NTM tests don't cover: zero threats, all threats with identical scores, single threat, threats at grid boundaries.

**What it would involve:** Add parameterized tests to `squad_leader_test.cpp`.

**Complexity:** Small

**Relevant existing code:** `tests/squad_leader_test.cpp`

---

## 15. Rotated Collision Shapes for Arena (IMP-026)

**Motivation:** Ships rotate freely in arena but collision detection assumes upward-facing. Ships can fly through towers they appear to hit.

**What it would involve:** Rotation-aware vertex computation in `collision.h`. New `bullet_triangle_collision_rotated()` variant.

**Complexity:** Small-Medium

**Relevant existing code:** `include/neuroflyer/collision.h`, `src/engine/arena_session.cpp`

---

## 16. Multi-Squad Support (IMP-023)

**Motivation:** The architecture already supports it (num_squads config, squad_of() method, per-squad stats). Just needs the tick loop to iterate squads.

**What it would involve:** Change the tick loop to run squad leader per squad (not per team). Assign ships to squads. Commander net dispatches to squad leaders.

**Complexity:** Medium-Large

**Relevant existing code:** `arena_config.h` (num_squads), `arena_session.h` (squad_of), `squad_leader.h`

---

## 17. Matchmaking Improvements (IMP-024)

**Motivation:** Currently hardcoded to team genomes 0 vs 1. Swiss-style or round-robin matchmaking would improve selection pressure.

**What it would involve:** Generate match pairings each round. Track per-genome match count. Normalize fitness by matches played.

**Complexity:** Medium

**Relevant existing code:** `arena_game_screen.cpp:123` (current_team_indices_), backlog item #11

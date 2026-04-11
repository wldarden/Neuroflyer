# New Feature Suggestions — Mar 30, 04:46 PM

> Ideas that emerged during the overnight review. None of these are urgent.
> Review at your leisure and pick up anything that sounds interesting.

## 1. Spatial Culling for Arena Sensor Queries

**ID:** `IMP-001`

**Motivation:** Arena sensor queries are O(ships * sensors * entities) per tick with no spatial filtering. The SectorGrid already exists for NTM but is unused by sensors. At 100x speed, sensor queries dominate frame time.

**What it would involve:** Insert towers/tokens/bullets into the SectorGrid. For each sensor query, compute bounding sectors from max range and only test nearby entities. Pure performance change, no behavioral difference.

**Complexity:** Medium

**Relevant existing code:** `src/engine/arena_sensor.cpp`, `include/neuroflyer/sector_grid.h`, `src/ui/screens/arena/arena_game_screen.cpp:266-277`

---

## 2. Spatial Culling for Bullet-Ship Collisions

**ID:** `IMP-002`

**Motivation:** `resolve_bullet_ship_collisions()` is O(bullets * ships) with no spatial partitioning. Will scale poorly with larger populations.

**What it would involve:** Use SectorGrid for bullet-ship collision phase. For each bullet, only check ships in nearby sectors.

**Complexity:** Small

**Relevant existing code:** `src/engine/arena_session.cpp:234-262`

---

## 3. Pre-allocated Input Vectors for Arena Sensor Queries

**ID:** `IMP-004`

**Motivation:** `build_arena_ship_input()` allocates a new `std::vector<float>` per ship per tick. At 100x speed with 16 ships, that's 1600 heap allocations per frame.

**What it would involve:** Add a `build_arena_ship_input_into(std::span<float>, ...)` variant. Pre-allocate buffer once in `tick_arena()` and reuse.

**Complexity:** Small

**Relevant existing code:** `src/engine/arena_sensor.cpp:226-269`

---

## 4. Allocation-free SectorGrid Query

**ID:** `IMP-005`

**Motivation:** `entities_in_diamond()` allocates a new vector each call. If spatial indexing is adopted for sensors (IMP-001), it would be called 128+ times per tick.

**What it would involve:** Add overload that appends to a caller-provided vector. Caller clears and reuses a scratch vector.

**Complexity:** Small

**Relevant existing code:** `src/engine/sector_grid.cpp:34-49`

---

## 5. Extract Shared Session Physics

**ID:** `IMP-008`

**Motivation:** `FighterDrillSession` and `ArenaSession` duplicate ~200 lines of collision/physics code. Bug fixes must be applied in both files. Already diverged on bullet boundary behavior.

**What it would involve:** Create `session_physics.h/.cpp` with free functions for boundary wrapping, bullet spawning, collision resolution. Both sessions call shared helpers.

**Complexity:** Medium

**Relevant existing code:** `src/engine/arena_session.cpp:176-325`, `src/engine/fighter_drill_session.cpp:134-244`

---

## 6. Persistent SectorGrid Between Ticks

**ID:** `IMP-012`

**Motivation:** A new SectorGrid is constructed and populated every tick, allocating cell vectors. Grid dimensions never change within a match.

**What it would involve:** Make SectorGrid a member of ArenaGameScreen. Call `clear()` + `insert()` instead of constructing a new one.

**Complexity:** Small

**Relevant existing code:** `src/ui/screens/arena/arena_game_screen.cpp:266-277`

---

## 7. Crossover in Team Evolution

**ID:** `IMP-014`

**Motivation:** Team evolution only uses asexual reproduction (tournament select + mutation). Solo mode already does same-topology crossover. Adding crossover to team evolution could accelerate convergence.

**What it would involve:** For each sub-net (NTM, squad leader, fighter), if two tournament-selected parents have the same topology, apply `evolve::crossover()`. Fall back to mutation-only if topologies differ.

**Complexity:** Medium

**Relevant existing code:** `src/engine/team_evolution.cpp:81-158`, `src/engine/evolution.cpp:601-667` (existing crossover pattern)

---

## 8. Arena Fitness Weight UI

**ID:** `IMP-018`

**Motivation:** Arena fitness weights (base_damage, survival, ships_alive, tokens) are hardcoded defaults with no UI exposure. Solo mode has a fitness editor but arena mode does not. Users can't experiment with different fitness landscapes without recompiling.

**What it would involve:** Add a "Fitness" tab to ArenaPauseScreen with `ui::slider_float()` widgets for each weight.

**Complexity:** Small

**Relevant existing code:** `include/neuroflyer/arena_config.h:63-66`, `src/ui/views/fitness_editor.cpp` (solo mode pattern)

---

## 9. Configurable Death Penalty for Fighter Drill

**ID:** `IMP-019`

**Motivation:** Dead ships keep their accumulated score with no penalty. Evolution can favor reckless burst-and-die strategies. Related to STALE-004 in the fix sprint.

**What it would involve:** Add `death_penalty` config field and subtract on death. Default 0 for backward compat.

**Complexity:** Small

**Relevant existing code:** `src/engine/fighter_drill_session.cpp:246-293`

---

## 10. Arena Match Replay System

**ID:** `IMP-020`

**Motivation:** ArenaSession is already deterministic (seeded RNG) and all ship actions are set externally. Recording seed + per-tick action vectors would enable match replay at minimal cost, allowing review of best matches and comparison across generations.

**What it would involve:**
1. Define `MatchReplay { seed, config, tick_actions }` struct
2. Optionally record actions in `tick_arena()`
3. Add "Replay" button to arena pause screen
4. Store replays as compact binary

**Complexity:** Large

**Relevant existing code:** `src/engine/arena_session.cpp`, `src/ui/screens/arena/arena_pause_screen.cpp`

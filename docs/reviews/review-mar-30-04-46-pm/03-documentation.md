# Documentation Gaps

> **Scope:** Full NeuroFlyer project. Primary focus on recently added features: Arena mode (squad leaders, NTM, fighter inputs, arena pause screen), Fighter Drill mode (FighterDrillSession, FighterDrillScreen, FighterDrillPauseScreen), expanded collision.h, and DrillPhase-based scoring. Compared CLAUDE.md, docs/feature-audit.md, docs/backlog.md, docs/arena_mode.md, and docs/engine-architecture.md against actual source files.
> **Findings:** 5 critical, 6 high, 7 medium, 5 low

---

## [CRITICAL] Fighter Drill mode entirely missing from CLAUDE.md

**ID:** `DOC-001`

**Location:** `CLAUDE.md` -- no mention anywhere

**Description:** Fighter Drill is a complete new game mode with 6 new files (engine header, engine source, 3 UI screens, 1 test file). It introduces `DrillPhase` (Expand/Contract/Attack/Done), `FighterDrillConfig`, `FighterDrillSession`, `FighterDrillScreen`, and `FighterDrillPauseScreen`. None of these appear in CLAUDE.md's file tree, screen flow, architecture section, controls table, or any other section. The entry point from VariantViewerScreen ("Fighter Drill" button) is also undocumented.

**Impact:** A future session working on fighter drill improvements, bug fixes, or extensions would have no documentation to orient from. The drilling concept (scripted squad inputs through timed phases to train fighters) is a novel pattern not used elsewhere in the codebase.

**Fix plan:** Add a new section to CLAUDE.md after the Arena Mode section:

```markdown
## Fighter Drill Mode

A specialized training mode for fighter nets. Instead of a real evolved squad leader, the system injects **scripted squad leader inputs** through three timed phases (Expand, Contract, Attack). 200 individual fighters are scored on how well they follow commands, then evolved using the same individual-based evolution as the scroller.

### Architecture

- **FighterDrillSession** (`fighter_drill_session.h` / `fighter_drill_session.cpp`) -- pure engine class. Manages a small arena world (4000x4000) with ships, towers, tokens, one enemy starbase, and bullets. Tracks `DrillPhase` enum (Expand/Contract/Attack/Done) with configurable phase timing.
- **FighterDrillScreen** (`ui/screens/fighter_drill_screen.h` / `game/fighter_drill_screen.cpp`) -- UIScreen driving the training loop. Computes scripted squad inputs per phase, runs arena sensors via `build_arena_ship_input()`, forwards fighter nets, decodes outputs, ticks the session, and renders via direct SDL (same pattern as ArenaGameView).
- **FighterDrillPauseScreen** (`ui/screens/fighter_drill_pause_screen.h` / `game/fighter_drill_pause_screen.cpp`) -- pause overlay with two tabs: Evolution (mutation rates) and Save Variants (multi-select fighters to save as named variants).

### Entry Point

From the Variant Viewer screen, select a fighter variant and click "Fighter Drill." The variant is converted to a fighter net (if not already) via `convert_variant_to_fighter()`, then 200 mutated copies are seeded.

### Drill Phases

Three sequential phases, 20 seconds each (1200 ticks at 60fps):

| Phase | Scripted Squad Inputs | Scoring |
|-------|----------------------|---------|
| Expand | spacing=+1, aggression=0, no target | Movement away from squad center |
| Contract | spacing=-1, aggression=0, no target | Movement toward squad center |
| Attack | spacing=0, aggression=+1, target=starbase | Movement toward starbase + bullet hits |

All scoring is velocity-dot-product-based (per-tick), not position-based.

### Evolution

Individual-based (same as scroller). Uses `evolve_population()` directly -- no TeamIndividual wrappers.
```

---

## [CRITICAL] Arena Pause Screen not documented

**ID:** `DOC-002`

**Location:** `CLAUDE.md` -- Screen Flow section, file tree, and Existing Implementations table

**Description:** `ArenaPauseScreen` (`arena_pause_screen.h` / `arena/arena_pause_screen.cpp`) is a fully implemented screen that allows saving squad leader variants from arena training. It is reached via Space key during arena gameplay (push onto screen stack). It saves both squad leader snapshots and companion NTM snapshots to the `squad/` subdirectory. This screen is not mentioned in CLAUDE.md's screen flow diagram, file tree, or implementations table.

**Impact:** The previous documentation review (DOC-005 in the 01:35 PM review) noted the arena screen flow was missing but listed the pause screen as "planned." It is now implemented and should be documented as such.

**Fix plan:** Update the Screen Flow section to include:

```
               -> push(ArenaConfigScreen) -> push(ArenaGameScreen)
                                              -> Space: push(ArenaPauseScreen)
                                              |   -> Save squad leader + NTM variants
                                              -> follow mode: net viewer (Fighter/SquadLeader toggle)
```

Add to the file tree under `include/neuroflyer/ui/screens/`:
```
|   |   |   +-- arena_pause_screen.h
```

Add to `src/ui/screens/arena/`:
```
|   |   |   +-- arena_pause_screen.cpp
```

Add to the Existing Implementations table:
```
| Screen | `ArenaPauseScreen` | `ui/screens/arena_pause_screen.h` | `ui/screens/arena/arena_pause_screen.cpp` |
```

---

## [CRITICAL] Fighter Drill files missing from CLAUDE.md file tree

**ID:** `DOC-003`

**Location:** `CLAUDE.md` file tree (lines 21-137)

**Description:** Six fighter drill files exist in the codebase but are absent from the file tree:

Headers:
- `include/neuroflyer/fighter_drill_session.h`
- `include/neuroflyer/ui/screens/fighter_drill_screen.h`
- `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h`

Sources:
- `src/engine/fighter_drill_session.cpp`
- `src/ui/screens/game/fighter_drill_screen.cpp`
- `src/ui/screens/game/fighter_drill_pause_screen.cpp`

Test:
- `tests/fighter_drill_session_test.cpp`

**Impact:** These files are invisible to anyone navigating via CLAUDE.md.

**Fix plan:** Add to `include/neuroflyer/`:
```
|   +-- fighter_drill_session.h  -- FighterDrillSession, FighterDrillConfig, DrillPhase enum
```

Add to `include/neuroflyer/ui/screens/`:
```
|   |   |   +-- fighter_drill_screen.h
|   |   |   +-- fighter_drill_pause_screen.h
```

Add to `src/engine/`:
```
|   |   +-- fighter_drill_session.cpp  -- Drill world simulation, phase scoring
```

Add to `src/ui/screens/game/`:
```
|   |   |   +-- fighter_drill_screen.cpp
|   |   |   +-- fighter_drill_pause_screen.cpp
```

---

## [CRITICAL] collision.h description is stale

**ID:** `DOC-004`

**Location:** `CLAUDE.md` line 63: `collision.h -- ray_circle_intersect, point_in_circle (inline)`

**Description:** The CLAUDE.md annotation says collision.h contains only `ray_circle_intersect` and `point_in_circle`. The actual file now contains 8 functions:

1. `ray_circle_intersect` (original)
2. `point_in_circle` (original)
3. `bullet_triangle_collision` -- bullet point vs triangle vertices (non-rotated)
4. `triangle_circle_collision` -- triangle vertices vs circle (non-rotated)
5. `bullet_circle_collision` -- bullet point vs circle
6. `rotated_triangle_vertices` -- compute rotated vertex positions
7. `bullet_triangle_collision_rotated` -- bullet vs rotated triangle (arena mode)
8. `triangle_circle_collision_rotated` -- rotated triangle vs circle (arena mode)

The rotation-aware collision functions are essential for arena mode where ships have facing directions.

**Impact:** A developer looking for collision functions would not know about the rotation-aware variants. Someone implementing new arena collision behavior might duplicate existing functions.

**Fix plan:** Update the collision.h annotation:
```
|   +-- collision.h               -- Collision math: ray_circle_intersect, point_in_circle, bullet/triangle/circle collisions (rotated and non-rotated variants for arena)
```

---

## [CRITICAL] DrillPhase enum not documented anywhere

**ID:** `DOC-005`

**Location:** No documentation exists

**Description:** `DrillPhase` is an enum in `fighter_drill_session.h` with four values: `Expand`, `Contract`, `Attack`, `Done`. It drives the entire fighter drill scoring system. The phase determines which scripted squad leader inputs are injected and how per-tick fitness is computed. This is a novel game mechanic pattern not used in scroller or arena modes.

**Impact:** The phase-based scoring design is the core innovation of fighter drill mode. Without documentation, the rationale (velocity-based scoring, scripted inputs, phase sequencing) would need to be reverse-engineered from source.

**Fix plan:** Covered by the Fighter Drill Mode section proposed in DOC-001.

---

## [HIGH] Arena mode section still missing from CLAUDE.md

**ID:** `DOC-006`

**Location:** `CLAUDE.md` -- entire document

**Description:** The previous review (DOC-001 from the 01:35 PM review) identified that arena mode architecture is entirely missing from CLAUDE.md. This remains true. No arena section has been added. The previous review provided detailed draft text covering ArenaConfig, ArenaSession, ArenaMatch, ArenaSensor, Base, Camera, hierarchical brain architecture, squad leader system, sector grid, team evolution, arena fitness, and movement model. That draft text is still accurate and should be applied.

**Impact:** Arena mode is the largest subsystem in the codebase (7 engine headers, 6 engine source files, 2 screens + 1 pause screen, 4 views, 1 modal, 6 test files). Its complete absence from CLAUDE.md is the single largest documentation gap.

**Fix plan:** Apply the draft text from the previous review's DOC-001. Additionally, add the arena pause screen (now implemented, not just planned) and fighter drill entry point.

---

## [HIGH] Screen flow diagram missing fighter drill path

**ID:** `DOC-007`

**Location:** `CLAUDE.md` lines 139-151 (Screen Flow section)

**Description:** The screen flow diagram does not include the fighter drill navigation path:

```
VariantViewerScreen -> push(FighterDrillScreen) -> push(FighterDrillPauseScreen)
                                                    -> tabs: Evolution, Save Variants
```

This is accessible from the variant viewer via the "Fighter Drill" button.

**Impact:** Contributors cannot trace how fighter drill screens are reached from the UI.

**Fix plan:** Add to the Screen Flow section under the VariantViewerScreen branch:

```
                                        -> push(FighterDrillScreen) (fighter drill training)
                                           -> Space: push(FighterDrillPauseScreen)
                                              -> tabs: Evolution, Save Variants
```

---

## [HIGH] Arena sensor system (rotation-aware) not documented in Sensor Engine section

**ID:** `DOC-008`

**Location:** `CLAUDE.md` Sensor Engine section (lines 181-193)

**Description:** The Sensor Engine section describes only the scroller sensor system (`query_sensor`, `build_ship_input`, `decode_output`). The arena has a parallel sensor system in `arena_sensor.h`/`arena_sensor.cpp` with:

- `query_arena_sensor()` -- dispatches Raycast or Occulus against arena entities (towers, tokens, friendly ships, enemy ships, bullets)
- `build_arena_ship_input()` -- builds fighter input vector: [sensor values] + [6 squad leader inputs] + [memory]
- `ArenaHitType` enum -- Nothing, Tower, Token, FriendlyShip, EnemyShip, Bullet
- `ArenaQueryContext` -- bundles ship position, rotation, team, and all entity spans
- `DirRange` / `compute_dir_range()` -- heading/distance computation for squad leader inputs
- `is_full_sensor` flag on SensorDef -- determines whether a sensor produces 1 value (distance only) or 5 values (distance + entity type channels)

The arena sensor system is rotation-aware: sensor angles are relative to ship facing direction, not world-absolute.

**Impact:** The sensor engine section claims to be the "single source of truth" for sensor detection but does not mention the arena-specific sensor functions. A developer modifying sensor behavior might update only the scroller path.

**Fix plan:** Add to the Sensor Engine section:

```markdown
### Arena Sensors (`arena_sensor.h`)

Arena mode has a parallel sensor system for rotation-aware detection:
- `query_arena_sensor(SensorDef, ArenaQueryContext)` -- dispatches Raycast or Occulus, rotated by ship facing direction. Detects towers, tokens, friendly ships, enemy ships, and bullets. Returns `ArenaSensorReading` with distance and `ArenaHitType`.
- `build_arena_ship_input(ShipDesign, ctx, squad_inputs..., memory)` -- builds the complete fighter input vector: [sensor values] + [6 squad leader inputs] + [memory slots].
- `is_full_sensor` flag -- when true, each sensor produces 5 input values (distance, is_tower, is_token, is_friend, is_bullet) instead of 1. This gives the fighter net entity-type awareness.

Both the arena game screen and fighter drill screen use `build_arena_ship_input()` for their fighter nets.
```

---

## [HIGH] `is_full_sensor` flag on SensorDef not documented

**ID:** `DOC-009`

**Location:** `CLAUDE.md` -- Sensor Engine section, ShipDesign description

**Description:** The `SensorDef` struct has an `is_full_sensor` boolean flag that determines whether a sensor produces 1 input value (distance only, like scroller mode) or 5 input values (distance + 4 entity-type channels: is_tower, is_token, is_friend, is_bullet). This flag is used in `build_arena_ship_input()` and `build_arena_fighter_input_labels()`. It fundamentally changes the neural net input size for arena fighters. CLAUDE.md does not mention this field.

**Impact:** A developer adding new sensor types or modifying the arena input layout would not know about this field or how it affects input vector sizing.

**Fix plan:** Add to the ShipDesign / SensorDef documentation:

```markdown
`SensorDef` fields include:
- `is_full_sensor` (bool) -- when true, the sensor produces 5 input values in arena mode (distance, is_tower, is_token, is_friend, is_bullet) instead of a single distance value. This gives arena fighters entity-type awareness. Does not affect scroller mode sensors.
```

---

## [HIGH] Existing Implementations table missing fighter drill and arena pause entries

**ID:** `DOC-010`

**Location:** `CLAUDE.md` lines 312-329 (Existing Implementations table)

**Description:** The reference table is missing entries for:
- `FighterDrillScreen` (Screen)
- `FighterDrillPauseScreen` (Screen)
- `ArenaPauseScreen` (Screen)

Plus the arena entries noted in the previous review (DOC-006).

**Impact:** The table is the quick-reference for finding implementation patterns. Missing entries mean new contributors cannot find these screens.

**Fix plan:** Add rows:
```markdown
| Screen | `FighterDrillScreen` | `ui/screens/fighter_drill_screen.h` | `ui/screens/game/fighter_drill_screen.cpp` |
| Screen | `FighterDrillPauseScreen` | `ui/screens/fighter_drill_pause_screen.h` | `ui/screens/game/fighter_drill_pause_screen.cpp` |
| Screen | `ArenaPauseScreen` | `ui/screens/arena_pause_screen.h` | `ui/screens/arena/arena_pause_screen.cpp` |
```

---

## [HIGH] docs/arena_mode.md is a stub

**ID:** `DOC-011`

**Location:** `/Users/wldarden/repos/Neuroflyer/docs/arena_mode.md`

**Description:** This file contains only the line `# Arena mode` -- a completely empty stub. Arena mode is a major subsystem with extensive implementation. Having a stub file with no content is worse than having no file at all, because it suggests documentation exists when it does not.

**Impact:** Someone looking for arena documentation finds a stub and assumes arena mode is unfinished or trivially simple.

**Fix plan:** Either populate this file with a summary of arena mode architecture (referencing the design specs in `docs/superpowers/specs/2026-03-28-arena-mode-design.md` and `2026-03-30-squad-leader-design.md`), or delete the file and consolidate arena documentation into CLAUDE.md.

---

## [MEDIUM] feature-audit.md missing fighter drill and arena pause screen entries

**ID:** `DOC-012`

**Location:** `/Users/wldarden/repos/Neuroflyer/docs/feature-audit.md`

**Description:** The feature audit has no entries for:
- FighterDrillSession (engine)
- FighterDrillScreen (UI)
- FighterDrillPauseScreen (UI)
- ArenaPauseScreen (UI)
- DrillPhase scoring system
- Fighter drill test suite

The previous review noted arena systems are missing too (DOC-011 in that review). These remain unaddressed, and fighter drill adds more entries.

**Impact:** The feature audit no longer represents the system inventory. Code size summary (line 87) is also significantly outdated.

**Fix plan:** Add to the arena systems section (or create one if applying previous review's draft):

```markdown
| Fighter drill session | Current | `fighter_drill_session.cpp` | Phase-based training for fighter nets with scripted squad inputs |
| Fighter drill screen | Current | `fighter_drill_screen.cpp` | UI for fighter drill training loop |
| Fighter drill pause screen | Current | `fighter_drill_pause_screen.cpp` | Evolution config + variant saving for drills |
| Arena pause screen | Current | `arena_pause_screen.cpp` | Squad leader + NTM variant saving from arena training |
```

---

## [MEDIUM] Triangle struct has undocumented arena-specific members

**ID:** `DOC-013`

**Location:** `CLAUDE.md` line 64: `game.h -- Tower, Token, Bullet, Triangle, GameSession`

**Description:** The `Triangle` struct in `game.h` now has arena-specific members that are not documented:
- `float rotation = 0.0f` -- facing direction in radians (0 = up, CW positive)
- `float rotation_speed = 0.05f` -- radians per tick
- `void apply_arena_actions(...)` -- rotation+thrust movement model (vs. scroller's strafe model)

The Bullet struct also has directional fields: `dir_x`, `dir_y`, `owner_index`, `distance_traveled`, `max_range`, and `update_directional()`. These support arena-mode directional bullets (fire in facing direction with max range cutoff).

CLAUDE.md's annotation for game.h gives no hint that these arena features exist.

**Impact:** A developer working with Triangle or Bullet would not know about the arena movement model or directional bullet system.

**Fix plan:** Update the game.h annotation:
```
|   +-- game.h                    -- Tower, Token, Bullet (directional with max range), Triangle (rotation + arena thrust model), GameSession
```

---

## [MEDIUM] Key Design Decisions missing fighter drill and collision decisions

**ID:** `DOC-014`

**Location:** `CLAUDE.md` lines 331-343 (Key Design Decisions)

**Description:** Missing design decisions:
- **Duplicated physics in FighterDrillSession** -- collision and movement math is duplicated from ArenaSession rather than extracting a shared module. This was a deliberate scope decision documented in the spec but not in CLAUDE.md.
- **Velocity-based drill scoring** -- scoring uses per-tick velocity dot products, not absolute positions. This prevents spawn-position bias and is a non-obvious design choice.
- **Rotation-aware collision functions** -- `collision.h` has both rotated and non-rotated variants. Scroller uses non-rotated (ships don't rotate), arena uses rotated. Both kept to avoid unnecessary rotation math in scroller mode.

**Impact:** A developer might try to refactor the "duplicated" physics code without knowing it was intentional, or might change drill scoring to position-based without understanding why velocity-based was chosen.

**Fix plan:** Add to Key Design Decisions:

```markdown
- **Duplicated arena physics in fighter drill** -- `FighterDrillSession` duplicates collision and movement math from `ArenaSession` rather than extracting a shared module. Intentional scope decision to keep fighter drill self-contained.
- **Velocity-based drill scoring** -- drill phases score using per-tick `dot(velocity, desired_direction)`, not absolute position. Prevents a fighter's spawn position from unfairly affecting fitness.
- **Rotated + non-rotated collision variants** -- `collision.h` provides both `bullet_triangle_collision()` (scroller, ships face up) and `bullet_triangle_collision_rotated()` (arena, ships have facing direction). Both kept to avoid unnecessary trig in scroller mode.
```

---

## [MEDIUM] Scoring table only covers scroller mode

**ID:** `DOC-015`

**Location:** `CLAUDE.md` lines 195-205 (Scoring section)

**Description:** The scoring table documents only scroller scoring (distance, tower destroyed, token collected, bullet fired, death). Fighter drill has a completely different scoring model (expand/contract/attack velocity scores, attack hit bonus, token bonus). Arena mode has its own fitness weights (base damage, survival time, ships alive, tokens collected). Neither is documented.

**Impact:** Users and developers would assume the scroller scoring model applies everywhere.

**Fix plan:** Add sub-sections:

```markdown
### Arena Scoring (defaults, tunable)

| Factor | Default Weight |
|--------|---------------|
| Base damage dealt | 1.0 |
| Survival time | 0.5 |
| Ships alive at end | 0.2 |
| Tokens collected | 0.3 |

### Fighter Drill Scoring

Per-tick velocity-based scoring:

| Phase | Scoring Formula |
|-------|----------------|
| Expand | dot(velocity, away_from_center) * expand_weight |
| Contract | dot(velocity, toward_center) * contract_weight |
| Attack | dot(velocity, toward_starbase) * travel_weight + bullet_hits * hit_bonus |

Always active: +token_bonus per token collected. Tower collision = death.
```

---

## [MEDIUM] arena-mode-backlog.md not referenced from main backlog

**ID:** `DOC-016`

**Location:** `/Users/wldarden/repos/Neuroflyer/docs/arena-mode-backlog.md` and `/Users/wldarden/repos/Neuroflyer/docs/backlog.md`

**Description:** There are two separate backlog files. The main `backlog.md` has arena items (8-11) but the separate `arena-mode-backlog.md` has different items (squad leader net analysis, squad leader training modes, arena info panel improvements). Neither references the other, and some items overlap (e.g., squad leader training appears in both with different scope).

**Impact:** Backlog items could be lost or duplicated. A developer checking one backlog would miss items in the other.

**Fix plan:** Merge `arena-mode-backlog.md` into `backlog.md` as a dedicated "## Arena Mode Backlog" section, then delete the standalone file. Or add a cross-reference at the top of each file.

---

## [MEDIUM] docs/engine-architecture.md is stale

**ID:** `DOC-017`

**Location:** `/Users/wldarden/repos/Neuroflyer/docs/engine-architecture.md`

**Description:** This document describes a planned ECS-lite architecture that was never implemented. It says "Status: Planned, not yet implemented" from 2026-03-26. However, arena mode was implemented using the existing flat entity structs (Triangle, Tower, Token, Bullet, Base) with separate session classes -- not the ECS approach described here. The document's "Current State" section says entities are "Flat structs in game.h" which is still accurate, but the document implies this is temporary. In practice, the flat-struct approach has been proven workable through arena mode and fighter drill mode.

**Impact:** A developer reading this document might attempt the ECS refactor without realizing the current approach has been validated through two additional game modes.

**Fix plan:** Update the status and current state sections to reflect that arena mode and fighter drill mode were successfully built on top of the flat entity approach. The ECS plan may still be desirable for future extensibility but is no longer a prerequisite for any planned feature.

---

## [MEDIUM] Tests directory not documented in CLAUDE.md file tree

**ID:** `DOC-018`

**Location:** `CLAUDE.md` line 131: `tests/ -- GoogleTests`

**Description:** The tests directory is listed with only a one-word annotation. There are now 21 test files covering scroller, arena, and fighter drill systems. The annotation gives no sense of test coverage scope. New test files added recently include:
- `arena_session_test.cpp`
- `arena_match_test.cpp`
- `arena_sensor_test.cpp`
- `squad_leader_test.cpp`
- `sector_grid_test.cpp`
- `team_evolution_test.cpp`
- `camera_test.cpp`
- `base_test.cpp`
- `fighter_drill_session_test.cpp`

**Impact:** Low-medium. Contributors would not know which systems have test coverage without browsing the directory.

**Fix plan:** Expand the test annotation:
```
+-- tests/                        -- GoogleTests (21 files: scroller, arena, drill, evolution, snapshot, collision, sensors)
```

---

## [LOW] Controls table missing fighter drill keybindings

**ID:** `DOC-019`

**Location:** `CLAUDE.md` lines 218-226 (Controls section)

**Description:** The controls table covers only scroller mode. Fighter drill mode has similar but distinct controls:
- Tab: cycle camera (Swarm/Best/Worst)
- Space: push FighterDrillPauseScreen
- 1-4: speed multiplier
- Escape: exit drill

**Impact:** Users of fighter drill mode cannot find keybindings in documentation.

**Fix plan:** Add "### Fighter Drill Controls" sub-section with the keybindings listed above.

---

## [LOW] Neural Net section outputs description is incomplete

**ID:** `DOC-020`

**Location:** `CLAUDE.md` line 163: `Outputs (5): UP, DOWN, LEFT, RIGHT, SHOOT`

**Description:** The Neural Net section says outputs are always 5 (UP, DOWN, LEFT, RIGHT, SHOOT). Squad leader nets have 5 outputs but they are completely different: 3 tactical (AttackStarbase, AttackShip, DefendHome) + 2 spacing (Expand, Contract), interpreted via argmax. Additionally, fighter and solo nets may have more than 5 outputs when memory slots are used (outputs beyond index 4 are recurrent memory feedback).

**Impact:** Minor inaccuracy that could confuse someone working on output decoding.

**Fix plan:** Update:
```markdown
- **Outputs:** 5 action outputs (UP, DOWN, LEFT, RIGHT, SHOOT) + optional recurrent memory outputs. Squad leader nets use 5 outputs differently: 3 tactical orders + 2 spacing orders via argmax.
```

---

## [LOW] `sensor_engine.h` now exports squad leader label functions

**ID:** `DOC-021`

**Location:** `CLAUDE.md` Sensor Engine section (lines 181-193)

**Description:** The sensor engine section lists `build_input_labels/colors/display_order(ShipDesign)` but does not mention the new squad leader label functions:
- `build_squad_leader_input_labels()` -- returns 14 labels for the squad leader net's strategic inputs
- `build_squad_leader_output_labels()` -- returns 5 labels for tactical/spacing orders

These are used by `variant_net_render.cpp` when rendering squad leader nets in the net viewer.

**Impact:** Minor. A developer extending net viewer labels would not know these functions exist.

**Fix plan:** Add to the Sensor Engine function list:
```markdown
- `build_squad_leader_input_labels()` -- 14 strategic input labels for squad leader nets
- `build_squad_leader_output_labels()` -- 5 tactical/spacing output labels for squad leader nets
```

---

## [LOW] `VariantNetConfig` struct has `net_type` field not documented

**ID:** `DOC-022`

**Location:** `CLAUDE.md` Key Design Decisions -- neuralnet-ui library section

**Description:** The Key Design Decisions section describes the two-layer render design (`render_neural_net()` generic + `render_variant_net()` NeuroFlyer-specific) but does not mention that `VariantNetConfig` now includes a `NetType net_type` field that switches between Solo, Fighter, and SquadLeader label/color schemes. The `build_variant_net_config()` function has a 3-way switch on NetType.

**Impact:** A developer adding a new NetType (e.g., Commander) would need to find this switch statement without documentation pointing to it.

**Fix plan:** Add to the variant_net_render annotation:
```
|   +-- variant_net_render.h  -- render_variant_net(): ShipDesign + NetType -> configured net render (Solo/Fighter/SquadLeader label schemes)
```

---

## [LOW] `EvolutionConfig.elitism_count` default changed

**ID:** `DOC-023`

**Location:** `CLAUDE.md` line 210: `Elitism: top 10 survive unchanged`

**Description:** The Evolution section says "top 10 survive unchanged." The actual `EvolutionConfig` default is `elitism_count = 3`. The GameConfig still has `elitism_count = 10` for the scroller pause screen, but the EvolutionConfig used by fighter drill defaults to 3. This discrepancy between the two config structs is undocumented.

**Impact:** Minor confusion about the actual default.

**Fix plan:** Update:
```markdown
- **Elitism:** top N survive unchanged (default: 10 for scroller via GameConfig, 3 for EvolutionConfig)
```

---

## Appendix: Completeness Checklist (updated from previous review)

| Question | Previous (01:35 PM) | Current (04:46 PM) |
|----------|---------------------|---------------------|
| Does CLAUDE.md mention arena mode? | NO | NO (unchanged) |
| Does CLAUDE.md mention fighter drill mode? | N/A (didn't exist) | NO |
| Does CLAUDE.md list FighterDrillScreen / FighterDrillPauseScreen? | N/A | NO |
| Does CLAUDE.md list ArenaPauseScreen? | NO (noted as planned) | NO (now implemented) |
| Does CLAUDE.md describe DrillPhase enum? | N/A | NO |
| Does CLAUDE.md describe is_full_sensor flag? | N/A | NO |
| Does CLAUDE.md list rotation-aware collision functions? | NO | NO |
| Does CLAUDE.md describe fighter drill scoring? | N/A | NO |
| Does CLAUDE.md describe arena scoring? | NO | NO |
| Is docs/arena_mode.md populated? | YES (stub noted) | NO (still a stub) |
| Does feature-audit.md cover fighter drill? | N/A | NO |
| Are backlogs consolidated? | N/A | NO (two separate files) |
| Does engine-architecture.md reflect current state? | Not reviewed | NO (stale) |

### Previous Review Items Still Unaddressed

All 16 items from the 01:35 PM review remain unaddressed in the documentation. The critical items are:
- DOC-001: Arena mode architecture missing from CLAUDE.md
- DOC-002: Snapshot format version wrong (says v1-v4, actual v1-v7)
- DOC-003: File tree missing many files
- DOC-004: NetType enum not documented

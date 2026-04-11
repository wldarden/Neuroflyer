# Cleanup Sprint Plan — Mar 30, 04:46 PM

> Technical debt, code quality, documentation, and consolidation.
> See category reports for full details.
>
> **How to use:** Tell Claude:
> - "execute the cleanup sprint" — run all cleanup items
> - "skip CLEANUP-ID" — reject a specific item
> - "what's the cleanup status?" — check progress

## Architecture & Consolidation Items

| ID | Category | Severity | Description | Location | Status |
|----|----------|----------|-------------|----------|--------|
| ARCH-001 | Architecture | CRITICAL | Engine code imports UI header (theme.h) | `sensor_engine.cpp:3` | pending |
| ARCH-002 | Architecture | CRITICAL | FighterDrillScreen mixes game logic into UI (~800 lines) | `fighter_drill_screen.cpp:254-358` | pending |
| CONS-001 | Consolidation | HIGH | SDL drawing primitives duplicated across 2 files | `fighter_drill_screen.cpp:28-90`, `arena_game_view.cpp:36-106` | pending |
| CONS-002 | Consolidation | HIGH | Session tick mechanics duplicated (~200 lines) | `arena_session.cpp:176-325`, `fighter_drill_session.cpp:134-244` | pending |
| CONS-003 | Consolidation | HIGH | Occulus ellipse overlap math duplicated | `sensor_engine.cpp:79-137`, `arena_sensor.cpp:98-181` | pending |
| ARCH-003 | Architecture | HIGH | FighterDrillPauseScreen copies population by value | `fighter_drill_pause_screen.h:29` | pending |
| ARCH-004 | Architecture | HIGH | Duplicate session mechanics (same as CONS-002) | `arena_session.cpp`, `fighter_drill_session.cpp` | pending |
| ARCH-005 | Architecture | HIGH | arena_sensor.cpp duplicates occulus math (same as CONS-003) | `arena_sensor.cpp:98-181` | pending |
| IMP-003 | Improvement | HIGH | Duplicated snapshot-to-individual deserialization logic | `evolution.cpp:752-780, 782-852` | pending |
| CONS-004 | Consolidation | MEDIUM | Input handling (speed/zoom/pan) duplicated between screens | `fighter_drill_screen.cpp:196-250`, `arena_game_screen.cpp:192-257` | pending |
| CONS-007 | Consolidation | MEDIUM | Render color constants duplicated as inline RGBA tuples | 5+ locations | pending |
| CONS-008 | Consolidation | MEDIUM | ImGui window flags boilerplate repeated 18+ times | All screen files | pending |
| ARCH-006 | Architecture | MEDIUM | SDL drawing helpers duplicated (same as CONS-001) | `fighter_drill_screen.cpp`, `arena_game_view.cpp` | pending |
| ARCH-007 | Architecture | MEDIUM | Heading normalization code repeated inline | `fighter_drill_screen.cpp:293-317` | pending |
| ARCH-008 | Architecture | MEDIUM | FighterDrillSession::get_scores() returns by value | `fighter_drill_session.h:63` | pending |
| ARCH-009 | Architecture | MEDIUM | ArenaQueryContext constructed with 12-line boilerplate x3 | 3 locations | pending |
| ARCH-010 | Architecture | MEDIUM | Non-rotated collision variants exist alongside rotated | `collision.h:43-61` | pending |
| IMP-006 | Improvement | MEDIUM | compute_squad_stats recomputes world diagonal twice | `arena_session.cpp:349-416` | pending |
| IMP-007 | Improvement | MEDIUM | teams_alive() uses std::set for 2-4 elements | `arena_session.cpp:440-448` | pending |
| IMP-009 | Improvement | MEDIUM | No test coverage for team_evolution crossover/squad-only | `team_evolution_test.cpp` | pending |
| IMP-010 | Improvement | MEDIUM | No test for snapshot round-trip with v7 net_type | `snapshot_io.cpp` | pending |
| IMP-011 | Improvement | MEDIUM | Activation gene extraction duplicated 3 times | `evolution.cpp` | pending |
| IMP-013 | Improvement | MEDIUM | Bullet::update_directional computes unnecessary sqrt | `game.cpp:56-65` | pending |
| CONS-009 | Consolidation | LOW | ArenaQueryContext built manually in 3 locations (same as ARCH-009) | 3 locations | pending |
| CONS-010 | Consolidation | LOW | Phase progress bar computation duplicated in drill HUD | `fighter_drill_screen.cpp:671,715` | pending |
| CONS-011 | Consolidation | LOW | Alive-ship counting duplicated in fighter_drill_screen | `fighter_drill_screen.cpp:680,727` | pending |
| CONS-012 | Consolidation | LOW | ray_circle_hit re-implements collision.h logic | `arena_sensor.cpp:16-33` | pending |
| ARCH-011 | Architecture | LOW | std::cout debug logging in UI screens | `fighter_drill_screen.cpp:190`, `arena_game_screen.cpp:411` | pending |
| ARCH-012 | Architecture | LOW | Snapshot stored by value in FighterDrillScreen after init | `fighter_drill_screen.h:41` | pending |
| ARCH-013 | Architecture | LOW | Build system fetches library repos with GIT_TAG main | `CMakeLists.txt:14-25` | pending |
| IMP-015 | Improvement | LOW | gmtime_r is POSIX-only | `genome_manager.cpp:31` | pending |
| IMP-016 | Improvement | LOW | snprintf used instead of std::format | `arena_sensor.cpp:279-308` | pending |
| IMP-017 | Improvement | LOW | No static assertions for NTM/squad-leader input sizes | `arena_config.h:50-57` | pending |

## Documentation Items

| ID | Severity | Description | Status |
|----|----------|-------------|--------|
| DOC-001 | CRITICAL | Fighter Drill mode entirely missing from CLAUDE.md | pending |
| DOC-002 | CRITICAL | Arena Pause Screen not documented | pending |
| DOC-003 | CRITICAL | Fighter Drill files missing from CLAUDE.md file tree | pending |
| DOC-004 | CRITICAL | collision.h description is stale | pending |
| DOC-005 | CRITICAL | DrillPhase enum not documented anywhere | pending |
| DOC-006 | HIGH | Arena mode section still missing from CLAUDE.md | pending |
| DOC-007 | HIGH | Screen flow diagram missing fighter drill path | pending |
| DOC-008 | HIGH | Arena sensor system not documented in Sensor Engine section | pending |
| DOC-009 | HIGH | is_full_sensor flag on SensorDef not documented | pending |
| DOC-010 | HIGH | Existing Implementations table missing entries | pending |
| DOC-011 | HIGH | docs/arena_mode.md is a stub | pending |
| DOC-012 | MEDIUM | feature-audit.md missing fighter drill and arena pause entries | pending |
| DOC-013 | MEDIUM | Triangle struct has undocumented arena-specific members | pending |
| DOC-014 | MEDIUM | Key Design Decisions missing entries | pending |
| DOC-015 | MEDIUM | Scoring table only covers scroller mode | pending |
| DOC-016 | MEDIUM | arena-mode-backlog.md not referenced from main backlog | pending |
| DOC-017 | MEDIUM | docs/engine-architecture.md is stale | pending |
| DOC-018 | MEDIUM | Tests directory annotation sparse | pending |
| DOC-019 | LOW | Controls table missing fighter drill keybindings | pending |
| DOC-020 | LOW | Neural Net section outputs description incomplete | pending |
| DOC-021 | LOW | sensor_engine.h squad leader label functions not documented | pending |
| DOC-022 | LOW | VariantNetConfig net_type field not documented | pending |
| DOC-023 | LOW | EvolutionConfig.elitism_count default changed | pending |

---

### ARCH-001: Engine code imports UI header
**What to do:** Move `node_sight`, `node_sensor`, `node_system`, `node_memory` color constants from `theme.h` to a non-UI header (e.g., `ship_design.h` alongside `NodeStyle`). Remove `#include <neuroflyer/ui/theme.h>` from `sensor_engine.cpp`.
**Files to touch:** `include/neuroflyer/ui/theme.h`, `include/neuroflyer/ship_design.h`, `src/engine/sensor_engine.cpp`

### ARCH-002: FighterDrillScreen mixes game logic into UI
**What to do:** Extract tick logic into a `FighterDrillMatch` engine class. The screen should own a match instance and only handle rendering/input.
**Files to touch:** New: `include/neuroflyer/fighter_drill_match.h`, `src/engine/fighter_drill_match.cpp`. Modify: `src/ui/screens/game/fighter_drill_screen.cpp`

### CONS-001 / ARCH-006: SDL drawing primitives duplicated
**What to do:** Create `include/neuroflyer/ui/sdl_draw.h` and `src/ui/renderers/sdl_draw.cpp` with shared `draw_rotated_triangle`, `draw_filled_circle`, `draw_circle_outline`. Remove copies from both files.
**Files to touch:** New: `include/neuroflyer/ui/sdl_draw.h`, `src/ui/renderers/sdl_draw.cpp`. Modify: `fighter_drill_screen.cpp`, `arena_game_view.cpp`

### CONS-002 / ARCH-004: Session tick mechanics duplicated
**What to do:** Extract shared mechanics into free functions in `session_mechanics.h/.cpp`. Both sessions call shared functions.
**Files to touch:** New: `include/neuroflyer/session_mechanics.h`, `src/engine/session_mechanics.cpp`. Modify: `arena_session.cpp`, `fighter_drill_session.cpp`

### CONS-003 / ARCH-005: Occulus overlap math duplicated
**What to do:** Extract core ellipse overlap check into shared function in `sensor_engine.cpp` or new `sensor_math.h`.
**Files to touch:** `src/engine/sensor_engine.cpp`, `src/engine/arena_sensor.cpp`

### ARCH-003: Population copied by value on pause
**What to do:** Change FighterDrillPauseScreen constructor to accept `const std::vector<Individual>&`.
**Files to touch:** `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h`, `src/ui/screens/game/fighter_drill_pause_screen.cpp`

### IMP-003: Duplicated snapshot-to-individual logic
**What to do:** Have `create_population_from_snapshot()` call `snapshot_to_individual()` for its first individual instead of duplicating the loop.
**Files to touch:** `src/engine/evolution.cpp`

### CONS-004: Input handling duplicated
**What to do:** Add `Camera::handle_pan_input()`, `Camera::handle_zoom_input()`, and free `apply_speed_controls()`. Both screens call helpers.
**Files to touch:** `include/neuroflyer/camera.h`, `src/engine/camera.cpp`, `fighter_drill_screen.cpp`, `arena_game_screen.cpp`

### CONS-007: Render color constants duplicated
**What to do:** Add game object render colors to `theme.h`. Replace inline RGBA tuples.
**Files to touch:** `include/neuroflyer/ui/theme.h`, `arena_game_view.cpp`, `fighter_drill_screen.cpp`

### CONS-008: ImGui window flags boilerplate
**What to do:** Define `kFixedPanelFlags` in `ui_widget.h`. Replace all 18+ occurrences.
**Files to touch:** `include/neuroflyer/ui/ui_widget.h`, all screen files

### ARCH-007: Heading normalization repeated
**What to do:** Extract `normalize_relative_heading()` utility. Use `std::remainder()` instead of while loops.
**Files to touch:** `src/ui/screens/game/fighter_drill_screen.cpp`

### ARCH-008: get_scores() returns by value
**What to do:** Change to `const std::vector<float>&` return.
**Files to touch:** `include/neuroflyer/fighter_drill_session.h`

### ARCH-009 / CONS-009: ArenaQueryContext boilerplate
**What to do:** Add factory method `ArenaQueryContext::for_ship(...)`. Replace 3 manual construction sites.
**Files to touch:** `include/neuroflyer/arena_sensor.h`, `fighter_drill_screen.cpp`, `arena_game_screen.cpp`, `arena_match.cpp`

### Documentation items (DOC-001 through DOC-023)
**What to do:** The documentation report (03-documentation.md) contains draft text for all 23 items. Apply them to CLAUDE.md, docs/feature-audit.md, docs/arena_mode.md, and docs/engine-architecture.md.
**Files to touch:** `CLAUDE.md`, `docs/feature-audit.md`, `docs/arena_mode.md`, `docs/engine-architecture.md`, `docs/backlog.md`, `docs/arena-mode-backlog.md`

---

## Status Tracker

| Total Items | Completed | Pending | Rejected | Blocked |
|-------------|-----------|---------|----------|---------|
| 56 | 0 | 56 | 0 | 0 |

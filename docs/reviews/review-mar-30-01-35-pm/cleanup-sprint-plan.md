# Cleanup Sprint Plan — Mar 30, 01:35 PM

> Code that works but could be better. Technical debt, architecture improvements, documentation.
> See category reports for full details.
>
> **How to use:** Tell Claude:
> - "execute the cleanup sprint" — run all cleanups
> - "skip CLEANUP-ID" — reject a specific item
> - "what's the cleanup status?" — check progress

## Cleanups

| ID | Category | Severity | Description | Location | Status |
|----|----------|----------|-------------|----------|--------|
| ARCH-001 | Architecture | HIGH | Engine file depends on UI header (theme.h) | `sensor_engine.cpp:3` | pending |
| ARCH-002 | Architecture | HIGH | FlySessionScreen uses static locals for all state | `fly_session_screen.cpp:28,713` | pending |
| CONS-001 | Consolidation | HIGH | Squad leader input computation duplicated across 3 files | `arena_game_screen.cpp, arena_match.cpp` | pending |
| CONS-002 | Consolidation | HIGH | evolve_team_population and evolve_squad_only 90% identical | `team_evolution.cpp:76-153` | pending |
| CONS-003 | Consolidation | HIGH | Fitness scoring duplicated with subtle drift | `arena_match.cpp, arena_game_screen.cpp` | pending |
| STALE-005 | Stale | HIGH | Duplicated 130-line arena tick loop | `arena_game_screen.cpp, arena_match.cpp` | pending |
| STALE-006 | Stale | MEDIUM | build_input_labels/colors/display_order produce scroller data for fighters | `sensor_engine.cpp:284-408` | pending |
| STALE-007 | Stale | MEDIUM | VariantViewerScreen tab state persists unexpectedly | `variant_viewer_screen.cpp:51-61` | pending |
| STALE-008 | Stale | MEDIUM | PauseConfigScreen doesn't set net_type on saved snapshots | `pause_config_screen.cpp:327-341` | pending |
| ARCH-003 | Architecture | MEDIUM | ArenaGameView instantiated per frame | `arena_game_screen.cpp:430` | pending |
| ARCH-004 | Architecture | MEDIUM | Renderer exposes public member variables | `renderer.h:55-56` | pending |
| ARCH-005 | Architecture | MEDIUM | Redundant net topology configs | `arena_config.h, team_evolution.h` | pending |
| ARCH-006 | Architecture | MEDIUM | collision.h triangle collision ignores rotation | `collision.h:43-61` | pending |
| ARCH-008 | Architecture | LOW | AppState used as message bus for squad training flags | `app_state.h:59-63` | pending |
| ARCH-009 | Architecture | LOW | No separate engine library target in CMake | `CMakeLists.txt:67-125` | pending |
| ARCH-010 | Architecture | LOW | Missing on_exit() in ArenaGameScreen | `arena_game_screen.h` | pending |
| CONS-004 | Consolidation | MEDIUM | SectorGrid rebuilt from scratch every tick | `arena_game_screen.cpp:235` | pending |
| CONS-007 | Consolidation | MEDIUM | ArenaQueryContext 11-field inline construction duplicated | `arena_game_screen.cpp, arena_match.cpp` | pending |
| CONS-008 | Consolidation | MEDIUM | NTM/leader/fighter network compilation duplicated | `arena_match.cpp, arena_game_screen.cpp` | pending |
| CONS-009 | Consolidation | LOW | Recurrent state initialization duplicated | Multiple files | pending |
| CONS-010 | Consolidation | LOW | Enemy base lookup duplicated (subsumed by CONS-001) | Multiple files | pending |
| CONS-011 | Consolidation | LOW | Triangle::SIZE overloaded for 3 different roles | Multiple files | pending |
| CONS-012 | Consolidation | LOW | Team colors only in anonymous namespace | `arena_game_view.cpp:15-33` | pending |
| DEAD-004 | Dead Code | MEDIUM | UIManager::replace_screen() never called | `ui_manager.cpp:40-48` | pending |
| DEAD-005 | Dead Code | MEDIUM | UIManager::has_screens/active_screen never called externally | `ui_manager.cpp:50-57` | pending |
| DEAD-006 | Dead Code | MEDIUM | game-ui/modal.h compiled but never used | `CMakeLists.txt:124` | pending |
| DEAD-007 | Dead Code | MEDIUM | ArenaSession::add_bullet() only used by tests | `arena_session.h:31` | pending |
| DEAD-008 | Dead Code | MEDIUM | ArenaSession::squad_of() only used by tests | `arena_session.h:36` | pending |
| DOC-001 | Documentation | HIGH | Arena mode architecture absent from CLAUDE.md | `CLAUDE.md` | pending |
| DOC-002 | Documentation | HIGH | Snapshot format version wrong in CLAUDE.md (says v1-v4, actual v7) | `CLAUDE.md` | pending |
| DOC-003 | Documentation | HIGH | File tree missing ~30 files, lists 3 nonexistent files | `CLAUDE.md` | pending |
| DOC-004 | Documentation | HIGH | NetType enum undocumented | `CLAUDE.md` | pending |
| DOC-005 | Documentation | MEDIUM | Screen flow missing arena path | `CLAUDE.md` | pending |
| DOC-006 | Documentation | MEDIUM | Reference table incomplete | `CLAUDE.md` | pending |
| DOC-007 | Documentation | MEDIUM | GameMode enum undocumented | `CLAUDE.md` | pending |
| DOC-008 | Documentation | MEDIUM | Genome subdirectory layout change undocumented | `CLAUDE.md` | pending |
| DOC-009 | Documentation | MEDIUM | net_viewer.h listed but doesn't exist | `CLAUDE.md` | pending |
| DOC-010 | Documentation | MEDIUM | feature-audit.md has stale version | `docs/feature-audit.md` | pending |
| DOC-011 | Documentation | MEDIUM | feature-audit missing all arena systems | `docs/feature-audit.md` | pending |
| DOC-012 | Documentation | LOW | Opening description outdated | `CLAUDE.md` | pending |
| DOC-013 | Documentation | LOW | Key design decisions missing arena rationale | `CLAUDE.md` | pending |
| DOC-014 | Documentation | LOW | Controls table only covers scroller | `CLAUDE.md` | pending |
| DOC-015 | Documentation | LOW | Renderers tree lists removed files | `CLAUDE.md` | pending |
| DOC-016 | Documentation | LOW | Spec files not cross-referenced | `CLAUDE.md` | pending |
| STALE-011 | Stale | LOW | Fighter label display order includes scroller system block | `sensor_engine.cpp:373-376` | pending |
| STALE-014 | Stale | LOW | is_token weight drop is a judgment call | `evolution.cpp:282-287` | pending |

---

### ARCH-001: Engine depends on UI header
**What to do:** Move `build_input_colors()` to UI layer or move theme.h constants to engine-layer header.
**Files to touch:** `src/engine/sensor_engine.cpp`, `include/neuroflyer/ui/theme.h`

### ARCH-002: FlySessionScreen static locals
**What to do:** Move `FlySessionState` and `NetViewerViewState` to member variables on the screen class. Init in `on_enter()`, cleanup in destructor.
**Files to touch:** `include/neuroflyer/ui/screens/fly_session_screen.h`, `src/ui/screens/game/fly_session_screen.cpp`

### CONS-001: Squad leader input computation duplicated
**What to do:** Create `compute_squad_leader_inputs()` function in `squad_leader.h/cpp`. Use `compute_dir_range()` internally.
**Files to touch:** `include/neuroflyer/squad_leader.h`, `src/engine/squad_leader.cpp`, `src/ui/screens/arena/arena_game_screen.cpp`, `src/engine/arena_match.cpp`

### CONS-002: Evolution functions 90% duplicated
**What to do:** Extract shared logic into `evolve_team_population_impl()` with a mutation callback.
**Files to touch:** `src/engine/team_evolution.cpp`, `include/neuroflyer/team_evolution.h`

### CONS-003: Fitness scoring duplicated
**What to do:** Extract `compute_team_fitness()` into shared function. Call from both sites.
**Files to touch:** New `arena_scoring.h/cpp` or add to `arena_match.cpp`

### STALE-005: Duplicated tick loop
**What to do:** Extract common per-tick logic into shared function with callback for visualization data capture.
**Files to touch:** `src/engine/arena_match.cpp`, `src/ui/screens/arena/arena_game_screen.cpp`

### DOC-001 through DOC-016: Documentation updates
**What to do:** Update CLAUDE.md with arena mode architecture, correct snapshot version, complete file tree, NetType enum, arena screen flow, and all missing references. Draft text provided in 03-documentation.md.
**Files to touch:** `CLAUDE.md`, `docs/feature-audit.md`

---

## Needs Discussion

| ID | Question | Context |
|----|----------|---------|
| ARCH-002 | How disruptive is the FlySessionScreen refactor? | It's the largest single file. May want to defer to a dedicated session. |
| STALE-005 | Should the headless runner be the canonical tick implementation? | If yes, ArenaGameScreen calls it. If no, delete it (see DEAD-001). |

---

## Status Tracker

| Total Items | Completed | Pending | Rejected | Blocked |
|-------------|-----------|---------|----------|---------|
| 46 | 0 | 46 | 0 | 0 |

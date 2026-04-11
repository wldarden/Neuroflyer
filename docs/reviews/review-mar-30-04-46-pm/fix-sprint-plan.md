# Fix Sprint Plan — Mar 30, 04:46 PM

> Things that are objectively wrong and should be fixed.
> See category reports for full details.
>
> **How to use:** Tell Claude:
> - "execute the fix sprint" — run all fixes
> - "skip FIX-ID" — reject a specific item
> - "what's the fix status?" — check progress

## Fixes

| ID | Category | Severity | Description | Location | Status |
|----|----------|----------|-------------|----------|--------|
| STALE-001 | Stale | CRITICAL | FighterDrillPauseScreen does not sync per-node activations when saving variants | `fighter_drill_pause_screen.cpp:249-273` | pending |
| STALE-002 | Stale | CRITICAL | NTM snapshots saved with NetType::Solo instead of dedicated NTM type | `arena_pause_screen.cpp:244` | pending |
| BUG-001 | Bug | CRITICAL | OOB access in ArenaSession when bullet owner_index is -1 | `arena_session.cpp:252-256` | pending |
| DEAD-003 | Dead Code | CRITICAL | ArenaConfig::world_diagonal() unused; inline sqrt duplicated everywhere | `arena_config.h:73-75`, `arena_session.cpp:375` | pending |
| BUG-002 | Bug | HIGH | FighterDrillScreen population/session size mismatch after pause screen edit | `fighter_drill_screen.cpp:153-175, 362-404` | pending |
| BUG-003 | Bug | HIGH | drill_ship_teams_ never resized after evolution | `fighter_drill_screen.cpp:171, 334` | pending |
| BUG-004 | Bug | HIGH | Division by zero in arena ray_circle_hit when sensor.range is zero | `arena_sensor.cpp:16-33` | pending |
| STALE-004 | Stale | HIGH | FighterDrillConfig has no scoring penalty for dead ships | `fighter_drill_session.cpp:246-292` | pending |
| STALE-005 | Stale | HIGH | FighterDrillPauseScreen saves variants to genome root, not squad/ dir | `fighter_drill_pause_screen.cpp:266` | pending |
| DEAD-001 | Dead Code | HIGH | render_variant_net() is never called externally | `variant_net_render.cpp:130-133` | pending |
| DEAD-002 | Dead Code | HIGH | ArenaConfig NTM/SquadLeader topology fields never read | `arena_config.h:50-57` | pending |
| ASM-001 | Assumption | HIGH | convert_variant_to_fighter hardcodes sensor column semantics | `evolution.cpp:242-322` | pending |
| ASM-002 | Assumption | HIGH | ArenaGameScreen hardcodes exactly 2 teams | `arena_game_screen.cpp:143-165` | pending |
| BUG-005 | Bug | MEDIUM | Arena occulus distance normalization inconsistent with raycast | `arena_sensor.cpp:121-137` | pending |
| BUG-006 | Bug | MEDIUM | FighterDrillSession::update_bullets doesn't destroy OOB bullets | `fighter_drill_session.cpp:176-181` | pending |
| BUG-008 | Bug | MEDIUM | Net viewer creates full Network copy via shared_ptr every frame | `net_viewer_view.cpp:90` | pending |
| STALE-006 | Stale | MEDIUM | variant_net_render switch has no explicit case NetType::Solo | `variant_net_render.cpp:24-61` | pending |
| STALE-007 | Stale | MEDIUM | FighterDrillSession does not resolve bullet-ship collisions | `fighter_drill_session.cpp:100-106` | pending |
| ASM-006 | Assumption | MEDIUM | Hardcoded 6 instead of ArenaConfig::squad_leader_fighter_inputs | `arena_sensor.cpp:218-224` | pending |
| DEAD-004 | Dead Code | MEDIUM | cast_rays() and cast_rays_with_endpoints() only called from tests | `ray.h:38-51`, `ray.cpp:9-103` | pending |
| DEAD-005 | Dead Code | MEDIUM | bullet_triangle_collision() (non-rotated) is dead | `collision.h:43-51` | pending |
| DEAD-006 | Dead Code | MEDIUM | check_collision/check_bullet_hit thin wrappers | `game.h:64,67` | pending |
| CONS-005 | Magic Numbers | MEDIUM | Tower obstacle radius range 15-35 unnamed in 3 files | `arena_session.cpp:92`, `fighter_drill_session.cpp:39`, `game.cpp:125` | pending |
| CONS-006 | Magic Numbers | MEDIUM | BULLET_RADIUS = 2.0f duplicated in arena_sensor.cpp | `arena_sensor.cpp:83, 172` | pending |
| BUG-007 | Bug | MEDIUM | ArenaSession bullets die at world boundary instead of wrapping | `arena_session.cpp:221-232` | pending |
| BUG-009 | Bug | LOW | compute_dir_range does not account for world wrapping | `arena_sensor.cpp:198-216` | pending |
| BUG-010 | Bug | LOW | Arena sensor raycast doesn't detect across wrap boundary | `arena_sensor.cpp:36-93` | pending |
| BUG-011 | Bug | LOW | FighterDrillPauseScreen copies entire population by value | `fighter_drill_pause_screen.h:16-23` | pending |
| STALE-008 | Stale | LOW | effective_ship_design() doesn't preserve is_full_sensor or type | `evolution.cpp:214-231` | pending |
| STALE-009 | Stale | LOW | squad_leader_fighter_inputs constant not referenced | `arena_config.h:60` | pending |
| ASM-007 | Assumption | MEDIUM | evolve_population uses config size, evolve_team_population uses vector size | `evolution.cpp:623`, `team_evolution.cpp:98` | pending |
| DEAD-007 | Dead Code | LOW | Legacy go_to_screen() and Screen enum still exist | `app_state.h:80`, `legacy_screen.h` | pending |
| DEAD-008 | Dead Code | LOW | world_diagonal() methods tested but never called from production | `fighter_drill_session.h:46`, `arena_config.h:73` | pending |
| STALE-003 | Stale | HIGH | ArenaConfig stale NTM/SL topology fields duplicate team_evolution.h | `arena_config.h:50-57` | pending |

### Verified Assumptions (No Action Required)

| ID | Status | Description |
|----|--------|-------------|
| ASM-003 | VERIFIED | Snapshot v7 serialization round-trip correct |
| ASM-004 | VERIFIED | compute_arena_input_size matches build_arena_ship_input |
| ASM-005 | PARTIALLY VERIFIED | NetType enum consistent for 3 current values |
| ASM-008 | VERIFIED | DrillPhase enum transitions correct |
| ASM-009 | VERIFIED | Triangle vertex positions consistent across collision and rendering |
| ASM-010 | VERIFIED | Snapshot weight/activation round-trip correct despite redundant storage |

---

### STALE-001: FighterDrillPauseScreen does not sync per-node activations when saving
**What to do:** Add the per-node activation sync loop between `snap.weights = ind.genome.flatten(...)` and `save_snapshot()`. Copy the pattern from `pause_config_screen.cpp:342-356` or `arena_pause_screen.cpp:212-225`.
**Files to touch:** `src/ui/screens/game/fighter_drill_pause_screen.cpp`

### STALE-002: NTM snapshots saved with NetType::Solo
**What to do:** Add `NTM = 3` to `NetType` enum. Update `arena_pause_screen.cpp:244` to use `NetType::NTM`. Add NTM case to `variant_net_render.cpp` switch with appropriate labels. Snapshot serialization already handles arbitrary uint8_t.
**Files to touch:** `include/neuroflyer/snapshot.h`, `src/ui/screens/arena/arena_pause_screen.cpp`, `src/ui/renderers/variant_net_render.cpp`

### BUG-001: OOB access when bullet owner_index is -1
**What to do:** Add guard `if (killer < team_assignments_.size())` before accessing `team_assignments_[killer]`, `ally_kills_[killer]`, and `enemy_kills_[killer]`.
**Files to touch:** `src/engine/arena_session.cpp`

### DEAD-003: ArenaConfig::world_diagonal() never called
**What to do:** Replace inline diagonal computations in `arena_session.cpp` and `arena_match.cpp` with `config_.world_diagonal()`.
**Files to touch:** `src/engine/arena_session.cpp`, `src/engine/arena_match.cpp`

### BUG-002: Population/session size mismatch after pause screen edit
**What to do:** In `evolve_generation()`, sync `drill_config_.population_size = population_.size()` before creating the new FighterDrillSession.
**Files to touch:** `src/ui/screens/game/fighter_drill_screen.cpp`

### BUG-003: drill_ship_teams_ never resized
**What to do:** Add `drill_ship_teams_.assign(population_.size(), 0);` in `evolve_generation()` alongside the `recurrent_states_` resize.
**Files to touch:** `src/ui/screens/game/fighter_drill_screen.cpp`

### BUG-004: Division by zero when sensor.range is zero
**What to do:** Add `if (range < 1e-6f) return 1.0f;` at the top of `ray_circle_hit()`.
**Files to touch:** `src/engine/arena_sensor.cpp`

### STALE-004: No scoring penalty for dead ships in drill
**What to do:** Add `float death_penalty = -200.0f` to `FighterDrillConfig`. In `resolve_ship_tower_collisions()`, subtract penalty when `ships_[i].alive = false`.
**Files to touch:** `include/neuroflyer/fighter_drill_session.h`, `src/engine/fighter_drill_session.cpp`

### STALE-005: Fighter drill variants saved to wrong directory
**What to do:** Use `save_squad_variant(genome_dir_, snap)` instead of direct `save_snapshot(snap, path)`.
**Files to touch:** `src/ui/screens/game/fighter_drill_pause_screen.cpp`

### DEAD-001: Unused render_variant_net()
**What to do:** Remove `render_variant_net()` from `variant_net_render.h` and `variant_net_render.cpp`.
**Files to touch:** `include/neuroflyer/renderers/variant_net_render.h`, `src/ui/renderers/variant_net_render.cpp`

### DEAD-002 / STALE-003: Unused ArenaConfig NTM/SquadLeader topology fields
**What to do:** Remove the six unused fields (`ntm_input_size`, `ntm_hidden_sizes`, `ntm_output_size`, `squad_leader_input_size`, `squad_leader_hidden_sizes`, `squad_leader_output_size`) from `ArenaConfig`.
**Files to touch:** `include/neuroflyer/arena_config.h`

### ASM-001: convert_variant_to_fighter hardcodes sensor column semantics
**What to do:** Extract `SCROLLER_FULL_SENSOR_COLS = 4` and `ARENA_FULL_SENSOR_COLS = 5` as named constants. Reference them in both the input builders and the converter.
**Files to touch:** `src/engine/evolution.cpp`, `src/engine/arena_sensor.cpp`, `src/engine/sensor_engine.cpp`

### ASM-002: ArenaGameScreen hardcodes exactly 2 teams
**What to do:** Add `assert(config_.num_teams == 2)` in `initialize()` until multi-team support is implemented. Document the constraint.
**Files to touch:** `src/ui/screens/arena/arena_game_screen.cpp`

### BUG-005: Occulus distance normalization inconsistent
**What to do:** Document the difference between raycast and occulus normalization, or normalize both by `sensor.range`.
**Files to touch:** `src/engine/arena_sensor.cpp`

### BUG-006: FighterDrillSession doesn't destroy OOB bullets
**What to do:** Add boundary destruction in `update_bullets()`, matching ArenaSession's approach.
**Files to touch:** `src/engine/fighter_drill_session.cpp`

### BUG-008: Net viewer copies Network every frame
**What to do:** Cache the shared_ptr and only recreate when `state.network` pointer changes. Add `last_network_ptr` to `NetViewerViewState`.
**Files to touch:** `src/ui/views/net_viewer_view.cpp`, `include/neuroflyer/ui/views/net_viewer_view.h`

### STALE-006: No explicit case NetType::Solo in variant_net_render
**What to do:** Replace `default:` with explicit `case NetType::Solo:` so compiler warns on new unhandled values.
**Files to touch:** `src/ui/renderers/variant_net_render.cpp`

### STALE-007: FighterDrillSession missing bullet-ship collisions
**What to do:** Either add `resolve_bullet_ship_collisions()` or add a comment documenting intentional omission.
**Files to touch:** `src/engine/fighter_drill_session.cpp`

### ASM-006: Hardcoded 6 instead of constant
**What to do:** Replace hardcoded `6` with `ArenaConfig::squad_leader_fighter_inputs` in `compute_arena_input_size`, `build_arena_ship_input`, `build_arena_fighter_input_labels`, `build_arena_fighter_input_colors`.
**Files to touch:** `src/engine/arena_sensor.cpp`

### DEAD-004: Dead cast_rays/cast_rays_with_endpoints
**What to do:** Remove from `ray.h` and `ray.cpp`. Remove or rewrite `tests/ray_test.cpp`.
**Files to touch:** `include/neuroflyer/ray.h`, `src/engine/ray.cpp`, `tests/ray_test.cpp`

### DEAD-005: Dead non-rotated bullet_triangle_collision
**What to do:** Remove from `collision.h`. Update `tests/game_test.cpp` to test the rotated variant.
**Files to touch:** `include/neuroflyer/collision.h`, `tests/game_test.cpp`

### DEAD-006: Thin wrapper check_collision/check_bullet_hit
**What to do:** Low priority. Inline alive checks at call sites or keep as-is.
**Files to touch:** `include/neuroflyer/game.h`, `src/engine/game.cpp`

### CONS-005: Magic tower radius range
**What to do:** Define `TOWER_MIN_RADIUS = 15.0f` and `TOWER_MAX_RADIUS = 35.0f` in `game.h`. Replace 3 hardcoded sites.
**Files to touch:** `include/neuroflyer/game.h`, `src/engine/arena_session.cpp`, `src/engine/fighter_drill_session.cpp`, `src/engine/game.cpp`

### CONS-006: Magic BULLET_RADIUS
**What to do:** Add `static constexpr float SENSOR_RADIUS = 2.0f` to `Bullet` struct. Replace both local definitions.
**Files to touch:** `include/neuroflyer/game.h`, `src/engine/arena_sensor.cpp`

### BUG-007: Bullets don't wrap at world boundary
**What to do:** Either wrap bullets when wrapping is enabled, or document as intentional.
**Files to touch:** `src/engine/arena_session.cpp`

### BUG-009 / BUG-010: No world-wrap awareness in dir_range and sensors
**What to do:** Add wrap-aware distance computation. Low priority for large worlds but significant for drill (4000x4000).
**Files to touch:** `src/engine/arena_sensor.cpp`

### BUG-011: Population copied by value on pause
**What to do:** Change constructor to accept `const std::vector<Individual>&` or `std::span<const Individual>`.
**Files to touch:** `include/neuroflyer/ui/screens/fighter_drill_pause_screen.h`, `src/ui/screens/game/fighter_drill_pause_screen.cpp`

### STALE-008: effective_ship_design() doesn't preserve is_full_sensor/type
**What to do:** Add warning comment, or accept a template ShipDesign to copy sensor type/flags from.
**Files to touch:** `src/engine/evolution.cpp`

### STALE-009 / ASM-006: squad_leader_fighter_inputs constant not referenced
**What to do:** Already covered by ASM-006 fix above.

### ASM-007: Population size source inconsistency
**What to do:** Document the intentional difference or unify. Related to BUG-002.
**Files to touch:** `src/engine/evolution.cpp`, `src/engine/team_evolution.cpp`

### DEAD-007: Legacy screen enum
**What to do:** Audit if `go_to_screen()` is still called. If not, remove it and the bridge.
**Files to touch:** `include/neuroflyer/app_state.h`, `include/neuroflyer/ui/legacy_screen.h`, `src/ui/main.cpp`

### DEAD-008: Dead world_diagonal methods
**What to do:** Wire up production calls (DEAD-003 fix) or remove.
**Files to touch:** Already covered by DEAD-003.

---

## Needs Discussion

| ID | Question | Context |
|----|----------|---------|
| BUG-007 | Should bullets wrap like ships, or is boundary destruction intentional? | Wrapping bullets requires cross-boundary collision detection |
| STALE-007 | Should fighter drill have friendly fire? | Current drill has all ships on same team; no FF matches arena training |
| STALE-004 | What's the right death penalty value? | -200 suggested; needs tuning via training runs |
| DEAD-007 | Is anything still using the legacy Screen enum? | Needs audit before removal |
| ASM-002 | Should arena support 3+ teams? | Currently hardcoded to 2; assertion is a stopgap |

---

## Status Tracker

| Total Items | Completed | Pending | Rejected | Blocked |
|-------------|-----------|---------|----------|---------|
| 34 | 0 | 34 | 0 | 0 |

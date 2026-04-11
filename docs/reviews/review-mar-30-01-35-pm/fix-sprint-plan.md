# Fix Sprint Plan — Mar 30, 01:35 PM

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
| BUG-001 | Bug | CRITICAL | Only team genomes 0 and 1 ever get fitness — evolution broken | `arena_game_screen.cpp:123` | pending |
| STALE-001 | Stale | CRITICAL | Occulus sensors silently broken in arena mode (treated as raycasts) | `arena_sensor.cpp:33-96` | pending |
| STALE-002 | Stale | CRITICAL | Fighter net viewer shows scroller labels instead of arena labels | `variant_net_render.cpp:35-44` | pending |
| BUG-003 | Bug | HIGH | Triangle collision ignores rotation — wrong in arena mode | `collision.h:43-61` | pending |
| STALE-003 | Stale | HIGH | Arena has no pause screen — can't save trained variants | `arena_game_screen.cpp:217-219` | pending |
| BUG-005 | Bug | MEDIUM | Stale net_viewer individual pointer after evolution | `arena_game_screen.cpp:484` | pending |
| BUG-007 | Bug | MEDIUM | convert_variant_to_fighter drops is_token weight | `evolution.cpp:278-287` | pending |
| STALE-009 | Stale | MEDIUM | ArenaConfig NTM/squad topology fields never read | `arena_config.h:48-56` | pending |
| STALE-010 | Stale | MEDIUM | NTM + squad leader runs for dead teams (wasted computation) | `arena_game_screen.cpp:252-316` | pending |
| BUG-004 | Bug | HIGH | Division by zero in compute_dir_range with zero world | `arena_sensor.cpp:106` | pending |
| BUG-006 | Bug | MEDIUM | ray_circle_hit division by zero with zero-range sensor | `arena_sensor.cpp:24` | pending |
| BUG-010 | Bug | LOW | build_display_order uses 4 per full sensor, should be 5 for arena | `sensor_engine.cpp:368` | pending |
| STALE-004 | Stale | HIGH | convert_variant_to_fighter lacks input size validation assertion | `evolution.cpp:248-253` | pending |
| STALE-012 | Stale | LOW | Squad net lineage not tracked (parent always empty) | `genome_manager.cpp` | pending |
| CONS-005 | Consolidation | MEDIUM | BULLET_RADIUS magic number inconsistent with collision radius | `arena_sensor.cpp:86` | pending |
| CONS-006 | Consolidation | MEDIUM | world_diag recomputed in 4+ locations | Multiple files | pending |
| CONS-013 | Consolidation | LOW | Inconsistent sensor counting convention between scroller/arena | `arena_sensor.cpp` | pending |
| DEAD-002 | Dead Code | HIGH | Legacy screen system completely dead (go_to_screen, Screen enum) | `app_state.h, legacy_screen.h, ui_manager.cpp` | pending |
| DEAD-001 | Dead Code | HIGH | run_arena_match() dead in production, only tests | `arena_match.h/cpp` | pending |
| DEAD-003 | Dead Code | MEDIUM | cast_rays functions never called | `ray.h/ray.cpp` | pending |
| DEAD-009 | Dead Code | LOW | Unused #include arena_match.h in arena_game_screen.h | `arena_game_screen.h:4` | pending |
| DEAD-010 | Dead Code | LOW | sensor_engine.cpp includes ui/theme.h (arch violation) | `sensor_engine.cpp:3` | pending |
| DEAD-011 | Dead Code | LOW | ArenaConfig NTM/squad fields are dead data | `arena_config.h:48-56` | pending |
| ARCH-007 | Architecture | LOW | M_PI instead of std::numbers::pi_v | `arena_session.cpp:45,78` | pending |

---

### BUG-001: Arena evolution broken — only 2 of 20 genomes evaluated
**What to do:** Implement round-robin or random matchmaking so all team genomes get evaluated each generation. Track cumulative fitness across rounds.
**Files to touch:** `src/ui/screens/arena/arena_game_screen.cpp`

### STALE-001: Occulus sensors broken in arena mode
**What to do:** Add `switch(sensor.type)` to `query_arena_sensor()`. Port ellipse overlap logic from scroller's `query_occulus()`. Add tests.
**Files to touch:** `src/engine/arena_sensor.cpp`, `tests/arena_sensor_test.cpp`

### STALE-002: Fighter net viewer shows wrong labels
**What to do:** Add `build_arena_fighter_input_labels()`, `build_arena_fighter_input_colors()`, `build_arena_fighter_display_order()`. Add `NetType::Fighter` branch in `variant_net_render.cpp`.
**Files to touch:** `src/engine/arena_sensor.cpp` or `include/neuroflyer/arena_sensor.h`, `src/ui/renderers/variant_net_render.cpp`

### BUG-003: Triangle collision ignores rotation
**What to do:** Add rotation-aware collision variant. Use in `ArenaSession::resolve_bullet_ship_collisions()`. Keep original for scroller mode.
**Files to touch:** `include/neuroflyer/collision.h`, `src/engine/arena_session.cpp`

### STALE-003: Arena has no pause screen
**What to do:** Create `ArenaPauseScreen` or extend `PauseConfigScreen`. Wire Space key to push it. Implement Save Variants tab for squad/fighter nets.
**Files to touch:** New screen file(s), `src/ui/screens/arena/arena_game_screen.cpp`

### BUG-005: Stale individual pointer after evolution
**What to do:** Clear `net_viewer_state_.individual = nullptr` in `do_arena_evolution()` before population is replaced.
**Files to touch:** `src/ui/screens/arena/arena_game_screen.cpp`

### BUG-007: is_token weight dropped during conversion
**What to do:** Map `old_weights[src_col + 3]` (scroller is_token) to `new_weights[dst_col + 2]` (arena is_token).
**Files to touch:** `src/engine/evolution.cpp`

### STALE-009: ArenaConfig topology fields disconnected
**What to do:** Wire `ArenaGameScreen::initialize()` to read NTM/squad config from `config_`, OR remove dead fields from ArenaConfig.
**Files to touch:** `src/ui/screens/arena/arena_game_screen.cpp`, `include/neuroflyer/arena_config.h`

### STALE-010: Skip dead teams in NTM loop
**What to do:** Add `if (stats.alive_fraction < 1e-6f) continue;` at top of per-team NTM loop.
**Files to touch:** `src/ui/screens/arena/arena_game_screen.cpp`

### BUG-004: Division by zero in compute_dir_range
**What to do:** Add guard: `if (world_diag < 1e-6f) world_diag = 1.0f;`.
**Files to touch:** `src/engine/arena_sensor.cpp`

### BUG-006: ray_circle_hit div-by-zero
**What to do:** Add early return `if (a < 1e-12f) return 1.0f;`.
**Files to touch:** `src/engine/arena_sensor.cpp`

### BUG-010: build_display_order wrong for arena
**What to do:** Subsumed by STALE-002 fix — create arena-specific display order.
**Files to touch:** `src/engine/sensor_engine.cpp`

### STALE-004: Missing assertion in convert_variant_to_fighter
**What to do:** Add `assert(src_col + 3 + design.memory_slots == old_input)` after sensor loop.
**Files to touch:** `src/engine/evolution.cpp`

### STALE-012: Squad net lineage flat
**What to do:** Set `snap.parent_name` when saving squad nets after training.
**Files to touch:** `src/ui/screens/hangar/variant_viewer_screen.cpp`

### CONS-005: BULLET_RADIUS magic number
**What to do:** Add `static constexpr float RADIUS = 2.0f` to `Bullet` struct. Use consistently.
**Files to touch:** `include/neuroflyer/game.h`, `src/engine/arena_sensor.cpp`, `src/engine/arena_session.cpp`

### CONS-006: world_diag recomputed everywhere
**What to do:** Add `float world_diagonal() const` to `ArenaConfig`. Use everywhere.
**Files to touch:** `include/neuroflyer/arena_config.h`, multiple consumer files

### CONS-013: Sensor counting convention inconsistency
**What to do:** Add comments documenting the convention or add `SensorDef::input_count()`.
**Files to touch:** `include/neuroflyer/ship_design.h`, `src/engine/arena_sensor.cpp`

### DEAD-002: Legacy screen system dead
**What to do:** Remove `go_to_screen()`, `Screen` enum, `LegacyScreen`, `sync_legacy_navigation()`.
**Files to touch:** `include/neuroflyer/app_state.h`, `include/neuroflyer/ui/legacy_screen.h`, `src/ui/framework/ui_manager.cpp`, `src/ui/main.cpp`

### DEAD-001: run_arena_match() dead production code
**What to do:** Either extract shared tick logic from ArenaGameScreen (preferred), or delete arena_match.h/cpp and rewrite tests.
**Files to touch:** `src/engine/arena_match.cpp`, `include/neuroflyer/arena_match.h`, `tests/arena_match_test.cpp`

### DEAD-003: cast_rays never called
**What to do:** Remove `cast_rays()` and `cast_rays_with_endpoints()` from ray.h/cpp. Update ray_test.cpp.
**Files to touch:** `include/neuroflyer/ray.h`, `src/engine/ray.cpp`, `tests/ray_test.cpp`

### DEAD-009: Unused include
**What to do:** Remove `#include <neuroflyer/arena_match.h>` from `arena_game_screen.h`.
**Files to touch:** `include/neuroflyer/ui/screens/arena_game_screen.h`

### DEAD-010: Engine includes UI header
**What to do:** Move color constants out of theme.h into engine-layer header. Remove include.
**Files to touch:** `src/engine/sensor_engine.cpp`, new header or existing `sensor_engine.h`

### DEAD-011: Dead ArenaConfig topology fields
**What to do:** Subsumed by STALE-009 — either wire them or remove them.
**Files to touch:** See STALE-009.

### ARCH-007: M_PI instead of std::numbers
**What to do:** Replace `static_cast<float>(M_PI)` with `std::numbers::pi_v<float>`.
**Files to touch:** `src/engine/arena_session.cpp`

---

## Needs Discussion

| ID | Question | Context |
|----|----------|---------|
| STALE-003 | How much arena pause screen functionality is needed for Phase 1? | Full pause screen is a multi-day effort. Minimal "save best squad net" button could be done in hours. |
| BUG-001 | Round-robin vs random matchmaking? | Round-robin is fairer but O(N²). Random grouping is simpler. |
| DEAD-001 | Keep headless match runner or delete? | If headless training (no GUI) is planned, keep and deduplicate. Otherwise delete. |

---

## Status Tracker

| Total Items | Completed | Pending | Rejected | Blocked |
|-------------|-----------|---------|----------|---------|
| 24 | 0 | 24 | 0 | 0 |

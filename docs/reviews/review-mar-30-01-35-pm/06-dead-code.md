# Dead Code Review -- 2026-03-30

Scope: full NeuroFlyer repo. Last 30 commits add arena mode with squad leaders, NTM, sector grid, net type tabs.

---

## [HIGH] `run_arena_match()` is dead production code -- only called from tests

**ID:** `DEAD-001`
**Location:** `include/neuroflyer/arena_match.h:22-26`, `src/engine/arena_match.cpp:12-242`
**Description:** `run_arena_match()` is a standalone headless match runner. It was the original arena execution path but was superseded by `ArenaGameScreen::tick_arena()`, which duplicates the exact same NTM + squad leader + fighter tick loop inline. The function is never called from any production code -- only from `tests/arena_match_test.cpp`. The header is also `#include`'d in `arena_game_screen.h` but no symbols from it are used.
**Impact:** 240 lines of duplicated arena tick logic. If the tick loop in `ArenaGameScreen` is updated (e.g., new NTM parameters, new inputs), `arena_match.cpp` will silently drift. Tests will pass against stale logic.
**Fix plan:**
1. Make `ArenaGameScreen::tick_arena()` call a shared function (either `run_arena_match` or a new `tick_one_frame()` extracted from the common logic).
2. Remove the unused `#include <neuroflyer/arena_match.h>` from `arena_game_screen.h`.
3. Or: if headless match running is no longer planned, delete `arena_match.h/cpp` and update `tests/arena_match_test.cpp` to test via `ArenaSession` directly.

---

## [HIGH] Legacy screen system is completely dead: `go_to_screen()`, `Screen` enum, `LegacyScreen`, `sync_legacy_navigation()`

**ID:** `DEAD-002`
**Location:** `include/neuroflyer/app_state.h:13-21,79-82`, `include/neuroflyer/ui/legacy_screen.h:1-34`, `src/ui/framework/ui_manager.cpp:113-170`
**Description:** The migration from the legacy `Screen` enum + `go_to_screen()` to the `UIManager` push/pop stack is complete. Evidence:
- `go_to_screen()` is defined in `app_state.h:79` but **never called** from any `.cpp` file.
- `LegacyScreen` (the adapter class in `legacy_screen.h`) is **never instantiated** anywhere.
- `UIManager::sync_legacy_navigation()` is called every frame in `main.cpp:113` but `state.current` is never mutated (no code calls `go_to_screen`), so it is a no-op. The 60-line switch statement in `ui_manager.cpp:113-170` is dead.
- `AppState::current` and `AppState::previous` (the `Screen` enum fields) are only read by the dead sync function.
**Impact:** ~100 lines of dead bridge code executed every frame. Confusing for new contributors who might think the legacy system is still active. The `Screen` enum in `AppState` also bloats the struct with unused fields.
**Fix plan:**
1. Remove `go_to_screen()` from `app_state.h`.
2. Remove `Screen current` and `Screen previous` from `AppState`.
3. Remove the `Screen` enum from `app_state.h`.
4. Delete `include/neuroflyer/ui/legacy_screen.h`.
5. Remove `sync_legacy_navigation()` from `UIManager` (declaration and the 60-line implementation).
6. Remove the call to `ui.sync_legacy_navigation(state, renderer)` from `main.cpp:113`.
7. Remove the `#include` of `legacy_screen.h` from `ui_manager.h` if present.

---

## [MEDIUM] `cast_rays()` and `cast_rays_with_endpoints()` in `ray.h/ray.cpp` are dead functions

**ID:** `DEAD-003`
**Location:** `include/neuroflyer/ray.h:38-51`, `src/engine/ray.cpp:9-107`
**Description:** The two main functions in `ray.cpp` (`cast_rays` and `cast_rays_with_endpoints`) are **never called** from any production source file. The sensor engine (`sensor_engine.cpp`) implements its own ray-circle intersection inline via `query_raycast()` and does NOT delegate to `ray.cpp`. The `HitType` enum, `RayResult`, `RayEndpoint`, and `ray_range_multiplier()` are all still used (by `sensor_engine.h/cpp`, `game_view.cpp`, `test_bench.cpp`, `fly_session_screen.cpp`), but the two functions that actually cast arrays of rays are dead.
The file is compiled in both the main executable and the test executable. `tests/ray_test.cpp` tests `cast_rays` directly.
**Impact:** 100 lines of dead implementation code. The test file `ray_test.cpp` covers only this dead code path, giving false confidence.
**Fix plan:**
1. Remove `cast_rays()` and `cast_rays_with_endpoints()` declarations from `ray.h`.
2. Remove the implementations from `ray.cpp` (if the file becomes empty, remove `ray.cpp` from both CMakeLists.txt and keep `ray.h` as a header-only file for `HitType`, `RayResult`, `RayEndpoint`, `ray_range_multiplier`).
3. Delete or rewrite `tests/ray_test.cpp` to test `query_sensor()` from sensor_engine instead.

---

## [MEDIUM] `UIManager::replace_screen()` is defined but never called

**ID:** `DEAD-004`
**Location:** `src/ui/framework/ui_manager.cpp:40-48`, `include/neuroflyer/ui/ui_manager.h`
**Description:** `replace_screen()` is implemented (pops all screens, pushes a new root) but has zero callers. The comment says "Currently unused -- available for future use."
**Impact:** 10 lines of dead code. Low risk but adds API surface that may mislead callers into using it instead of the documented push/pop pattern.
**Fix plan:** Remove the method declaration and implementation. Re-add if/when needed.

---

## [MEDIUM] `UIManager::has_screens()` and `UIManager::active_screen()` are defined but never called externally

**ID:** `DEAD-005`
**Location:** `src/ui/framework/ui_manager.cpp:50-57`, `include/neuroflyer/ui/ui_manager.h:25-26`
**Description:** Both public methods are implemented but never called from any source file outside `ui_manager.cpp` itself (and they are not called internally either -- the manager accesses `screen_stack_` directly).
**Impact:** Minor dead API surface.
**Fix plan:** Remove or mark as internal. Low priority.

---

## [MEDIUM] `game-ui/modal.h` and `game-ui/modal.cpp` are compiled but never used

**ID:** `DEAD-006`
**Location:** `game-ui/include/game-ui/components/modal.h`, `game-ui/src/components/modal.cpp`, `CMakeLists.txt:124`
**Description:** The `gameui::draw_confirm_modal`, `gameui::draw_input_modal`, and `gameui::draw_choice_modal` functions are compiled into the neuroflyer executable via `CMakeLists.txt:124` but **no source file includes `game-ui/components/modal.h`**. The app uses `UIModal` subclasses (`ConfirmModal`, `InputModal`) from the neuroflyer-specific modal framework instead. Only `game-ui/components/highlight_list.h` is actually used (by `hangar_screen.cpp`).
**Impact:** Dead compiled code. May cause linker symbol bloat and confusion about which modal system to use.
**Fix plan:**
1. Remove `game-ui/src/components/modal.cpp` from the `add_executable` list in `CMakeLists.txt`.
2. Optionally delete the unused `modal.h` and `modal.cpp` files.

---

## [MEDIUM] `ArenaSession::add_bullet()` is a public method only used by tests

**ID:** `DEAD-007`
**Location:** `include/neuroflyer/arena_session.h:31`, `src/engine/arena_session.cpp:328`
**Description:** `add_bullet()` is declared public and has an implementation, but is only called from `tests/arena_session_test.cpp` (6 call sites). No production code calls it -- arena bullets are spawned internally via `spawn_bullets_from_ships()`. It is a test helper masquerading as a public API method.
**Impact:** Misleading public API. Low risk.
**Fix plan:** Either make it private and friend the test, or accept it as a test-only utility.

---

## [MEDIUM] `ArenaSession::squad_of()` is only used in tests

**ID:** `DEAD-008`
**Location:** `include/neuroflyer/arena_session.h:36`, `src/engine/arena_session.cpp:343`
**Description:** `squad_of()` is declared and implemented but never called from production code. Only called from `tests/arena_session_test.cpp` (6 times). The arena game screen's tick loop uses `team_of()` but never `squad_of()`. This is likely intended for multi-squad support (currently `num_squads=1`), so it is premature rather than dead.
**Impact:** Low -- correctly anticipates future use. But currently dead.
**Fix plan:** Keep for now (multi-squad is on the roadmap). Document as future-use.

---

## [LOW] `#include <neuroflyer/arena_match.h>` in `arena_game_screen.h` is unused

**ID:** `DEAD-009`
**Location:** `include/neuroflyer/ui/screens/arena_game_screen.h:4`
**Description:** The arena game screen header includes `arena_match.h` but does not use `ArenaMatchResult` or `run_arena_match`. This was likely left over from an earlier design where the screen delegated to the standalone runner.
**Impact:** Unnecessary compile dependency. Pulls in `arena_session.h`, `arena_sensor.h`, `team_evolution.h` transitively.
**Fix plan:** Remove the include.

---

## [LOW] `sensor_engine.cpp` includes `ui/theme.h` -- engine code importing UI header

**ID:** `DEAD-010`
**Location:** `src/engine/sensor_engine.cpp:3`
**Description:** `sensor_engine.cpp` is in `src/engine/` (pure logic, zero SDL/ImGui deps per architecture rules) but includes `<neuroflyer/ui/theme.h>`. The theme header is a constexpr-only header (no SDL/ImGui includes of its own), so it compiles fine, but it violates the architectural boundary. The sensor engine uses `theme::node_sight`, `theme::node_sensor`, etc. for `build_input_colors()`.
**Impact:** Architectural violation. The `NodeStyle` colors returned by `build_input_colors()` are already a UI-agnostic struct. The actual color values belong in the component, not the engine.
**Fix plan:** Move the color constants used by `build_input_colors()` into `sensor_engine.h` or a standalone `sensor_colors.h` in the engine layer. Remove the `#include <neuroflyer/ui/theme.h>` from `sensor_engine.cpp`.

---

## [LOW] `ArenaConfig` NTM/squad-leader topology fields duplicate `NtmNetConfig` and `SquadLeaderNetConfig`

**ID:** `DEAD-011`
**Location:** `include/neuroflyer/arena_config.h:48-56`, `include/neuroflyer/team_evolution.h:13-23`
**Description:** `ArenaConfig` has `ntm_input_size`, `ntm_hidden_sizes`, `ntm_output_size`, `squad_leader_input_size`, etc. These duplicate the structs `NtmNetConfig` and `SquadLeaderNetConfig` in `team_evolution.h`. The `ArenaGameScreen` creates its own `ntm_config_` and `leader_config_` members and never reads the `ArenaConfig` topology fields.
**Impact:** The `ArenaConfig` topology fields are written but never read -- they are dead data. Changes to them in the config screen have no effect.
**Fix plan:** Either wire `ArenaGameScreen::initialize()` to read from `config_.ntm_input_size` etc., or remove those fields from `ArenaConfig`.

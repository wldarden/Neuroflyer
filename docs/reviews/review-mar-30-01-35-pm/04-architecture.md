# Architecture Review

**Date:** 2026-03-30
**Scope:** Full NeuroFlyer repo, C++20 / SDL2 / ImGui
**Focus:** Engine/UI separation, 4-layer UI framework compliance, resource management, const correctness, naming conventions

---

## [HIGH] Engine file depends on UI header (theme.h)

**ID:** `ARCH-001`
**Location:** `src/engine/sensor_engine.cpp:3`
**Description:** `sensor_engine.cpp` includes `<neuroflyer/ui/theme.h>` for the `build_input_colors()` function. This function converts `theme::Color` values into `NodeStyle` structs (lines 331-351). The `theme.h` header is under the `ui/` directory, which violates the architectural rule that engine code has zero UI dependencies.
**Impact:** Creates an upward dependency from the engine layer to the UI layer. If `theme.h` were to gain SDL/ImGui includes in the future, the engine would transitively depend on those. It also muddies the conceptual boundary: node colors are a presentation concern that should not be in the sensor engine.
**Fix plan:**
1. Move `build_input_colors()` out of `sensor_engine.cpp` to a new file `src/ui/renderers/input_colors.cpp` (or add it to `variant_net_render.cpp`).
2. Alternatively, move `theme.h` to a non-UI location (e.g., `include/neuroflyer/theme.h`) since it contains only pure data constants with no SDL/ImGui deps. This is the lower-risk option.
3. Remove the `#include <neuroflyer/ui/theme.h>` from `sensor_engine.cpp`.

---

## [HIGH] FlySessionScreen uses static locals for all state

**ID:** `ARCH-002`
**Location:** `src/ui/screens/game/fly_session_screen.cpp:28,713`
**Description:** `FlySessionScreen` stores its entire state in two function-static singletons: `get_fly_net_viewer_state()` (line 28) returns a `static NetViewerViewState`, and `get_fly_session_state()` (line 713) returns a `static FlySessionState`. The screen class itself is empty (no member variables at all -- see `fly_session_screen.h`). The `CLAUDE.md` rules explicitly say "No static locals for UI state. All state on the class. Static locals cause bugs when re-entering screens."
**Impact:** Re-entering the fly session screen (e.g., popping to hangar, pushing a new fly session) reuses stale state from the previous session. The `initialized` flag partially mitigates this via `reset()`, but SDL texture handles in `NetViewerViewState` may leak or alias. This is the single largest framework violation in the codebase.
**Fix plan:**
1. Move `FlySessionState` from the global `get_fly_session_state()` to a member variable on `FlySessionScreen`.
2. Move `NetViewerViewState` to a member variable on `FlySessionScreen`.
3. Initialize both in `on_enter()`. Clean up `NetViewerViewState` textures in the destructor.
4. Remove both `get_*` functions.

---

## [MEDIUM] ArenaGameView instantiated per frame, not as a member

**ID:** `ARCH-003`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:430`
**Description:** `render_arena()` constructs `ArenaGameView view(renderer.renderer_)` as a local variable every frame. While `ArenaGameView` is lightweight (one pointer + four ints), creating and destroying it 60+ times per second is unnecessary allocation churn. Worse, if `ArenaGameView` ever acquires SDL textures or other resources, this pattern would cause per-frame allocation/deallocation.
**Impact:** Minor performance waste today. Architectural debt if the view grows. Inconsistent with how `ArenaGameScreen` already holds `net_viewer_state_` as a member.
**Fix plan:**
1. Add `ArenaGameView game_view_` as a member of `ArenaGameScreen`.
2. Initialize it in `initialize()` when the SDL renderer is available.
3. Call `game_view_.set_bounds(...)` and `game_view_.render(...)` each frame.

---

## [MEDIUM] Renderer exposes public member variables (`renderer_`, `window_`)

**ID:** `ARCH-004`
**Location:** `include/neuroflyer/renderer.h:55-56`
**Description:** `Renderer::renderer_` (the raw `SDL_Renderer*`) and `Renderer::window_` (the raw `SDL_Window*`) are public member variables. Multiple files access `renderer.renderer_` directly (13 call sites across 8 files). This exposes internal implementation details and bypasses any encapsulation the `Renderer` class provides.
**Impact:** Any file can manipulate the SDL renderer state arbitrarily (blend mode, draw color, clip rect) without going through Renderer, making it impossible to reason about renderer state at any given point. It also makes Renderer impossible to mock for testing.
**Fix plan:**
1. Make `renderer_` and `window_` private (they already have trailing underscores suggesting private intent).
2. The existing `game_w()`, `screen_h()`, etc. accessors handle layout queries. Add a `sdl_renderer()` accessor for the few places that legitimately need the raw pointer (view rendering, net viewer).
3. Update the 13 call sites to use the accessor.

---

## [MEDIUM] ArenaConfig has redundant net topology fields duplicated in NtmNetConfig/SquadLeaderNetConfig

**ID:** `ARCH-005`
**Location:** `include/neuroflyer/arena_config.h:49-56` and `include/neuroflyer/team_evolution.h:13-23`
**Description:** `ArenaConfig` defines `ntm_input_size`, `ntm_hidden_sizes`, `ntm_output_size`, `squad_leader_input_size`, `squad_leader_hidden_sizes`, `squad_leader_output_size`. Meanwhile, `NtmNetConfig` and `SquadLeaderNetConfig` in `team_evolution.h` define the exact same fields with identical defaults. `ArenaGameScreen` uses `NtmNetConfig` / `SquadLeaderNetConfig` members directly (lines 47-48) and never reads the `ArenaConfig` topology fields.
**Impact:** Two sources of truth for the same configuration. If someone changes the topology in `ArenaConfig`, nothing happens because `ArenaGameScreen` uses its own defaults. This is a latent bug waiting for someone to wire the config screen sliders to the wrong struct.
**Fix plan:**
1. Remove the topology fields from `ArenaConfig` (they are not wired to any UI sliders yet).
2. Or, keep them in `ArenaConfig` and have `ArenaGameScreen::initialize()` construct `NtmNetConfig` and `SquadLeaderNetConfig` from the config values.
3. Either way, have exactly one source of truth.

---

## [MEDIUM] collision.h uses non-rotated vertex positions for triangle collision

**ID:** `ARCH-006`
**Location:** `include/neuroflyer/collision.h:43-61`
**Description:** `bullet_triangle_collision()` and `triangle_circle_collision()` use fixed vertex offsets `(x, y-SIZE)`, `(x-SIZE, y+SIZE)`, `(x+SIZE, y+SIZE)`. These are the default facing-up positions. In arena mode, ships rotate freely (`Triangle::rotation` field). The collision functions ignore rotation entirely, meaning a ship facing right has its collision shape still pointing up.
**Impact:** In the scroller game mode, ships always face up, so this is correct. In arena mode, the comment in `arena_session.cpp:243-247` explicitly acknowledges this with a fallback distance check: `hit = (dx*dx + dy*dy) < (Triangle::SIZE * Triangle::SIZE)`. This fallback makes the vertex-based check partially redundant. The collision accuracy degrades as ships turn sideways.
**Fix plan:**
1. Add a `bullet_triangle_collision_rotated()` variant that applies `Triangle::rotation` to vertex positions before checking.
2. Use the rotated version in `ArenaSession::resolve_bullet_ship_collisions()`.
3. The existing non-rotated version stays for the scroller mode.

---

## [LOW] M_PI used instead of std::numbers::pi_v in arena_session.cpp

**ID:** `ARCH-007`
**Location:** `src/engine/arena_session.cpp:45,78`
**Description:** Two uses of `static_cast<float>(M_PI)` instead of `std::numbers::pi_v<float>`. The rest of the codebase consistently uses `std::numbers::pi_v<float>` (C++20 style). `M_PI` is POSIX-specific and not guaranteed by the C++ standard.
**Impact:** Portability risk. Inconsistent style with the rest of the engine.
**Fix plan:** Replace both occurrences with `std::numbers::pi_v<float>` and add `#include <numbers>`.

---

## [LOW] AppState used as a communication bus for squad training flags

**ID:** `ARCH-008`
**Location:** `include/neuroflyer/app_state.h:59-63`
**Description:** `AppState` contains transient one-shot flags (`squad_training_mode`, `base_attack_mode`, `squad_paired_fighter_name`, `squad_training_genome_dir`) that are set by one screen, read-and-consumed by `ArenaGameScreen::initialize()` (lines 63-66), and then cleared. This turns `AppState` into a message bus rather than persistent application state.
**Impact:** Fragile coupling. If `initialize()` is called twice, the flags are gone. If a new screen needs these flags, it must read them before ArenaGameScreen does. The pattern scales poorly as more inter-screen communication is needed.
**Fix plan:**
1. Pass these values as constructor arguments to `ArenaGameScreen` (similar to how `ArenaConfig` is already passed).
2. Create an `ArenaLaunchConfig` struct that bundles the arena config + squad training params.
3. Remove the transient flags from `AppState`.

---

## [LOW] Build system monolithic executable

**ID:** `ARCH-009`
**Location:** `CMakeLists.txt:67-125`
**Description:** All engine and UI source files are compiled into a single `add_executable(neuroflyer ...)` target. There is no separate library target for the engine code. This means the architectural rule "engine has zero SDL/ImGui deps" is enforced only by convention, not by the build system.
**Impact:** Nothing prevents an engine file from including ImGui -- it would compile fine. A separate `add_library(neuroflyer_engine STATIC ...)` target that does NOT link SDL2/ImGui would enforce the boundary at build time.
**Fix plan:**
1. Create `add_library(neuroflyer_engine STATIC src/engine/*.cpp)`.
2. Link it only against `neuralnet`, `evolve`, and `nlohmann_json`.
3. Have the main executable link `neuroflyer_engine` + SDL2 + ImGui.
4. Any accidental UI include in engine code will now be a build error.

---

## [LOW] Missing `on_exit()` override in ArenaGameScreen

**ID:** `ARCH-010`
**Location:** `include/neuroflyer/ui/screens/arena_game_screen.h`
**Description:** `ArenaGameScreen` overrides `on_enter()` (to set `initialized_ = false`) but does not override `on_exit()`. The destructor calls `destroy_net_viewer_view()`, but the UIScreen contract expects cleanup in `on_exit()` for resources that should be released when the screen is popped (before destruction, which may happen later if the unique_ptr lingers).
**Impact:** Minor. The destructor handles cleanup, so there is no leak. But if a future change keeps a reference to the screen after popping (e.g., for animation), the net viewer textures would remain allocated until the unique_ptr is destroyed.
**Fix plan:** Move `destroy_net_viewer_view(net_viewer_state_)` from the destructor to an `on_exit()` override.

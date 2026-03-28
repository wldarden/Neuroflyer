# NeuroFlyer main.cpp Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split neuroflyer/src/main.cpp (3,527 lines) into focused screen files, reusable components, and a thin main loop — zero behavior changes.

**Architecture:** Each screen becomes its own .cpp with a draw function that takes AppState& and Renderer&. Shared components (net viewer, test bench, lineage graph, fitness editor) are reusable across screens. main.cpp shrinks to ~150 lines: SDL/ImGui init, asset loading, screen dispatch switch, cleanup.

**Tech Stack:** C++20, SDL2, Dear ImGui, nlohmann/json, GoogleTest

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-24-main-cpp-refactor-design.md`

**CRITICAL:** This is a pure refactor. Every screen must look and behave exactly as before. No new features, no behavior changes, no visual differences. The only change is code organization.

**Rendering model:** Main calls `ImGui::NewFrame()` before dispatch and `ImGui::Render()` + `SDL_RenderPresent()` after. Screens build ImGui UI and make SDL renderer calls (e.g., `renderer.render_net_preview()`, `renderer.render_game_panel()`) within their draw function. Since ImGui uses the SDL renderer backend, all draws go to the same surface in order. SDL content drawn before ImGui windows will appear behind them — this is correct for overlays like the pause screen over the game. Screens that need SDL content behind ImGui (like game panel behind pause overlay) should do their SDL draws before their ImGui calls.

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `include/neuroflyer/app_state.h` | Screen enum, AppState struct, GenStats struct, go_to_screen() |
| `include/neuroflyer/paths.h` | data_dir(), asset_dir() |
| `src/paths.cpp` | Path resolution implementation |
| `include/neuroflyer/screens/main_menu.h` | draw_main_menu() declaration |
| `src/screens/main_menu.cpp` | Main menu screen (Fly/Hangar/Quit) |
| `include/neuroflyer/screens/hangar.h` | draw_hangar() declaration |
| `src/screens/hangar.cpp` | Genome browser + dispatches to sub-views |
| `include/neuroflyer/screens/create_genome.h` | draw_create_genome() declaration |
| `src/screens/create_genome.cpp` | New genome creation dialog |
| `include/neuroflyer/screens/variant_viewer.h` | draw_variant_viewer() declaration |
| `src/screens/variant_viewer.cpp` | Variant list + dispatches to components |
| `include/neuroflyer/screens/fly_session.h` | FlySessionState, draw_fly_session(), get_fly_session_state() |
| `src/screens/fly_session.cpp` | Game loop (frame-based), generation lifecycle |
| `include/neuroflyer/screens/pause_config.h` | draw_pause_config() declaration |
| `src/screens/pause_config.cpp` | Pause/config overlay during flight |
| `include/neuroflyer/components/net_viewer.h` | NetViewerState, draw_net_viewer() |
| `src/components/net_viewer.cpp` | Neural net viewer/editor component |
| `include/neuroflyer/components/test_bench.h` | TestBenchState, draw_test_bench() |
| `src/components/test_bench.cpp` | Sensor testing component |
| `include/neuroflyer/components/lineage_graph.h` | LineageGraphState, draw_lineage_graph() |
| `src/components/lineage_graph.cpp` | Ancestry tree visualization |
| `include/neuroflyer/components/fitness_editor.h` | draw_fitness_editor() |
| `src/components/fitness_editor.cpp` | Scoring parameter editor |

### Modified Files

| File | Changes |
|------|---------|
| `src/main.cpp` | Gutted to ~150 lines: init + dispatch + cleanup |
| `CMakeLists.txt` | Add all new .cpp files |
| `include/neuroflyer/evolution.h` | Add snapshot_to_individual() declaration |
| `src/evolution.cpp` | Add snapshot_to_individual() implementation |

---

## Task Ordering

The refactor proceeds bottom-up: extract leaf dependencies first, then screens that use them, then rewire main.

1. **Scaffolding** — AppState, paths, CMake setup
2. **Components** — net viewer, test bench, lineage graph, fitness editor (no screen dependencies)
3. **Simple screens** — main menu, create genome (no component dependencies)
4. **Complex screens** — hangar, variant viewer (compose components)
5. **Fly session + pause** — the hardest extraction (2200 lines → frame-based)
6. **Final** — gut main.cpp to thin dispatcher

---

### Task 1: AppState, Paths, and CMake Scaffolding

**Files:**
- Create: `include/neuroflyer/app_state.h`
- Create: `include/neuroflyer/paths.h`
- Create: `src/paths.cpp`
- Modify: `CMakeLists.txt`

This creates the foundation all other tasks depend on.

- [ ] **Step 1: Create app_state.h**

Create `neuroflyer/include/neuroflyer/app_state.h`:

```cpp
#pragma once

#include <neuroflyer/config.h>

#include <random>
#include <string>
#include <vector>

namespace neuroflyer {

enum class Screen {
    MainMenu,
    Hangar,
    CreateGenome,
    VariantViewer,
    Flying,
    PauseConfig,
};

struct GenStats {
    std::size_t generation = 0;
    float best = 0.0f;
    float avg = 0.0f;
    float stddev = 0.0f;
};

struct AppState {
    Screen current = Screen::MainMenu;
    Screen previous = Screen::MainMenu;

    // Paths
    std::string data_dir;
    std::string asset_dir;
    std::string settings_path;

    // Config (persisted)
    GameConfig config;

    // RNG
    std::mt19937 rng;

    // Navigation context
    std::string active_genome;
    std::string selected_variant;
    bool return_to_variant_view = false;
    std::string training_parent_name;

    // Pre-built population for fly session (set by variant viewer TrainFresh/TrainFrom)
    std::vector<Individual> pending_population;

    bool quit_requested = false;
};

inline void go_to_screen(AppState& state, Screen screen) {
    state.previous = state.current;
    state.current = screen;
}

} // namespace neuroflyer
```

- [ ] **Step 2: Create paths.h and paths.cpp**

Create `neuroflyer/include/neuroflyer/paths.h`:

```cpp
#pragma once

#include <string>

namespace neuroflyer {

/// Locate the neuroflyer data directory.
[[nodiscard]] std::string data_dir();

/// Locate the neuroflyer assets directory.
[[nodiscard]] std::string asset_dir();

/// Format a unix timestamp as a short date string (e.g. "Mar 24").
[[nodiscard]] std::string format_short_date(int64_t timestamp);

} // namespace neuroflyer
```

Create `neuroflyer/src/paths.cpp` — extract the bodies of `neuroflyer_data_dir()` (lines 33–47), `neuroflyer_asset_dir()` (lines 50–60), and `format_short_date()` (lines 149–159) from main.cpp. Wrap them in `namespace neuroflyer`.

- [ ] **Step 3: Add paths.cpp to CMake**

In `neuroflyer/CMakeLists.txt`, add `src/paths.cpp` to the source list.

- [ ] **Step 4: Build**

Run: `cmake --build build --target neuroflyer --parallel`
Expected: Build succeeds (nothing uses the new files yet)

- [ ] **Step 5: Commit**

```bash
git add neuroflyer/include/neuroflyer/app_state.h \
        neuroflyer/include/neuroflyer/paths.h \
        neuroflyer/src/paths.cpp \
        neuroflyer/CMakeLists.txt
git commit -m "refactor(neuroflyer): add AppState, Screen enum, paths utilities"
```

---

### Task 2: Fitness Editor Component

**Files:**
- Create: `include/neuroflyer/components/fitness_editor.h`
- Create: `src/components/fitness_editor.cpp`
- Modify: `CMakeLists.txt`

The simplest component — extract the fitness function editor (lines 2345–2386 of main.cpp).

- [ ] **Step 1: Create fitness_editor.h**

```cpp
#pragma once

#include <neuroflyer/config.h>

namespace neuroflyer {

/// Draw the fitness/scoring parameter editor panel.
/// Caller handles persistence (saving config to disk).
void draw_fitness_editor(GameConfig& config);

} // namespace neuroflyer
```

- [ ] **Step 2: Create fitness_editor.cpp**

Extract the fitness function UI code from main.cpp lines 2345–2386. This is the ImGui sliders for `pts_per_distance`, `pts_per_tower`, `pts_per_token`, `pts_per_bullet`, and the position scoring multipliers (`x_center_mult`, `x_edge_mult`, `y_bottom_mult`, `y_center_mult`, `y_top_mult`). Wrap in a `draw_fitness_editor(GameConfig& config)` function. Include `<imgui.h>`.

- [ ] **Step 3: Add to CMake, build, commit**

Add `src/components/fitness_editor.cpp` to CMake. Build. Commit: `"refactor(neuroflyer): extract fitness editor component"`

---

### Task 3: Lineage Graph Component

**Files:**
- Create: `include/neuroflyer/components/lineage_graph.h`
- Create: `src/components/lineage_graph.cpp`
- Modify: `CMakeLists.txt`

Extract `LineageGraphState`, `rebuild_lineage_graph()`, `draw_lineage_graph()` from main.cpp lines 652–871.

- [ ] **Step 1: Create lineage_graph.h**

```cpp
#pragma once

#include <string>
#include <vector>

namespace neuroflyer {

struct LineageGraphState {
    struct GraphNode {
        std::string name;
        std::string file;
        int generation = 0;
        bool is_genome = false;
        bool is_mrca_stub = false;
        std::string topology_summary;
        float x = 0.0f, y = 0.0f;
        std::vector<int> children;
    };

    int selected_node = -1;
    bool needs_rebuild = true;
    std::string loaded_dir;
    std::vector<GraphNode> nodes;
    int root = -1;
};

/// Rebuild the lineage graph from a genome directory's lineage.json.
void rebuild_lineage_graph(LineageGraphState& state, const std::string& genome_dir);

/// Draw the lineage graph. Returns the selected variant name if user clicks one,
/// or empty string otherwise.
[[nodiscard]] std::string draw_lineage_graph(LineageGraphState& state,
                                             const std::string& genome_dir,
                                             float graph_w, float graph_h);

} // namespace neuroflyer
```

- [ ] **Step 2: Create lineage_graph.cpp**

Extract the function bodies from main.cpp:
- `rebuild_lineage_graph()` — lines 673–752. Change from using global `g_lineage` to taking `LineageGraphState&` parameter.
- `draw_lineage_graph()` — lines 755–871. Same change: state parameter instead of global.

Include: `<imgui.h>`, `<nlohmann/json.hpp>`, `<neuroflyer/renderer.h>` (for `draw_tiny_text` if needed), `<filesystem>`, `<fstream>`.

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract lineage graph component"`

---

### Task 4: Net Viewer Component

**Files:**
- Create: `include/neuroflyer/components/net_viewer.h`
- Create: `src/components/net_viewer.cpp`
- Modify: `CMakeLists.txt`

Extract the net viewer/editor from main.cpp lines 1725–1808 (the `in_net_viewer` block in the hangar loop).

- [ ] **Step 1: Create net_viewer.h**

```cpp
#pragma once

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/renderer.h>
#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

struct NetViewerState {
    std::vector<Individual> population;
    std::vector<neuralnet::Network> networks;
    std::size_t best_idx = 0;

    // Editor state
    int edit_num_hidden = 0;
    int edit_layer_sizes[8] = {};

    bool wants_close = false;
    bool initialized = false;
};

/// Draw the neural net viewer/editor component.
/// Handles viewing, creating random nets, and saving.
void draw_net_viewer(NetViewerState& state, GameConfig& config,
                     Renderer& renderer, const std::string& data_dir);

} // namespace neuroflyer
```

- [ ] **Step 2: Create net_viewer.cpp**

Extract the net viewer UI code from main.cpp lines 1725–1808. This includes:
- Full-screen ImGui window with "NETWORK VIEWER" title
- Network topology info display
- "Create New Network" editor (hidden layer count, per-layer size sliders, memory slots)
- "Create Random Net" button → `create_population()` → populates state.population/networks
- "Save" button → saves genome or snapshot
- "Back" button → sets `state.wants_close = true`
- After ImGui::Render: `renderer.render_net_panel()` call for the visualization

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract net viewer component"`

---

### Task 5: Test Bench Component

**Files:**
- Create: `include/neuroflyer/components/test_bench.h`
- Create: `src/components/test_bench.cpp`
- Modify: `CMakeLists.txt`

The largest component extraction — ~500 lines from main.cpp lines 1851–2344.

- [ ] **Step 1: Create test_bench.h**

```cpp
#pragma once

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/renderer.h>
#include <neuralnet/network.h>

#include <string>
#include <vector>

namespace neuroflyer {

struct TestBenchState {
    struct TestObject {
        float x, y, radius;
        bool is_tower;
        bool dragging;
    };

    struct ResArc {
        float start_rad, span_rad;
        std::vector<int> active_set;
        bool has_sensor;
    };

    std::vector<TestObject> objects;
    int drag_idx = -1;

    // Manual input controls
    float manual_x = 640.0f, manual_y = 700.0f, manual_speed = 3.0f;
    float res_test_radius = 200.0f;
    bool show_arcs = false;

    // Shared input state
    std::vector<float> input_shared;
    int mem_slots_shared = 0;
    std::vector<ResArc> arcs;

    // Config backup for cancel
    GameConfig config_backup;

    bool wants_save = false;
    bool wants_cancel = false;
    bool initialized = false;
};

/// Draw the sensor test bench component.
void draw_test_bench(TestBenchState& state,
                     std::vector<Individual>& population,
                     std::vector<neuralnet::Network>& networks,
                     std::size_t best_idx,
                     GameConfig& config,
                     Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Create test_bench.cpp**

Extract the test bench code from TWO sections of main.cpp:
1. **ImGui controls** — lines 1851–2344: panel with test object controls, add tower/coin, manual position sliders, occulus config, arc visualization toggles
2. **SDL rendering** — lines 2669–2991: game area rendering with test objects (circles), ship preview, ray/oval visualization, arc markers, input value bars, position labels

Both sections must be combined into a single `draw_test_bench()` function. The ImGui portion draws the control panel, the SDL portion draws the game visualization. They interleave in the same call.

Additional logic:
- Raycast and occulus vision computation
- Network forward pass on each frame
- Input vector display
- Config backup/restore on cancel

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract test bench component"`

---

### Task 6: Main Menu Screen

**Files:**
- Create: `include/neuroflyer/screens/main_menu.h`
- Create: `src/screens/main_menu.cpp`
- Modify: `CMakeLists.txt`

Extract `draw_main_menu()` from lines 66–131 and adapt to use AppState.

- [ ] **Step 1: Create main_menu.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

void draw_main_menu(AppState& state, Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Create main_menu.cpp**

Extract `draw_main_menu()` from main.cpp lines 66–131. Change it from returning `MenuChoice` to directly calling `go_to_screen()`:
- "Fly" → `go_to_screen(state, Screen::Flying)`
- "Hangar" → `go_to_screen(state, Screen::Hangar)`
- "Quit" → `state.quit_requested = true`

Also include the `renderer.render_menu_background()` call that currently lives in the menu loop (around line 1619 in main).

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract main menu screen"`

---

### Task 7: Create Genome Screen

**Files:**
- Create: `include/neuroflyer/screens/create_genome.h`
- Create: `src/screens/create_genome.cpp`
- Modify: `CMakeLists.txt`

Extract `draw_create_genome_screen()` from lines 383–618 and its struct/enum.

- [ ] **Step 1: Create create_genome.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

void draw_create_genome(AppState& state, Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Create create_genome.cpp**

Extract `draw_create_genome_screen()` from main.cpp lines 383–618. Internalize `CreateGenomeAction` enum and `CreateGenomePreviewState` struct as file-static types. Change from returning an action to directly navigating:
- On create: create the genome dir/files, set `state.active_genome`, `go_to_screen(state, Screen::VariantViewer)`
- On cancel: `go_to_screen(state, Screen::Hangar)`

The screen also needs `renderer.render_net_preview()` for the live topology preview.

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract create genome screen"`

---

### Task 8: Hangar Screen

**Files:**
- Create: `include/neuroflyer/screens/hangar.h`
- Create: `src/screens/hangar.cpp`
- Modify: `CMakeLists.txt`

Extract `draw_hangar_screen()` from lines 162–369 PLUS the hangar loop dispatch logic (lines 1636–3037) and adapt to use AppState + compose components.

The hangar is more than just the genome list — it's the hub that dispatches to sub-views (net viewer, test bench, fitness editor) from the hangar context (not just from variant viewer). The current code manages this with boolean flags; the refactored version uses an internal `SubView` enum.

- [ ] **Step 1: Create hangar.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

void draw_hangar(AppState& state, Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Create hangar.cpp**

Extract `draw_hangar_screen()` from main.cpp lines 162–369 plus the hangar action dispatch from lines 2565–2656. Internalize `HangarAction`, `HangarPreviewState`, and add a `SubView` enum:

```cpp
enum class SubView { GenomeList, NetViewer, TestBench, FitnessFunc };
static SubView s_sub_view = SubView::GenomeList;
```

When in `SubView::GenomeList`, draw the genome browser. Handle actions:
- Select genome → set `state.active_genome`, `go_to_screen(state, Screen::VariantViewer)`
- Create genome → `go_to_screen(state, Screen::CreateGenome)`
- View Net → switch to `SubView::NetViewer`, call `draw_net_viewer()` component
- Test Bench → switch to `SubView::TestBench`, call `draw_test_bench()` component
- Fitness function → switch to `SubView::FitnessFunc`, call `draw_fitness_editor()` component
- Back → `go_to_screen(state, Screen::MainMenu)`

Include component headers: `fitness_editor.h`, `net_viewer.h`, `test_bench.h`.

The hangar uses `renderer.render_net_preview()` for the hover preview panel (SDL draws that happen alongside ImGui).

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract hangar screen"`

---

### Task 9: Variant Viewer Screen

**Files:**
- Create: `include/neuroflyer/screens/variant_viewer.h`
- Create: `src/screens/variant_viewer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `include/neuroflyer/evolution.h` (add snapshot_to_individual)
- Modify: `src/evolution.cpp` (add snapshot_to_individual)

The most complex screen — it composes net viewer, test bench, and lineage graph components.

- [ ] **Step 1: Move snapshot_to_individual to evolution**

Add to `evolution.h`:
```cpp
[[nodiscard]] Individual snapshot_to_individual(const Snapshot& snap);
```

Extract the body from main.cpp lines 1229–1250 into `evolution.cpp`.

- [ ] **Step 2: Create variant_viewer.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

void draw_variant_viewer(AppState& state, Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 3: Create variant_viewer.cpp**

Extract `draw_variant_screen()` from main.cpp lines 874–1226 plus the hangar sub-screen dispatch logic for variant-related actions (lines 2387–2550). Internalize `VariantAction` and `VariantScreenState`.

The variant viewer manages sub-views internally:
```cpp
enum class SubView { List, NetViewer, TestBench, Lineage };
static SubView s_sub_view = SubView::List;
```

When the user clicks "View Net", switch to `SubView::NetViewer` and call `draw_net_viewer()`. Same for test bench and lineage. This replaces the boolean flags (`in_net_viewer`, `in_test_bench`, etc.) from the old code.

**State initialization:** The variant viewer's static `VariantScreenState` should detect screen transitions. When `state.active_genome` differs from the previously loaded genome (or on first entry), reload the variant list and reset selection. This replaces the old hangar's manual `variant_state` initialization.

Navigation:
- Train Fresh/From → set `state.selected_variant`, `state.active_genome`, `state.return_to_variant_view = true`, populate `state.pending_population`, `go_to_screen(state, Screen::Flying)`
- Back → `go_to_screen(state, Screen::Hangar)`

Include component headers: `net_viewer.h`, `test_bench.h`, `lineage_graph.h`.

- [ ] **Step 4: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract variant viewer screen with component composition"`

---

### Task 10: Fly Session Screen

**Files:**
- Create: `include/neuroflyer/screens/fly_session.h`
- Create: `src/screens/fly_session.cpp`
- Modify: `CMakeLists.txt`

The hardest extraction — convert the blocking fly mode loop (lines 3053–3744) into a frame-based screen.

- [ ] **Step 1: Create fly_session.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/game.h>
#include <neuroflyer/mrca_tracker.h>
#include <neuroflyer/renderer.h>
#include <neuralnet/network.h>

#include <vector>

namespace neuroflyer {

struct FlySessionState {
    bool initialized = false;

    enum class Phase { Running, Evolving, HeadlessRunning, HeadlessEvolving };
    Phase phase = Phase::Running;

    std::size_t generation = 0;
    int ticks_per_frame = 1;
    int alive_count = 0;
    bool needs_reset = false;  // set by pause screen "Apply" action

    // Population
    std::vector<Individual> population;
    std::vector<GameSession> sessions;
    std::vector<neuralnet::Network> networks;
    std::vector<std::vector<float>> recurrent_states;
    EvolutionConfig evo_config;

    // Tracking
    std::vector<GenStats> gen_history;
    MrcaTracker mrca_tracker;

    // Rendering
    enum class ViewMode { Swarm, Best, Worst };
    ViewMode view = ViewMode::Swarm;

    // Headless
    int headless_remaining = 0;

    // Level seed
    uint32_t level_seed = 0;

    void reset();
};

FlySessionState& get_fly_session_state();

void draw_fly_session(AppState& state, Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Create fly_session.cpp**

This is the core extraction. Convert the blocking loop (lines 3053–3744) into a per-frame function:

**Initialization** (on first call or when `!state.initialized`):
- Load variant from `app_state.selected_variant` → create population
- Build sessions, networks, recurrent states
- Set `phase = Phase::Running`, `initialized = true`

**Phase::Running** (normal frame):
- Handle SDL events (Space → pause, Escape → quit, Tab → cycle view, 1-4 → speed)
- Run `ticks_per_frame` simulation ticks:
  - For each alive individual: raycast/occulus → build input → forward network → decode outputs → update session
  - Count alive
  - If `alive_count <= 1`, transition to `Phase::Evolving`
- Render: game panel + net panel via `renderer`

**Phase::Evolving** (transition frame):
- Compute fitness, gen_history stats
- `evolve_population()`
- MRCA tracking
- Autosave
- Create new sessions/networks for next generation
- `generation++`, transition to `Phase::Running`

**Phase::HeadlessRunning** / **Phase::HeadlessEvolving**:
- Same as Running/Evolving but skip rendering, process many ticks per call
- Show minimal progress display (generation count, best fitness)
- Decrement `headless_remaining` on evolve, revert to Running when 0

**needs_reset flag**: If pause screen set this (Apply action), reinitialize population from current config.

**get_fly_session_state()**: Returns reference to the file-static `FlySessionState`.

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract fly session as frame-based screen"`

**Event handling note:** In the frame-based model, main() polls SDL events and processes `SDL_QUIT`. The fly session uses `SDL_GetKeyboardState()` for input detection (Space, Escape, Tab, 1-4) rather than polling events itself. During headless phases, the fly session processes an entire generation per call but checks `SDL_GetKeyboardState()` periodically so the user can abort with Escape.

---

### Task 11: Pause Config Screen

**Files:**
- Create: `include/neuroflyer/screens/pause_config.h`
- Create: `src/screens/pause_config.cpp`
- Modify: `CMakeLists.txt`

Depends on Task 10 (needs `FlySessionState` from `fly_session.h`).

Extract `draw_imgui_config()` from lines 1260–1507.

- [ ] **Step 1: Create pause_config.h**

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/screens/fly_session.h>

namespace neuroflyer {

void draw_pause_config(AppState& state, Renderer& renderer,
                       FlySessionState& fly_state);

} // namespace neuroflyer
```

- [ ] **Step 2: Create pause_config.cpp**

Extract `draw_imgui_config()` from main.cpp lines 1260–1507. Internalize `PauseResult` enum. Change from returning a result to directly modifying state:
- Resume → `go_to_screen(state, Screen::Flying)`
- Apply → apply config changes, `go_to_screen(state, Screen::Flying)`, set a flag on `fly_state` to indicate reset needed
- Cancel → revert config, `go_to_screen(state, Screen::Flying)`
- Headless → set `fly_state.headless_remaining = N`, `go_to_screen(state, Screen::Flying)`

The pause screen reads `fly_state.population`, `fly_state.gen_history`, `fly_state.generation` for display. It embeds `draw_fitness_editor()` for the scoring section.

"Save Best as Variant" uses `fly_state.population` and saves via `best_as_snapshot()`.

- [ ] **Step 3: Add to CMake, build, commit**

Commit: `"refactor(neuroflyer): extract pause config screen"`

---

### Task 12: Gut main.cpp

**Files:**
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

Replace the entire main.cpp with the thin dispatcher.

- [ ] **Step 1: Rewrite main.cpp**

Replace main.cpp with ~150 lines:

```cpp
#include <neuroflyer/app_state.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/config.h>
#include <neuroflyer/genome_manager.h>

#include <neuroflyer/screens/main_menu.h>
#include <neuroflyer/screens/hangar.h>
#include <neuroflyer/screens/create_genome.h>
#include <neuroflyer/screens/variant_viewer.h>
#include <neuroflyer/screens/fly_session.h>
#include <neuroflyer/screens/pause_config.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <filesystem>
#include <iostream>
#include <random>

int main() {
    constexpr int SCREEN_W = 1280;
    constexpr int SCREEN_H = 800;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    auto* window = SDL_CreateWindow("NeuroFlyer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    neuroflyer::Renderer renderer(sdl_renderer, window, SCREEN_W, SCREEN_H);

    // Load assets
    {
        auto asset_path = neuroflyer::asset_dir();
        renderer.load_asteroid_texture(asset_path + "/asteroid-1.png");
        renderer.load_coin_strip(asset_path + "/coin-strip.png", 16);
        renderer.load_star_atlas(asset_path + "/star-atlas.png", 42);
        renderer.load_menu_background(asset_path + "/pixel-hangar.png");
        for (int i = 0; i < 10; ++i) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s/ships/ship-%02d-strip.png", asset_path.c_str(), i);
            renderer.load_ship_strip(buf, 4);
        }
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 3.0f;
    style.FramePadding = ImVec2(8.0f, 4.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    // App state
    neuroflyer::AppState state;
    state.data_dir = neuroflyer::data_dir();
    state.asset_dir = neuroflyer::asset_dir();
    state.settings_path = state.data_dir + "/settings.json";
    std::filesystem::create_directories(state.data_dir + "/nets");
    std::filesystem::create_directories(state.data_dir + "/genomes");
    state.config = neuroflyer::GameConfig::load(state.settings_path);
    state.rng.seed(std::random_device{}());

    // Recover auto-saves
    neuroflyer::recover_autosaves(state.data_dir + "/genomes");

    // Main loop
    while (!state.quit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) state.quit_requested = true;
        }

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        using S = neuroflyer::Screen;
        switch (state.current) {
        case S::MainMenu:      neuroflyer::draw_main_menu(state, renderer); break;
        case S::Hangar:        neuroflyer::draw_hangar(state, renderer); break;
        case S::CreateGenome:  neuroflyer::draw_create_genome(state, renderer); break;
        case S::VariantViewer: neuroflyer::draw_variant_viewer(state, renderer); break;
        case S::Flying:        neuroflyer::draw_fly_session(state, renderer); break;
        case S::PauseConfig:
            neuroflyer::draw_pause_config(state, renderer,
                                           neuroflyer::get_fly_session_state());
            break;
        }

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
    }

    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 2: Update CMakeLists.txt**

Ensure all new source files are listed. The final source list should include:
```cmake
add_executable(neuroflyer
    src/main.cpp
    src/paths.cpp
    src/game.cpp
    src/evolution.cpp
    src/renderer.cpp
    src/ray.cpp
    src/config.cpp
    src/snapshot_io.cpp
    src/genome_manager.cpp
    src/mrca_tracker.cpp
    src/screens/main_menu.cpp
    src/screens/hangar.cpp
    src/screens/create_genome.cpp
    src/screens/variant_viewer.cpp
    src/screens/fly_session.cpp
    src/screens/pause_config.cpp
    src/components/net_viewer.cpp
    src/components/test_bench.cpp
    src/components/lineage_graph.cpp
    src/components/fitness_editor.cpp
)
```

- [ ] **Step 3: Build**

Run: `cmake --build build --target neuroflyer --parallel`
Expected: Builds with zero warnings

- [ ] **Step 4: Run tests**

Run: `ctest --test-dir build -R neuroflyer --output-on-failure`
Expected: All tests pass (tests don't test UI, so the refactor shouldn't affect them)

- [ ] **Step 5: Manual smoke test**

Run: `./build/neuroflyer/neuroflyer`
1. Main menu appears → click Hangar → genome list loads
2. Create a genome → returns to variant list
3. Back to Hangar → Back to Main Menu
4. Click Fly → game starts, Tab cycles views, Space pauses
5. Pause screen → adjust settings → Resume
6. Escape → returns to menu

- [ ] **Step 6: Commit**

```bash
git commit -m "refactor(neuroflyer): gut main.cpp to thin screen dispatcher (~150 lines)"
```

---

### Task 13: Final Verification & Cleanup

- [ ] **Step 1: Verify main.cpp line count**

```bash
wc -l neuroflyer/src/main.cpp
```
Expected: ~150 lines (should not exceed 200)

- [ ] **Step 2: Verify no behavior changes**

Run through the full app workflow:
1. Main menu → Fly → play a generation → pause → resume → Escape
2. Main menu → Hangar → browse genomes → create genome → view variants
3. Variant viewer → View Net → close → Test Bench → close → Lineage
4. Train from variant → fly → pause → save variant → resume → Escape

- [ ] **Step 3: Delete any dead code**

Check if the old `draw_main_menu()`, `draw_hangar_screen()`, etc. function declarations still exist anywhere. They should all be gone from main.cpp and only exist in their new screen files.

- [ ] **Step 4: Run full test suite**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: Final commit if cleanup needed**

```bash
git commit -m "chore(neuroflyer): main.cpp refactor cleanup and verification"
```

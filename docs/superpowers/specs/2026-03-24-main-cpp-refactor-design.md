# NeuroFlyer main.cpp Refactor — Design Spec

**Date:** 2026-03-24
**Status:** Approved
**Scope:** Split neuroflyer/src/main.cpp (3,527 lines) into focused screen files, reusable components, and a thin main loop

## Goal

Decompose the monolithic main.cpp into organized, composable units. Each screen is its own file. Shared UI components (net viewer, test bench, lineage graph, fitness editor) are reusable across screens and potentially across projects. Fix-it-once: a bug fix in a component fixes it everywhere it's used.

## Architecture

### Screen dispatch model

`main()` runs a single frame loop. Each iteration calls one screen's draw function based on `state.current`. Screens set `state.current` to navigate. No nested `while` loops, no boolean flag state machines.

The fly session is frame-based like all other screens — `draw_fly_session()` advances one frame of simulation + rendering per call. Generation state persists in a `FlySessionState` struct between frames. Pausing is just `state.current = Screen::PauseConfig`.

### Composable components

Components are draw functions that take specific data, not AppState. Screens compose them. A component doesn't know which screen is using it — it just draws given the data it receives. This means the same net viewer can be embedded in the hangar, variant viewer, pause screen, or a completely different project.

## File Structure

### New Files

```
neuroflyer/
├── include/neuroflyer/
│   ├── screens/
│   │   ├── main_menu.h
│   │   ├── hangar.h
│   │   ├── create_genome.h
│   │   ├── variant_viewer.h
│   │   ├── pause_config.h
│   │   └── fly_session.h
│   ├── components/
│   │   ├── net_viewer.h
│   │   ├── test_bench.h
│   │   ├── lineage_graph.h
│   │   └── fitness_editor.h
│   ├── app_state.h
│   └── paths.h
├── src/
│   ├── screens/
│   │   ├── main_menu.cpp
│   │   ├── hangar.cpp
│   │   ├── create_genome.cpp
│   │   ├── variant_viewer.cpp
│   │   ├── pause_config.cpp
│   │   └── fly_session.cpp
│   ├── components/
│   │   ├── net_viewer.cpp
│   │   ├── test_bench.cpp
│   │   ├── lineage_graph.cpp
│   │   └── fitness_editor.cpp
│   └── paths.cpp
```

### Modified Files

| File | Changes |
|------|---------|
| `neuroflyer/src/main.cpp` | Gutted to ~150-200 lines: SDL/ImGui init, asset loading, screen dispatch loop, cleanup |
| `neuroflyer/CMakeLists.txt` | Add all new .cpp files to the executable |

### Unchanged Files

Existing library files remain untouched: `game.cpp`, `evolution.cpp`, `renderer.cpp`, `ray.cpp`, `config.cpp`, `snapshot_io.cpp`, `genome_manager.cpp`, `mrca_tracker.cpp`, and all their headers.

## AppState

Central shared state passed to all screens:

```cpp
// app_state.h

enum class Screen {
    MainMenu,
    Hangar,
    CreateGenome,
    VariantViewer,
    Flying,
    PauseConfig,
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

    // Navigation context
    std::string active_genome;           // genome dir path (set by hangar)
    std::string selected_variant;        // variant filename (set by variant viewer)
    bool return_to_variant_view = false; // after flying, go back to variants
    std::string training_parent_name;    // variant name that seeded the fly session

    // RNG (shared)
    std::mt19937 rng;

    bool quit_requested = false;
};

inline void go_to_screen(AppState& state, Screen screen) {
    state.previous = state.current;
    state.current = screen;
}
```

AppState contains only data needed for cross-screen navigation, shared config, and the RNG. Screen-specific state lives in each screen's own state struct (static or passed in).

### Shared structs

`GenStats` (generation statistics) is used by both fly_session and pause_config. Define it in `app_state.h`:

```cpp
struct GenStats {
    std::size_t generation;
    float best, avg, stddev;
};
```

`snapshot_to_individual()` is a cross-screen utility. Move it to `evolution.h` / `evolution.cpp` since it converts between snapshot and Individual types that already live there.

## Screen Contracts

Each screen is a free function that handles both ImGui UI and SDL rendering in one call. Screens call `ImGui::Render()` and SDL draw calls internally when needed, but do NOT call `SDL_RenderPresent()` — that's main's job.

```cpp
void draw_main_menu(AppState& state, neuroflyer::Renderer& renderer);
void draw_hangar(AppState& state, neuroflyer::Renderer& renderer);
void draw_create_genome(AppState& state, neuroflyer::Renderer& renderer);
void draw_variant_viewer(AppState& state, neuroflyer::Renderer& renderer);
void draw_fly_session(AppState& state, neuroflyer::Renderer& renderer);
void draw_pause_config(AppState& state, neuroflyer::Renderer& renderer,
                       FlySessionState& fly_state);
```

All take `AppState&` and `Renderer&`. Navigate by calling `go_to_screen(state, Screen::X)`. No return values needed — the state mutation IS the navigation.

### Two-phase rendering

Some screens need SDL draw calls after `ImGui::Render()` (e.g., hangar renders net preview, fly session renders the game panel, test bench draws rays/objects). These screens handle it internally:

1. Build ImGui UI
2. Call `ImGui::Render()` + `ImGui_ImplSDLRenderer2_RenderDrawData()`
3. Do SDL draw calls (net preview, game panel, etc.)
4. Return — main calls `SDL_RenderPresent()`

Main's loop becomes:
```cpp
ImGui_ImplSDLRenderer2_NewFrame();
ImGui_ImplSDL2_NewFrame();
ImGui::NewFrame();

switch (state.current) { /* dispatch */ }

// Only call SDL_RenderPresent — screens handle their own ImGui::Render + SDL draws
SDL_RenderPresent(sdl_renderer);
```

Wait — this creates a problem: if screens call `ImGui::Render()` internally, main can't call `ImGui::NewFrame()` + `ImGui::Render()` around them. Instead: main calls `NewFrame()`, screens build ImGui UI only, main calls `Render()`, then screens do their SDL draws via a second pass. This is awkward.

**Simpler approach:** Screens that need post-ImGui SDL rendering do it via the Renderer, which already has methods like `render_game_panel()`, `render_net_preview()`, `render_net_panel()`. These Renderer methods don't use ImGui — they're pure SDL. So the contract is:

1. Main calls `ImGui::NewFrame()`
2. Screen's draw function builds ImGui UI AND queues any SDL rendering it needs (e.g., calling `renderer.render_net_preview()` after `ImGui::Render()`)
3. Main calls `ImGui::Render()` + `ImGui_ImplSDLRenderer2_RenderDrawData()` + `SDL_RenderPresent()`

But Renderer calls like `render_net_preview()` draw immediately to the SDL surface — they can't be "queued." The actual pattern in the current code is: ImGui windows are drawn first, then `ImGui::Render()` composites them, then SDL draws go on top. Since ImGui uses the SDL renderer backend, everything composites in order.

**Final answer:** Screens do everything in their draw function — ImGui calls for UI, direct Renderer calls for SDL content. Main just wraps with NewFrame/Render/Present. The SDL renderer draws in order, so ImGui and direct SDL draws interleave correctly. Screens that need SDL content behind ImGui windows (like the game panel behind a pause overlay) render the SDL content first, then the ImGui overlay.

### Pause screen data access

`draw_pause_config` needs access to the fly session's live data (population, gen_history, generation count) to display stats and save variants. Rather than promote all this into AppState, the pause screen takes a reference to `FlySessionState`:

```cpp
void draw_pause_config(AppState& state, neuroflyer::Renderer& renderer,
                       FlySessionState& fly_state);
```

This means `FlySessionState` must be defined in a header (`fly_session.h`), not hidden as a static in the .cpp. The fly session screen owns the data, but the pause screen can read/write it. Main dispatches:

```cpp
case Screen::PauseConfig:
    draw_pause_config(state, renderer, get_fly_session_state());
    break;
```

Where `get_fly_session_state()` returns a reference to the static `FlySessionState` in fly_session.cpp.

### Screen-specific state

Each screen owns its own persistent state as static variables inside its .cpp file (same pattern as the existing code). For example:

- `hangar.cpp`: `static HangarPreviewState s_preview;`
- `variant_viewer.cpp`: `static VariantScreenState s_variant_state;`
- `fly_session.cpp`: `static FlySessionState s_fly;` (holds population, sessions, networks, generation count, tick count, recurrent states, gen_history, mrca_tracker)

### Screen-specific enums and structs

Each screen's header defines only its public interface — the draw function. Enums like `HangarAction` and structs like `HangarPreviewState` that are internal to a screen live in the .cpp file, not the header. They are implementation details.

Exception: structs/enums that need to be shared between screens go in app_state.h or their own header.

## Component Contracts

Components are draw functions that take specific data, not AppState:

```cpp
// net_viewer.h
struct NetViewerState { /* selection, hover, edit state */ };
void draw_net_viewer(NetViewerState& state,
                     const std::vector<neuroflyer::Individual>& population,
                     const std::vector<neuralnet::Network>& networks,
                     std::size_t best_idx,
                     neuroflyer::Renderer& renderer);

// test_bench.h
struct TestBenchState { /* test objects, drag state, arc state */ };
void draw_test_bench(TestBenchState& state,
                     neuralnet::Network& net,
                     neuroflyer::GameConfig& config,
                     neuroflyer::Renderer& renderer);

// lineage_graph.h
struct LineageGraphState { /* graph nodes, selected, root */ };
void draw_lineage_graph(LineageGraphState& state,
                        const std::string& genome_dir);
// Returns selected variant name if user double-clicks, empty string otherwise.

// fitness_editor.h
void draw_fitness_editor(neuroflyer::GameConfig& config);
```

Components own their state structs. Screens create/hold state instances and call the component draw function. A screen can embed a component inside its own ImGui window, or a component can draw its own full-screen window — that's up to the component's design.

## Fly Session — Frame-Based Design

The current fly mode is a blocking nested loop inside `main()`. After the refactor, `draw_fly_session()` advances one frame per call:

```cpp
struct FlySessionState {
    bool initialized = false;

    enum class Phase { Running, Evolving, HeadlessRunning, HeadlessEvolving };
    Phase phase = Phase::Running;

    std::size_t generation = 0;
    int ticks_per_frame = 1;
    int alive_count = 0;

    // Population
    std::vector<neuroflyer::Individual> population;
    std::vector<neuroflyer::GameSession> sessions;
    std::vector<neuralnet::Network> networks;
    std::vector<std::vector<float>> recurrent_states;

    // Tracking
    std::vector<GenStats> gen_history;
    neuroflyer::MrcaTracker mrca_tracker;

    // View
    enum class ViewMode { Swarm, Best, Worst };
    ViewMode view = ViewMode::Swarm;

    // Headless
    int headless_remaining = 0;  // generations left in headless run

    // Reset
    void reset();  // clears everything for a fresh fly session
};

FlySessionState& get_fly_session_state();  // access the static instance
```

### Generation lifecycle (phase state machine)

`draw_fly_session()` uses a phase to track where it is in the generation lifecycle:

**Phase::Running** — Normal frame: run `ticks_per_frame` simulation ticks, render game panel + net panel. After each tick, check `alive_count`. When `alive_count <= 1`, transition to `Phase::Evolving`.

**Phase::Evolving** — Happens in one frame: compute fitness/stats, `evolve_population()`, MRCA tracking, autosave, create new sessions/networks, increment generation, transition back to `Phase::Running`. This is synchronous — it happens between render frames.

**Phase::HeadlessRunning** — Like Running but skip rendering. Run many ticks per frame (entire generation in one frame if possible). When generation ends, transition to `Phase::HeadlessEvolving`.

**Phase::HeadlessEvolving** — Like Evolving but decrements `headless_remaining`. If `headless_remaining > 0`, transition to `HeadlessRunning`. Otherwise, transition to `Running` (resume visual rendering).

During headless phases, `draw_fly_session()` still polls SDL events (so the user can press Escape to abort) and renders a minimal progress display (generation count, best fitness).

### Initialization

On first call (or when `!initialized`): load the variant from `state.selected_variant`, create population from it, build sessions/networks, set `initialized = true`, `phase = Phase::Running`.

### Navigation

- Escape: `go_to_screen(state, state.return_to_variant_view ? Screen::VariantViewer : Screen::MainMenu)`. Resets fly session state.
- Space: `go_to_screen(state, Screen::PauseConfig)`. Fly state persists.
- Return from PauseConfig: `go_to_screen(state, Screen::Flying)` — resumes with existing state.
- Headless request from PauseConfig: sets `headless_remaining = N`, `phase = HeadlessRunning`.

## Paths

Extract `neuroflyer_data_dir()` and `neuroflyer_asset_dir()` from main.cpp into `paths.h` / `paths.cpp`:

```cpp
// paths.h
namespace neuroflyer {
[[nodiscard]] std::string data_dir();
[[nodiscard]] std::string asset_dir();
}
```

All screens and components use these instead of passing directory strings around. The AppState still stores `data_dir` for convenience, but the functions are the source of truth.

## Navigation Graph

```
MainMenu
├── Fly → Flying
│             ├── Space → PauseConfig → Resume → Flying
│             └── Escape → MainMenu (or VariantViewer if return_to_variant_view)
└── Hangar
    ├── Create Genome → CreateGenome → Back → Hangar
    └── Click genome → VariantViewer
                         ├── View Net → (net_viewer component embedded in VariantViewer)
                         ├── Test Bench → (test_bench component embedded in VariantViewer)
                         ├── Lineage → (lineage_graph component embedded in VariantViewer)
                         ├── Train Fresh/From → Flying (sets selected_variant, return_to_variant_view=true)
                         └── Back → Hangar
```

Note: Net Viewer, Test Bench, Lineage Graph, and Fitness Editor are **components embedded within screens**, not separate screens. The variant viewer manages which component is currently visible using its own internal state (e.g., `enum class SubView { List, NetViewer, TestBench, Lineage }`). The hangar also embeds the fitness editor and can access net viewer / test bench directly (when a genome is selected). This avoids polluting the global Screen enum with sub-views.

## Component Details

### Net Viewer

The net viewer is both a viewer AND an editor. It can:
- Display a network's topology with hover inspection (passive viewing)
- Create random networks with configurable topology
- Save networks as genomes or snapshots
- Edit hidden layer count and memory slots

The component signature accounts for editing:

```cpp
struct NetViewerState {
    std::vector<neuroflyer::Individual> population;
    std::vector<neuralnet::Network> networks;
    std::size_t best_idx = 0;
    bool wants_save = false;
    bool wants_close = false;
    // Editor state
    int edit_num_hidden = 0;
    int edit_layer_sizes[8] = {};
};

void draw_net_viewer(NetViewerState& state, neuroflyer::GameConfig& config,
                     neuroflyer::Renderer& renderer, const std::string& data_dir);
```

### Test Bench

Full interactive sensor testing with ~700 lines of logic. Component owns all test bench state:

```cpp
struct TestBenchState {
    struct TestObject { float x, y, radius; bool is_tower; bool dragging; };
    std::vector<TestObject> objects;
    float manual_x, manual_y, manual_speed;
    // Occulus / ray config
    bool show_arcs = false;
    int drag_idx = -1;
    GameConfig config_backup;  // for cancel/restore
    bool wants_save = false;
    bool wants_cancel = false;
    // ... arc state, resolution analysis state
};

void draw_test_bench(TestBenchState& state, neuralnet::Network& net,
                     neuroflyer::GameConfig& config, neuroflyer::Renderer& renderer);
```

### Fitness Editor

Simpler component — just the scoring parameter sliders:

```cpp
void draw_fitness_editor(neuroflyer::GameConfig& config);
```

The calling screen handles persistence (saving config to disk on exit).

### Lineage Graph

```cpp
struct LineageGraphState {
    struct GraphNode {
        std::string name, file;
        int generation;
        bool is_genome, is_mrca_stub;
        std::string topology_summary;
        float x, y;
        std::vector<int> children;
    };
    std::vector<GraphNode> nodes;
    int root = -1;
    int selected_node = -1;
    bool needs_rebuild = true;
    std::string loaded_dir;
};

void draw_lineage_graph(LineageGraphState& state, const std::string& genome_dir,
                        float width, float height);
```

## main.cpp After Refactor

```cpp
int main() {
    // SDL init, window, renderer (~15 lines)
    // ImGui init (~10 lines)

    neuroflyer::Renderer renderer(sdl_renderer, window, SCREEN_W, SCREEN_H);
    // Load assets (~15 lines)

    neuroflyer::AppState state;
    state.data_dir = neuroflyer::data_dir();
    state.asset_dir = neuroflyer::asset_dir();
    state.settings_path = state.data_dir + "/settings.json";
    state.config = neuroflyer::GameConfig::load(state.settings_path);
    state.rng.seed(std::random_device{}());

    // Recover auto-saves
    neuroflyer::recover_autosaves(state.data_dir + "/genomes");

    while (!state.quit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) state.quit_requested = true;
        }

        // Clear screen
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);

        // Start ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Dispatch to current screen
        // Each screen handles its own ImGui UI + SDL rendering calls.
        // SDL draws and ImGui compose in order on the same SDL surface.
        switch (state.current) {
        case Screen::MainMenu:      draw_main_menu(state, renderer); break;
        case Screen::Hangar:        draw_hangar(state, renderer); break;
        case Screen::CreateGenome:  draw_create_genome(state, renderer); break;
        case Screen::VariantViewer: draw_variant_viewer(state, renderer); break;
        case Screen::Flying:        draw_fly_session(state, renderer); break;
        case Screen::PauseConfig:
            draw_pause_config(state, renderer, get_fly_session_state());
            break;
        }

        // Finalize
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
    }

    // SDL/ImGui cleanup (~10 lines)
}
```

`SCREEN_W` (1280) and `SCREEN_H` (800) are defined as constants in `main.cpp` or in `app_state.h`.

## What This Refactor Does NOT Change

- No new features
- No behavior changes — every screen looks and works exactly as before
- No changes to library code (game.cpp, evolution.cpp, renderer.cpp, etc.)
- No changes to file formats (snapshots, genomes, config)
- Tests should continue to pass unchanged

## Migration Strategy

This is a pure extract-and-move refactor. For each screen/component:
1. Copy the function + its structs/enums out of main.cpp into the new file
2. Add the necessary includes
3. Replace hardcoded paths with `neuroflyer::data_dir()` / `neuroflyer::asset_dir()`
4. Wire it into the screen dispatch in main.cpp
5. Delete the old code from main.cpp

The order matters — do screens that have no component dependencies first (main_menu, create_genome), then components, then screens that use components (variant_viewer, hangar), then fly_session (the hardest one since it's the 2,200-line main() body).

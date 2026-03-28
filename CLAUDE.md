# NeuroFlyer

A neural net playground — vertical-scrolling arcade game where 100 neural nets learn to dodge towers and collect tokens via neuroevolution, with real-time brain visualization and a genome management hangar.

## Architecture

NeuroFlyer is a standalone app in the C++ monorepo. It shares `libs/neuralnet` and `libs/evolve` but has no dependency on EcoSim or AntSim.

The app uses a **4-layer UI architecture** managed by `UIManager`:

1. **Screen** (UIScreen) — major app contexts (MainMenu, Hangar, Game). Managed as a stack (push/pop).
2. **View** (UIView) — panels/regions within a screen with layout bounds.
3. **Modal** (UIModal) — temporary overlays (ConfirmModal, InputModal, NodeEditorModal). Rendered on top of screens.
4. **Widget** — `ui::` namespace free functions for consistent styled controls.

```
neuroflyer/
├── include/neuroflyer/
│   ├── ui/                       — UI framework + all UI headers
│   │   ├── ui_manager.h          — Screen stack, modal stack, resolution, main draw loop
│   │   ├── ui_screen.h           — Base class: on_enter/on_exit/on_resize/on_draw
│   │   ├── ui_view.h             — Base class: bounds-aware panel within a screen
│   │   ├── ui_modal.h            — Base class: overlay with optional input blocking
│   │   ├── ui_widget.h           — ui:: namespace styled controls
│   │   ├── legacy_screen.h       — Bridge for legacy Screen enum (being phased out)
│   │   ├── screens/              — One header per screen (subclass UIScreen)
│   │   │   ├── main_menu_screen.h
│   │   │   ├── hangar_screen.h
│   │   │   ├── create_genome_screen.h
│   │   │   ├── variant_viewer_screen.h
│   │   │   ├── lineage_tree_screen.h
│   │   │   ├── fly_session_screen.h
│   │   │   ├── pause_config_screen.h
│   │   │   └── settings_screen.h
│   │   ├── views/                — Reusable panels (subclass UIView)
│   │   │   ├── topology_preview_view.h
│   │   │   ├── net_viewer_view.h
│   │   │   └── test_bench_view.h
│   │   └── modals/               — Popup dialogs (subclass UIModal)
│   │       ├── confirm_modal.h
│   │       ├── editor_modal.h
│   │       ├── input_modal.h
│   │       ├── layer_editor_modal.h
│   │       └── node_editor_modal.h
│   ├── components/               — Reusable draw functions (callback-based, UI-framework-agnostic)
│   │   ├── net_viewer.h          — Neural net viewer/editor
│   │   ├── test_bench.h          — Sensor testing
│   │   ├── lineage_graph.h       — Ancestry tree visualization
│   │   ├── fitness_editor.h      — Scoring parameter editor
│   │   ├── pause_training.h      — Training tab content
│   │   ├── pause_evolution.h     — Evolution settings tab content
│   │   └── structural_heatmap.h  — Structural analysis heatmap
│   ├── renderers/
│   │   └── variant_net_render.h  — render_variant_net(): ShipDesign → configured net render
│   ├── app_state.h               — AppState, GenStats, dirty flags, go_to_screen() (legacy)
│   ├── config.h                  — GameConfig: scoring, population, ship, world params
│   ├── constants.h               — SCREEN_W, SCREEN_H
│   ├── collision.h               — ray_circle_intersect, point_in_circle (inline)
│   ├── game.h                    — Tower, Token, Bullet, Triangle, GameSession
│   ├── evolution.h               — Individual (StructuredGenome), EvolutionConfig, population ops
│   ├── renderer.h                — SDL2 split-screen renderer + deferred draw queue
│   ├── sensor_engine.h           — Single source of truth: query_sensor, build_ship_input, decode_output
│   ├── ship_design.h             — ShipDesign, SensorDef (uint16_t id), EvolvableFlags, NodeStyle
│   ├── snapshot.h                — Snapshot, SnapshotHeader, GenomeInfo structs
│   ├── snapshot_io.h             — Binary save/load (NFS format v1-v4, CRC32)
│   ├── snapshot_utils.h          — snapshot_to_individual() conversion
│   ├── genome_manager.h          — Genome/variant filesystem + cross-genome lineage
│   ├── mrca_tracker.h            — Elite lineage tracking with pruning + degradation
│   ├── name_validation.h         — Filesystem-safe name validation
│   ├── ray.h                     — ray_range_multiplier (visualization legacy)
│   └── paths.h                   — data_dir(), asset_dir(), format_short_date()
├── src/
│   ├── engine/                   — Pure logic (ZERO SDL/ImGui deps)
│   │   ├── game.cpp              — Game mechanics, collision, spawning, scoring
│   │   ├── evolution.cpp         — StructuredGenome evolution, topology mutations
│   │   ├── config.cpp            — GameConfig JSON save/load
│   │   ├── snapshot_io.cpp       — Binary I/O for snapshots (NFS v1-v4)
│   │   ├── genome_manager.cpp    — Filesystem ops + lineage.json + genomic_lineage.json
│   │   ├── mrca_tracker.cpp      — Elite lineage tracking
│   │   ├── sensor_engine.cpp     — Sensor detection + input encoding + display helpers
│   │   ├── ray.cpp               — Ray-circle intersection (visualization only)
│   │   └── paths.cpp             — File path utilities
│   ├── ui/                       — SDL/ImGui rendering and interaction
│   │   ├── main.cpp              — SDL init, UIManager setup, main loop
│   │   ├── renderer.cpp          — Game panel, fly mode orchestration, deferred draws
│   │   ├── asset_loader.cpp      — Texture loading (ships, asteroids, coins, stars, bg)
│   │   ├── framework/            — UI framework implementations
│   │   │   ├── ui_manager.cpp    — Screen/modal stack management
│   │   │   ├── ui_widget.cpp     — Styled widget implementations
│   │   │   └── modals/           — Generic modal implementations
│   │   │       ├── confirm_modal.cpp
│   │   │       ├── editor_modal.cpp
│   │   │       └── input_modal.cpp
│   │   ├── modals/               — App-specific modal implementations
│   │   │   ├── node_editor_modal.cpp
│   │   │   └── layer_editor_modal.cpp
│   │   ├── screens/              — Screen implementations (mirror include/ui/screens/)
│   │   │   ├── menu/             — Main menu + settings
│   │   │   │   ├── main_menu_screen.cpp
│   │   │   │   └── settings_screen.cpp
│   │   │   ├── hangar/           — Genome/variant management screens
│   │   │   │   ├── hangar_screen.cpp
│   │   │   │   ├── create_genome_screen.cpp
│   │   │   │   ├── variant_viewer_screen.cpp
│   │   │   │   └── lineage_tree_screen.cpp
│   │   │   ├── game/             — Training/flying screens
│   │   │   │   ├── fly_session_screen.cpp
│   │   │   │   └── pause_config_screen.cpp
│   │   │   └── analysis/         — Analysis tools
│   │   │       └── analysis.cpp
│   │   ├── views/                — View implementations (reusable panels)
│   │   │   ├── net_viewer.cpp          — Neural net viewer (legacy component wrapper)
│   │   │   ├── net_viewer_view.cpp     — UIView wrapper for net viewer
│   │   │   ├── test_bench.cpp          — Sensor test bench (legacy component wrapper)
│   │   │   ├── test_bench_view.cpp     — UIView wrapper for test bench
│   │   │   ├── topology_preview_view.cpp — Net topology preview panel
│   │   │   ├── lineage_graph.cpp       — Lineage tree visualization
│   │   │   ├── fitness_editor.cpp      — Scoring parameter sliders
│   │   │   ├── pause_training.cpp      — Training tab content
│   │   │   ├── pause_evolution.cpp     — Evolution settings tab content
│   │   │   └── structural_heatmap.cpp  — Structural analysis heatmap
│   │   └── renderers/            — SDL rendering helpers
│   │       ├── variant_net_render.cpp  — NeuroFlyer-specific net render wrapper
│   │       ├── starfield.cpp           — Parallax star background
│   │       └── occulus.cpp             — Ellipse sensor visualization
├── tests/                        — GoogleTests
├── assets/                       — Sprites (ships, asteroids, coins, stars, hangar bg)
└── docs/
    ├── backlog.md                — Feature backlog (keep updated!)
    ├── feature-audit.md          — System-by-system status audit
    └── superpowers/              — Design specs and implementation plans
```

## Screen Flow

```
MainMenuScreen → push(FlySessionScreen) → push(PauseConfigScreen)
               │                           └── tabs: Training, Evolution, Analysis, Save Variants
               → push(SettingsScreen)
               → push(HangarScreen) → push(CreateGenomeScreen)
                                     → push(VariantViewerScreen)
                                        → embeds: NetViewerView, TestBenchView
                                        → push(VariantNetEditorScreen) (zoomable net editor)
                                        → push(LineageTreeScreen)
                                        → push(FlySessionScreen) (training)
```

**Navigation:** `UIManager::push_screen()` / `pop_screen()` / `replace_screen()`. Screens subclass `UIScreen` and implement `on_draw(AppState&, Renderer&, UIManager&)`. Components take specific data + callbacks (not AppState), so they're reusable.

**Modals:** `UIManager::push_modal()` / `pop_modal()`. Rendered on top of all screens. Used for: confirm dialogs, text input, node editor, layer editor. Escape closes the top modal first.

**Legacy bridge:** `go_to_screen()` and `legacy_screen.h` still exist during migration — UIManager syncs legacy Screen enum changes. Being phased out in favor of direct push/pop.

## Neural Net

- **Inputs:** Configurable via sensor genes (rays or occulus ovals) + position + recurrent memory
- **Hidden:** Evolving topology — layers and nodes mutate
- **Outputs (5):** UP, DOWN, LEFT, RIGHT, SHOOT
- **Genome:** `StructuredGenome` with weight genes + sensor genes + topology genes
- **Recurrent memory:** Hidden layer activations feed back as extra inputs next tick

## Genome Management (Hangar)

Genomes are stored as directories in `data/genomes/{name}/`:
- `genome.bin` — root snapshot (NFS format v2, CRC32)
- `{variant name}.bin` — variant snapshots (same format)
- `lineage.json` — per-genome ancestry tree
- `data/genomes/genomic_lineage.json` — cross-genome lineage (genome-to-genome promotion links)

Each `.bin` file is self-describing: contains ShipDesign (sensors, memory), network topology, weights, parent link. Binary format versioned (v1 = original, v2 = evolvable flags).

The hangar lets users browse genomes, create new ones with custom sensor/topology configs, view/promote/delete variants, launch training runs, and view cross-genome lineage trees. Deleting a genome relinks child genomes to the grandparent.

**Variant saving from training:** The pause screen's "Save Variants" tab saves selected individuals with correct lineage. Elite individuals are saved via `save_elite_variants_with_mrca()` which computes the MRCA tree from the `MrcaTracker`, creates MRCA stub entries in lineage.json, and sets each variant's parent to the correct branch point. Non-elite individuals use `training_parent_name` directly. The `individual_hash()` function (in `evolution.h`) produces unique IDs for MRCA dedup.

## Sensor Engine

`sensor_engine.h` is the **single source of truth** for sensor detection and input encoding:
- `query_sensor(SensorDef, ship_pos, towers, tokens)` — dispatches Raycast (ray-circle intersection) or Occulus (ellipse overlap)
- `build_ship_input(ShipDesign, ...)` — builds complete neural net input vector. Iterates sensors in ShipDesign order (stable IDs).
- `decode_output(output, memory_slots)` — extracts actions + memory from net output
- `compute_sensor_shape(SensorDef, ship_pos)` — derives ellipse geometry (shared by detection AND rendering)
- `build_input_labels/colors/display_order(ShipDesign)` — for net panel display
- `query_sensors_with_endpoints(ShipDesign, ...)` — sensor positions for visualization

**All consumers** (fly mode, test bench, headless) call these functions. No inline detection math anywhere else.

Sensor parameters (angle, range, width) are per-variant via `ShipDesign` stored in each snapshot. They are evolvable via `EvolvableFlags` on the design.

## Scoring (defaults, tunable via pause screen)

| Event | Points |
|-------|--------|
| Distance traveled | +0.1/px |
| Tower destroyed | +50 |
| Token collected | +500 |
| Bullet fired | -30 |
| Hit a tower | DEATH |

Position multipliers scale score by screen position (center/edge, top/bottom).

## Evolution

- **Population:** 100 per generation (tunable)
- **Elitism:** top 10 survive unchanged
- **Selection:** tournament (size 5)
- **Crossover:** uniform, same-topology only
- **Weight mutation:** 10% of weights, Gaussian noise
- **Topology mutations:** add/remove nodes and layers
- **StructuredGenome:** weight genes + sensor genes (evolving ray/oval configurations)

## Controls

| Key | Action |
|-----|--------|
| Tab | Cycle view: SWARM / BEST / WORST |
| Space | Pause + config screen |
| 1-4 | Speed (1x/5x/20x/100x) |
| H | Print scoring rules to console |
| Escape | Quit / Back |
| Hover | Mouse over neural net nodes to inspect weights |

## Building & Running

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/neuroflyer/neuroflyer
```

## Backlog

Feature backlog is tracked in `neuroflyer/docs/backlog.md`. When implementing features from the backlog, update the file to reflect completed items. When new feature ideas come up during development, add them to the backlog.

## UI Framework — Standard Practice

All new UI features MUST use the 4-layer UI framework. Do NOT create standalone draw functions with static locals or use the legacy `go_to_screen()` / `Screen` enum pattern.

### The 4 Layers

**1. UIScreen** (`include/neuroflyer/ui/ui_screen.h`)
- Major app contexts: MainMenu, Hangar, FlySession, etc.
- Subclass `UIScreen`, implement `on_enter()`, `on_exit()`, `on_draw()`, `name()`
- State lives as **member variables** (NOT static locals)
- Navigate via `ui.push_screen()`, `ui.pop_screen()`, `ui.replace_screen()`
- Header: `include/neuroflyer/ui/screens/{name}_screen.h`
- Source: `src/ui/screens/{category}/{name}_screen.cpp`

**2. UIView** (`include/neuroflyer/ui/ui_view.h`)
- Panels/regions within a screen with layout bounds (`set_bounds(x, y, w, h)`)
- Subclass `UIView`, implement `on_draw()`
- Used for reusable sub-panels: topology preview, net viewer, test bench
- Header: `include/neuroflyer/ui/views/{name}_view.h`

**3. UIModal** (`include/neuroflyer/ui/ui_modal.h`)
- Temporary overlays rendered on top of everything
- Subclass `UIModal`, implement `on_draw()`, `name()`, optionally `blocks_input()`
- Used for: confirm dialogs, text input, node editor
- Escape closes the top modal first (before affecting screens)
- Open via `ui.push_modal()`, close via `ui.pop_modal()`
- Header: `include/neuroflyer/ui/modals/{name}_modal.h`

**4. Widgets** (`include/neuroflyer/ui/ui_widget.h`)
- `ui::` namespace free functions for styled controls
- `ui::button()`, `ui::slider_float()`, `ui::input_int()`, `ui::checkbox()`, `ui::section_header()`
- All new UI controls should use these for visual consistency
- Add new widget functions here when a new control pattern is needed

### How to Add a New Feature

**New screen (e.g., "Leaderboard"):**
1. Header: `include/neuroflyer/ui/screens/leaderboard_screen.h` — subclass `UIScreen`
2. Source: `src/ui/screens/{category}/leaderboard_screen.cpp` — implement `on_draw()`
   - Categories: `menu/`, `hangar/`, `game/`, `analysis/` (match the existing grouping)
3. State as member variables on the class, NOT static locals
4. Navigate: `ui.push_screen(std::make_unique<LeaderboardScreen>())`

**New popup (e.g., "Rename Variant"):**
1. Header: `include/neuroflyer/ui/modals/rename_modal.h` — subclass `UIModal`
2. Source: `src/ui/modals/rename_modal.cpp` (app-specific) or `src/ui/framework/modals/rename_modal.cpp` (generic/reusable)
3. Open: `ui.push_modal(std::make_unique<RenameModal>(variant_name, on_confirm_callback))`
4. The modal calls its callback and `ui.pop_modal()` when done

**New panel within an existing screen (e.g., "Sensor Editor"):**
1. Header: `include/neuroflyer/ui/views/sensor_editor_view.h` — subclass `UIView`
2. Source: `src/ui/views/sensor_editor_view.cpp`
3. Screen sets bounds via `view.set_bounds(x, y, w, h)` and calls `view.on_draw()`

**New reusable component (e.g., "Fitness Chart"):**
1. Header: `include/neuroflyer/components/fitness_chart.h` with a state struct + draw function
2. Source: `src/ui/views/fitness_chart.cpp` (component sources live alongside views)
3. Components use **callbacks** (e.g., `std::function<void(...)>`) — NOT UIManager
4. The screen that embeds the component wires callbacks to UIManager actions

### Rules

- **No static locals for UI state.** All state on the class. Static locals cause bugs when re-entering screens.
- **No `go_to_screen()`** for new code. Use `ui.push_screen()` / `ui.pop_screen()`.
- **Components don't know about UIManager.** They take data + callbacks. Screens wire them to navigation.
- **Engine code (`src/engine/`) never imports SDL or ImGui.** UI code (`src/ui/`) wraps engine calls.
- **Use `ui::` widgets** for buttons, sliders, inputs — not raw ImGui calls for styled controls.
- **Modals for popups**, not inline dialog state on screens.

### Existing Implementations (Reference)

| Type | Example | Header | Source |
|------|---------|--------|--------|
| Screen | `HangarScreen` | `ui/screens/hangar_screen.h` | `ui/screens/hangar/hangar_screen.cpp` |
| Screen | `FlySessionScreen` | `ui/screens/fly_session_screen.h` | `ui/screens/game/fly_session_screen.cpp` |
| Screen | `SettingsScreen` | `ui/screens/settings_screen.h` | `ui/screens/menu/settings_screen.cpp` |
| Screen | `VariantNetEditorScreen` | `ui/screens/hangar/variant_net_editor_screen.h` | `ui/screens/hangar/variant_net_editor_screen.cpp` |
| View | `TopologyPreviewView` | `ui/views/topology_preview_view.h` | `ui/views/topology_preview_view.cpp` |
| View | `NetViewerView` | `ui/views/net_viewer_view.h` | `ui/views/net_viewer_view.cpp` |
| View | `TestBenchView` | `ui/views/test_bench_view.h` | `ui/views/test_bench_view.cpp` |
| Modal | `ConfirmModal` | `ui/modals/confirm_modal.h` | `ui/framework/modals/confirm_modal.cpp` |
| Modal | `NodeEditorModal` | `ui/modals/node_editor_modal.h` | `ui/modals/node_editor_modal.cpp` |
| Modal | `LayerEditorModal` | `ui/modals/layer_editor_modal.h` | `ui/modals/layer_editor_modal.cpp` |
| Component | `net_viewer` | `components/net_viewer.h` | `ui/views/net_viewer.cpp` |
| Component | `test_bench` | `components/test_bench.h` | `ui/views/test_bench.cpp` |
| Component | `lineage_graph` | `components/lineage_graph.h` | `ui/views/lineage_graph.cpp` |
| Renderer | `variant_net_render` | `renderers/variant_net_render.h` | `ui/renderers/variant_net_render.cpp` |

## Key Design Decisions

- **4-layer UI architecture** — UIManager with screen stack + modal stack. See "UI Framework" section above.
- **Engine/UI split** — `src/engine/` has zero SDL/ImGui deps, `src/ui/` has all rendering. Clean separation.
- **Composable components** — net viewer, test bench, lineage graph, fitness editor are not screen-specific. They use callbacks (e.g., `on_node_click`) instead of knowing about UIManager.
- **Frame-based fly session** — game loop converted from blocking nested loops to one-frame-per-call, enabling clean pause/resume via screen transitions
- **neuralnet-ui library** — generic neural net rendering lives in `libs/neuralnet-ui/`, NeuroFlyer-specific wrapper in `src/ui/renderers/variant_net_render.cpp`. Two-layer design: `render_neural_net()` (generic) + `render_variant_net()` (NeuroFlyer-specific). The renderer supports `text_scale` for scaled bitmap font at higher zoom levels.
- **Net viewer zoom** — `NetViewerViewState` has `zoom`, `scroll_x`, `zoom_enabled` fields. The variant net editor defaults to 2x zoom with Shift+scroll to adjust. The view renders into a zoom-scaled off-screen SDL canvas and blits a viewport-sized window from it. Mouse coordinates are correctly mapped through the zoom+scroll transform.
- **StructuredGenome** — evolved sensor configurations alongside weights and topology. Per-node activation functions.
- **Recurrent not stacked frames** — net develops its own memory
- **Sensor engine as single source of truth** — one set of functions for detection, input encoding, and output decoding. No inline math in screens. Visual sorting via display permutation (doesn't affect functional data order).
- **ShipDesign per variant, not global** — sensor config lives on each saved variant, not on GameConfig. Editing one variant's sensors doesn't affect others.
- **Shared neuralnet/evolve libs** — same code powers EcoSim, NeuroFlyer, and AntSim

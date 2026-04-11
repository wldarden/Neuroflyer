# GraphNetwork Skirmish Demo

**Date:** 2026-04-10
**Purpose:** Standalone demo comparing GraphNetwork vs MLP forward-pass performance in a team skirmish simulation. No integration into the main app — just an isolated executable that runs the simulation and renders it.

## Goal

Determine whether swapping MLP (`neuralnet::Network`) for GraphNetwork (`neuralnet::GraphNetwork`) in the arena tick pipeline causes meaningful performance degradation. Both net types execute the same `forward(span<const float>) → vector<float>` interface, so the demo isolates the data-structure cost (flat weight matrices vs. sparse adjacency lists) for equivalent dense topologies.

## Reference Nets

Three saved snapshots define the topology templates. The demo loads these to extract layer structure, then creates equivalent dense GraphNetwork genomes with randomized weights.

| Net Type | Snapshot Path | Expected Topology |
|----------|--------------|-------------------|
| Squad Leader | `data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin` | 17 inputs → hidden → 5 outputs |
| NTM | `data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin` | 7 inputs → hidden → 1 output |
| Fighter | `data/genomes/ArenaFighter/Ace Fighter-1.bin` | sensors+6+memory inputs → hidden → 5+memory outputs |

The exact hidden layer sizes and sensor counts come from the snapshots at load time — not hardcoded.

## Architecture

### New Files (no changes to existing code)

```
src/demos/
├── graph_skirmish_demo.cpp    — SDL main loop, arena setup, rendering, timing
├── mlp_to_graph.h             — mlp_snapshot_to_graph_genome(): Snapshot → NeuralGenome
└── graph_arena_tick.h         — tick functions duplicated with GraphNetwork types
```

Plus a new CMake target: `graph_skirmish_demo`.

### mlp_to_graph.h — Topology Conversion

`mlp_snapshot_to_graph_genome(const Snapshot& snap, std::mt19937& rng) → NeuralGenome`

1. Load snapshot's `NetworkTopology`: `input_size`, `layers[]` (each has `output_size`, `activation`, `node_activations`), infer output layer as last in `layers`.
2. Create input nodes: IDs `0..input_size-1`, `role=Input`.
3. For each layer `l` in `topology.layers`:
   - Create `output_size` nodes with sequential IDs, `role=Hidden` (or `Output` for last layer).
   - Set per-node `NeuralNodeProps.activation` from `node_activations[i]` if available, else layer default.
   - Set `bias = 0.0f`, `type = Stateless`.
4. Create dense connections between adjacent node groups (previous layer → current layer):
   - For every `(from_id, to_id)` pair, create a `ConnectionGene` with random weight `uniform(-1.0, 1.0)`, `enabled=true`, sequential `innovation` number.
5. Return assembled `NeuralGenome`.

This produces a graph topologically identical to the MLP — same number of nodes, same connectivity pattern, same activations. Only the weights differ (random, since this is a performance benchmark, not a behavior comparison).

### graph_arena_tick.h — Duplicated Tick Functions

Three functions duplicated from existing engine code, with `neuralnet::Network` swapped to `neuralnet::GraphNetwork`:

1. **`graph_run_ntm_threat_selection()`** — from `squad_leader.cpp:run_ntm_threat_selection()`
   - Takes `GraphNetwork&` instead of `const Network&` (GraphNetwork::forward is non-const)

2. **`graph_run_squad_leader()`** — from `squad_leader.cpp:run_squad_leader()`
   - Same type swap

3. **`graph_tick_team_arena_match()`** — from `team_skirmish.cpp:tick_team_arena_match()`
   - `vector<vector<GraphNetwork>>&` for ntm/leader/fighter nets
   - Calls the graph versions of run_ntm and run_squad_leader
   - Everything else (sensor building, order decoding, arena physics) unchanged

The duplicated functions call the same engine functions for sensors, orders, physics — only the `.forward()` calls change type.

### graph_skirmish_demo.cpp — Main Executable

**Startup:**
1. Load 3 reference snapshots via `load_snapshot()`.
2. Extract `ShipDesign` from fighter snapshot (needed for sensor queries).
3. Convert each snapshot topology → `NeuralGenome` via `mlp_snapshot_to_graph_genome()`.
4. For each team (2 teams), create randomized GraphNetwork instances:
   - 1 NTM net per squad (1 squad per team)
   - 1 squad leader net per squad
   - 8 fighter nets (one per fighter, each with different random weights from the same genome template)
5. Set up `ArenaSession` with default `SkirmishConfig` (4000×4000 world, 2 teams, 1 squad, 8 fighters/squad, 50 towers, 30 tokens, 60s time limit).
6. Initialize SDL window, renderer, camera.

**Main loop:**
1. Call `graph_tick_team_arena_match()` at configured speed (1-4 keys for 1x/5x/20x/100x).
2. Render arena via `ArenaGameView::draw()` (reuse existing view — it takes `ArenaWorld&` and `Camera&`).
3. Display timing overlay via ImGui: current ticks/sec, average ms/tick, generation count.
4. On match end (time limit or one team eliminated): reset arena with new random seed, increment generation counter. Nets stay fixed (no evolution).
5. Escape to quit.

**Timing measurement:**
- Measure wall-clock time per `graph_tick_team_arena_match()` call (excludes rendering).
- Accumulate over 60 ticks, print average ms/tick and ticks/sec to both console and ImGui overlay.
- Also run an MLP baseline at startup: load the same snapshots, build `neuralnet::Network` instances, time 1000 ticks of `tick_team_arena_match()` headlessly. Print comparison.

### Recurrent Memory

The MLP system manages recurrence externally: memory outputs are extracted from the net output and fed back as inputs next tick via `recurrent_states[]`. The GraphNetwork demo uses the same external recurrence pattern — the graph has no internal recurrent connections, matching the MLP exactly. `recurrent_states` vectors are managed identically.

### CMake Target

No `neuroflyer_engine` library exists — the main app compiles engine sources directly. The demo lists the engine sources it needs plus ArenaGameView for rendering.

```cmake
add_executable(graph_skirmish_demo
    src/demos/graph_skirmish_demo.cpp

    # Engine sources needed for arena simulation
    src/engine/arena_world.cpp
    src/engine/arena_session.cpp
    src/engine/arena_tick.cpp       # MLP baseline timing
    src/engine/arena_sensor.cpp
    src/engine/sensor_engine.cpp
    src/engine/squad_leader.cpp
    src/engine/sector_grid.cpp
    src/engine/team_skirmish.cpp    # MLP baseline + ShipAssignment
    src/engine/evolution.cpp        # snapshot_to_individual, Individual
    src/engine/team_evolution.cpp   # TeamIndividual
    src/engine/snapshot_io.cpp
    src/engine/game.cpp             # Triangle, Tower, Token, Bullet
    src/engine/config.cpp
    src/engine/paths.cpp

    # Rendering
    src/ui/views/arena_game_view.cpp
)
target_include_directories(graph_skirmish_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(graph_skirmish_demo PRIVATE
    neuralnet evolve SDL2::SDL2 imgui_lib nlohmann_json::nlohmann_json
)
```

ArenaGameView has no UIManager dependency — it just takes `SDL_Renderer*`, `ArenaSession&`, and `Camera&`.

## What We Learn

- **Ticks/sec with GraphNetwork vs MLP** for equivalent dense topologies. If GraphNetwork is within ~20% of MLP, the robustness benefits justify switching. If it's 2-3x slower, we'd want to optimize the graph forward pass or consider hybrid approaches.
- **Whether the GraphNetwork API integrates cleanly** with the existing arena tick pipeline. If the type swap is truly mechanical, full integration later is straightforward.
- **Baseline for NEAT evolution** — once we add evolution (follow-up), we can compare sparse evolved topologies against the dense starting point.

## Out of Scope

- No evolution (fixed random nets, reset each match)
- No net viewer / brain visualization
- No pause screen or config UI
- No hangar / snapshot saving
- No integration with main app screens
- No behavior comparison (weights are random, not trained)

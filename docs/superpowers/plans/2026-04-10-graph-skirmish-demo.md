# GraphNetwork Skirmish Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Standalone SDL executable that runs a team skirmish simulation using GraphNetwork nets (instead of MLP) and displays tick performance stats.

**Architecture:** Load 3 reference MLP snapshots, convert their topologies to equivalent dense NeuralGenome graphs, build GraphNetworks with random weights, run the arena simulation using duplicated tick functions that accept GraphNetwork, render via ArenaGameView, and display timing comparison against an MLP baseline.

**Tech Stack:** neuralnet (GraphNetwork), evolve (GraphGenome), SDL2, ImGui, existing NeuroFlyer engine (ArenaWorld, sensors, squad_leader)

**Spec:** `docs/superpowers/specs/2026-04-10-graph-skirmish-demo-design.md`

---

### Task 1: CMake target and directory structure

**Files:**
- Create: `src/demos/graph_skirmish_demo.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create directory and skeleton main**

```cpp
// src/demos/graph_skirmish_demo.cpp
#include <iostream>

int main() {
    std::cout << "GraphNetwork Skirmish Demo\n";
    return 0;
}
```

- [ ] **Step 2: Add CMake target**

Add at the end of `CMakeLists.txt`, before the closing (after `add_subdirectory(tests)`):

```cmake
# GraphNetwork performance demo (standalone, no UI framework)
add_executable(graph_skirmish_demo
    src/demos/graph_skirmish_demo.cpp

    # Engine sources needed for arena simulation
    src/engine/arena_world.cpp
    src/engine/arena_session.cpp
    src/engine/arena_tick.cpp
    src/engine/arena_sensor.cpp
    src/engine/sensor_engine.cpp
    src/engine/squad_leader.cpp
    src/engine/sector_grid.cpp
    src/engine/team_skirmish.cpp
    src/engine/evolution.cpp
    src/engine/team_evolution.cpp
    src/engine/snapshot_io.cpp
    src/engine/game.cpp
    src/engine/config.cpp
    src/engine/paths.cpp

    # Rendering (ArenaGameView only — no UIManager)
    src/ui/views/arena_game_view.cpp
)
target_include_directories(graph_skirmish_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(graph_skirmish_demo PRIVATE
    neuralnet evolve SDL2::SDL2 imgui_lib nlohmann_json::nlohmann_json project_warnings
)
```

- [ ] **Step 3: Build and verify**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target graph_skirmish_demo`
Expected: Compiles and links. Running `build/graph_skirmish_demo` prints "GraphNetwork Skirmish Demo".

- [ ] **Step 4: Commit**

```
feat(demo): add graph_skirmish_demo CMake target and skeleton
```

---

### Task 2: MLP-to-GraphNetwork topology conversion

**Files:**
- Create: `src/demos/mlp_to_graph.h`
- Modify: `tests/evolution_test.cpp` (add test)

- [ ] **Step 1: Write the failing test**

Add to `tests/evolution_test.cpp`:

```cpp
#include <neuralnet/graph_network.h>
#include <neuralnet/neural_node_props.h>

TEST(EvolutionTest, MlpToGraphGenomeDenseTopology) {
    // Create a snapshot with known topology: 4 inputs -> 3 hidden -> 2 outputs
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(4, {3}, 2, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};  // empty, not needed for conversion

    auto genome = mlp_snapshot_to_graph_genome(snap, rng);

    // Node counts: 4 input + 3 hidden + 2 output = 9
    ASSERT_EQ(genome.nodes.size(), 9u);

    std::size_t input_count = 0, hidden_count = 0, output_count = 0;
    for (const auto& n : genome.nodes) {
        if (n.role == evolve::NodeRole::Input) ++input_count;
        else if (n.role == evolve::NodeRole::Hidden) ++hidden_count;
        else if (n.role == evolve::NodeRole::Output) ++output_count;
    }
    EXPECT_EQ(input_count, 4u);
    EXPECT_EQ(hidden_count, 3u);
    EXPECT_EQ(output_count, 2u);

    // Connection counts: (4*3) + (3*2) = 12 + 6 = 18 dense connections
    EXPECT_EQ(genome.connections.size(), 18u);
    for (const auto& c : genome.connections) {
        EXPECT_TRUE(c.enabled);
    }

    // All innovation numbers should be unique
    std::set<uint32_t> innovations;
    for (const auto& c : genome.connections) {
        innovations.insert(c.innovation);
    }
    EXPECT_EQ(innovations.size(), genome.connections.size());

    // Should build a valid GraphNetwork
    neuralnet::GraphNetwork net(genome);
    EXPECT_EQ(net.input_size(), 4u);
    EXPECT_EQ(net.output_size(), 2u);

    // Forward pass should work
    std::vector<float> input = {1.0f, -0.5f, 0.2f, 0.8f};
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 2u);
}

TEST(EvolutionTest, MlpToGraphGenomeMultipleHiddenLayers) {
    std::mt19937 rng(42);
    auto ind = nf::Individual::random(7, {4, 8}, 1, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};

    auto genome = mlp_snapshot_to_graph_genome(snap, rng);

    // 7 input + 4 hidden + 8 hidden + 1 output = 20 nodes
    EXPECT_EQ(genome.nodes.size(), 20u);

    // (7*4) + (4*8) + (8*1) = 28 + 32 + 8 = 68 connections
    EXPECT_EQ(genome.connections.size(), 68u);

    neuralnet::GraphNetwork net(genome);
    EXPECT_EQ(net.input_size(), 7u);
    EXPECT_EQ(net.output_size(), 1u);

    std::vector<float> input(7, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target neuroflyer_tests && build/tests/neuroflyer_tests --gtest_filter="EvolutionTest.MlpToGraph*"`
Expected: FAIL — `mlp_snapshot_to_graph_genome` is not defined.

- [ ] **Step 3: Implement mlp_to_graph.h**

```cpp
// src/demos/mlp_to_graph.h
#pragma once

#include <neuroflyer/snapshot.h>

#include <neuralnet/neural_node_props.h>
#include <evolve/graph_gene.h>

#include <random>

/// Convert an MLP snapshot's topology into an equivalent dense NeuralGenome.
/// The resulting graph has the same nodes and dense layer-to-layer connectivity
/// as the MLP, but weights are randomized (this is for performance benchmarking,
/// not behavior cloning).
inline neuralnet::NeuralGenome mlp_snapshot_to_graph_genome(
    const neuroflyer::Snapshot& snap, std::mt19937& rng) {

    neuralnet::NeuralGenome genome;
    std::uniform_real_distribution<float> weight_dist(-1.0f, 1.0f);

    uint32_t node_id = 0;
    uint32_t innovation = 0;

    // Track node ID ranges per "layer" for connection wiring.
    // Layer 0 = inputs, layer 1..N = topology.layers[0..N-1].
    struct LayerRange { uint32_t first; uint32_t count; };
    std::vector<LayerRange> layer_ranges;

    // --- Input nodes ---
    const auto num_inputs = snap.topology.input_size;
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_inputs)});
    for (std::size_t i = 0; i < num_inputs; ++i) {
        genome.nodes.push_back({
            .id = node_id++,
            .role = evolve::NodeRole::Input,
            .props = neuralnet::NeuralNodeProps{}
        });
    }

    // --- Hidden + output layers ---
    const auto& layers = snap.topology.layers;
    for (std::size_t l = 0; l < layers.size(); ++l) {
        const bool is_output = (l == layers.size() - 1);
        const auto& layer = layers[l];

        layer_ranges.push_back({node_id, static_cast<uint32_t>(layer.output_size)});

        for (std::size_t n = 0; n < layer.output_size; ++n) {
            // Per-node activation: use node_activations if available, else layer default
            auto act = layer.activation;
            if (n < layer.node_activations.size()) {
                act = layer.node_activations[n];
            }

            genome.nodes.push_back({
                .id = node_id++,
                .role = is_output ? evolve::NodeRole::Output : evolve::NodeRole::Hidden,
                .props = neuralnet::NeuralNodeProps{
                    .activation = act,
                    .type = neuralnet::NodeType::Stateless,
                    .bias = 0.0f,
                    .tau = 1.0f
                }
            });
        }
    }

    // --- Dense connections between adjacent layers ---
    for (std::size_t l = 0; l + 1 < layer_ranges.size(); ++l) {
        const auto& from_range = layer_ranges[l];
        const auto& to_range = layer_ranges[l + 1];

        for (uint32_t f = 0; f < from_range.count; ++f) {
            for (uint32_t t = 0; t < to_range.count; ++t) {
                genome.connections.push_back({
                    .from_node = from_range.first + f,
                    .to_node = to_range.first + t,
                    .weight = weight_dist(rng),
                    .enabled = true,
                    .innovation = innovation++
                });
            }
        }
    }

    return genome;
}
```

- [ ] **Step 4: Add include in test file**

Add to the includes in `tests/evolution_test.cpp`:

```cpp
#include "../src/demos/mlp_to_graph.h"

#include <set>
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build && build/tests/neuroflyer_tests --gtest_filter="EvolutionTest.MlpToGraph*"`
Expected: Both tests PASS.

- [ ] **Step 6: Commit**

```
feat(demo): add mlp_snapshot_to_graph_genome conversion function
```

---

### Task 3: Duplicated tick functions for GraphNetwork

**Files:**
- Create: `src/demos/graph_arena_tick.h`

These are mechanical type swaps of `run_ntm_threat_selection`, `run_squad_leader`, and `tick_team_arena_match` — changing `const neuralnet::Network&` / `neuralnet::Network` to `neuralnet::GraphNetwork&` / `neuralnet::GraphNetwork`. The implementations are identical except for the net type.

- [ ] **Step 1: Create graph_arena_tick.h with all three functions**

```cpp
// src/demos/graph_arena_tick.h
//
// Duplicates of arena tick functions that accept GraphNetwork instead of Network.
// This avoids modifying the existing engine code for the performance demo.
// If GraphNetwork integration proceeds, these should be replaced by templating
// the originals on network type.
#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_world.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/squad_leader.h>
#include <neuroflyer/team_skirmish.h>

#include <neuralnet/graph_network.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace graph_demo {

using namespace neuroflyer;

// --- NTM with GraphNetwork ---------------------------------------------------

[[nodiscard]] inline NtmResult graph_run_ntm_threat_selection(
    neuralnet::GraphNetwork& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h) {

    NtmResult result;
    if (threats.empty()) return result;

    float best_score = -std::numeric_limits<float>::max();

    for (const auto& threat : threats) {
        auto dr = compute_dir_range(
            squad_center_x, squad_center_y,
            threat.x, threat.y,
            world_w, world_h);

        std::vector<float> ntm_input = {
            dr.dir_sin, dr.dir_cos, dr.range,
            threat.health, squad_alive_fraction,
            threat.is_ship ? 1.0f : 0.0f,
            threat.is_starbase ? 1.0f : 0.0f
        };

        auto output = ntm_net.forward(std::span<const float>(ntm_input));
        float threat_score = output[0];

        if (threat_score > best_score) {
            best_score = threat_score;
            result.active = true;
            result.threat_score = threat_score;
            result.target_x = threat.x;
            result.target_y = threat.y;
            result.heading_sin = dr.dir_sin;
            result.heading_cos = dr.dir_cos;
            result.distance = dr.range;
        }
    }

    return result;
}

// --- Squad leader with GraphNetwork ------------------------------------------

[[nodiscard]] inline SquadLeaderOrder graph_run_squad_leader(
    neuralnet::GraphNetwork& leader_net,
    float squad_health,
    float home_heading_sin, float home_heading_cos, float home_distance,
    float home_health,
    float cmd_heading_sin, float cmd_heading_cos, float cmd_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y,
    float enemy_alive_fraction,
    float time_remaining,
    float squad_center_x_norm,
    float squad_center_y_norm) {

    std::vector<float> input = {
        squad_health,
        home_heading_sin, home_heading_cos, home_distance,
        home_health,
        cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
        ntm.active ? 1.0f : 0.0f,
        ntm.active ? ntm.heading_sin : 0.0f,
        ntm.active ? ntm.heading_cos : 0.0f,
        ntm.active ? ntm.distance : 0.0f,
        ntm.active ? ntm.threat_score : 0.0f,
        enemy_alive_fraction, time_remaining,
        squad_center_x_norm, squad_center_y_norm
    };

    auto output = leader_net.forward(std::span<const float>(input));

    SpacingOrder spacing = (output[0] >= output[1])
        ? SpacingOrder::Expand : SpacingOrder::Contract;

    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    float max_tactical = output[2];
    if (output[3] > max_tactical) { max_tactical = output[3]; tactical = TacticalOrder::AttackShip; }
    if (output[4] > max_tactical) { tactical = TacticalOrder::DefendHome; }

    SquadLeaderOrder order;
    order.tactical = tactical;
    order.spacing = spacing;

    switch (tactical) {
        case TacticalOrder::AttackStarbase:
            order.target_x = enemy_base_x; order.target_y = enemy_base_y; break;
        case TacticalOrder::AttackShip:
            if (ntm.active) { order.target_x = ntm.target_x; order.target_y = ntm.target_y; }
            else { order.target_x = enemy_base_x; order.target_y = enemy_base_y; }
            break;
        case TacticalOrder::DefendHome:
            order.target_x = own_base_x; order.target_y = own_base_y; break;
    }

    return order;
}

// --- Full team arena tick with GraphNetwork -----------------------------------

inline void graph_tick_team_arena_match(
    ArenaSession& arena,
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    const std::vector<ShipAssignment>& assignments,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_ntm_nets,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_leader_nets,
    std::vector<std::vector<neuralnet::GraphNetwork>>& team_fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams) {

    const std::size_t total_ships = arena.ships().size();
    const std::size_t num_teams = team_ntm_nets.size();

    // Build sector grid
    SectorGrid grid(arena_config.world.world_width, arena_config.world.world_height,
                    arena_config.sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena.ships()[i].alive)
            grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
    }
    for (std::size_t b = 0; b < arena.bases().size(); ++b) {
        if (arena.bases()[b].alive())
            grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
    }

    // Per-squad: NTM + squad leader -> orders
    std::vector<std::vector<SquadLeaderOrder>> team_squad_orders(num_teams);
    std::vector<std::vector<float>> squad_center_xs(num_teams);
    std::vector<std::vector<float>> squad_center_ys(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        const int team = static_cast<int>(t);
        const std::size_t num_squads = team_ntm_nets[t].size();
        team_squad_orders[t].resize(num_squads);
        squad_center_xs[t].resize(num_squads, 0.0f);
        squad_center_ys[t].resize(num_squads, 0.0f);

        for (std::size_t sq = 0; sq < num_squads; ++sq) {
            auto stats = arena.compute_squad_stats(team, static_cast<int>(sq));
            squad_center_xs[t][sq] = stats.centroid_x;
            squad_center_ys[t][sq] = stats.centroid_y;

            auto threats = gather_near_threats(
                grid, stats.centroid_x, stats.centroid_y,
                arena_config.ntm_sector_radius, team,
                arena.ships(), ship_teams, arena.bases());

            auto ntm = graph_run_ntm_threat_selection(
                team_ntm_nets[t][sq], stats.centroid_x, stats.centroid_y,
                stats.alive_fraction, threats,
                arena_config.world.world_width, arena_config.world.world_height);

            const float own_base_x = arena.bases()[t].x;
            const float own_base_y = arena.bases()[t].y;
            const float own_base_hp = arena.bases()[t].hp_normalized();
            float enemy_base_x = 0, enemy_base_y = 0;
            float min_dist_sq = std::numeric_limits<float>::max();
            for (const auto& base : arena.bases()) {
                if (base.team_id == team) continue;
                const float dx = stats.centroid_x - base.x;
                const float dy = stats.centroid_y - base.y;
                const float dsq = dx * dx + dy * dy;
                if (dsq < min_dist_sq) { min_dist_sq = dsq; enemy_base_x = base.x; enemy_base_y = base.y; }
            }

            const float world_diag = std::sqrt(
                arena_config.world.world_width * arena_config.world.world_width +
                arena_config.world.world_height * arena_config.world.world_height);
            const float home_dx = own_base_x - stats.centroid_x;
            const float home_dy = own_base_y - stats.centroid_y;
            const float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
            const float home_distance = home_dist_raw / world_diag;
            const float home_heading_sin = (home_dist_raw > 1e-6f) ? home_dx / home_dist_raw : 0.0f;
            const float home_heading_cos = (home_dist_raw > 1e-6f) ? home_dy / home_dist_raw : 0.0f;
            const float cmd_dx = enemy_base_x - stats.centroid_x;
            const float cmd_dy = enemy_base_y - stats.centroid_y;
            const float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
            const float cmd_heading_sin = (cmd_dist_raw > 1e-6f) ? cmd_dx / cmd_dist_raw : 0.0f;
            const float cmd_heading_cos = (cmd_dist_raw > 1e-6f) ? cmd_dy / cmd_dist_raw : 0.0f;
            const float cmd_target_distance = cmd_dist_raw / world_diag;

            float enemy_alive_frac = 0.0f;
            std::size_t enemy_total = 0, enemy_alive = 0;
            for (std::size_t si = 0; si < total_ships; ++si) {
                if (ship_teams[si] != team) { ++enemy_total; if (arena.ships()[si].alive) ++enemy_alive; }
            }
            if (enemy_total > 0) enemy_alive_frac = static_cast<float>(enemy_alive) / static_cast<float>(enemy_total);

            const float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
                static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

            team_squad_orders[t][sq] = graph_run_squad_leader(
                team_leader_nets[t][sq], stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance, own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world.world_width,
                stats.centroid_y / arena_config.world.world_height);
        }
    }

    // Per-ship: run fighter net
    std::vector<std::size_t> global_to_local(256, SIZE_MAX);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const std::size_t global_id = assignments[i].team_id;
        const std::size_t local_id = static_cast<std::size_t>(ship_teams[i]);
        if (global_id < global_to_local.size()) global_to_local[global_id] = local_id;
    }

    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena.ships()[i].alive) continue;

        const auto& assign = assignments[i];
        const std::size_t local_team = (assign.team_id < global_to_local.size())
            ? global_to_local[assign.team_id]
            : static_cast<std::size_t>(ship_teams[i]);
        const int team = ship_teams[i];
        const auto sq = assign.squad_index;
        const auto fi = assign.fighter_index;

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena.ships()[i].x, arena.ships()[i].y,
            arena.ships()[i].rotation,
            team_squad_orders[local_team][sq],
            squad_center_xs[local_team][sq], squad_center_ys[local_team][sq],
            arena_config.world.world_width, arena_config.world.world_height);

        auto ctx = ArenaQueryContext::for_ship(
            arena.ships()[i], i, team,
            arena_config.world.world_width, arena_config.world.world_height,
            arena.towers(), arena.tokens(),
            arena.ships(), ship_teams, arena.bullets());

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states[i]);

        assert(local_team < team_fighter_nets.size());
        assert(fi < team_fighter_nets[local_team].size());

        auto output = team_fighter_nets[local_team][fi].forward(
            std::span<const float>(input));

        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design.memory_slots);

        arena.set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        recurrent_states[i] = decoded.memory;
    }

    arena.tick();
}

} // namespace graph_demo
```

- [ ] **Step 2: Build to verify it compiles**

Run: `cmake --build build --target graph_skirmish_demo`
Expected: Compiles (the header is included by graph_skirmish_demo.cpp later, but we can verify the header parses by adding a temporary include).

- [ ] **Step 3: Commit**

```
feat(demo): add GraphNetwork arena tick functions
```

---

### Task 4: Demo main — load snapshots, MLP baseline, GraphNetwork arena, SDL rendering

**Files:**
- Modify: `src/demos/graph_skirmish_demo.cpp`

- [ ] **Step 1: Implement the complete demo main**

Replace the skeleton `graph_skirmish_demo.cpp` with:

```cpp
// src/demos/graph_skirmish_demo.cpp
//
// Standalone demo: team skirmish with GraphNetwork nets.
// Compares forward-pass performance of GraphNetwork vs MLP for equivalent
// dense topologies. No evolution, no net viewer, no config UI.

#include "mlp_to_graph.h"
#include "graph_arena_tick.h"

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/views/arena_game_view.h>

#include <neuralnet/graph_network.h>
#include <neuralnet/network.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

namespace nf = neuroflyer;

// ── Helpers ──────────────────────────────────────────────────────────────────

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 800;

struct TimingStats {
    double graph_ms_per_tick = 0;
    double mlp_ms_per_tick = 0;
    int generation = 0;
};

/// Run MLP baseline: 1000 headless ticks, return average ms/tick.
static double run_mlp_baseline(
    const nf::Snapshot& fighter_snap,
    const nf::Snapshot& squad_snap,
    const nf::Snapshot& ntm_snap,
    const nf::ShipDesign& fighter_design) {

    nf::SkirmishConfig sk_config;
    nf::ArenaConfig arena_config;
    arena_config.world = sk_config.world;
    arena_config.time_limit_ticks = sk_config.time_limit_ticks;
    arena_config.sector_size = sk_config.sector_size;
    arena_config.ntm_sector_radius = sk_config.ntm_sector_radius;

    const std::size_t num_teams = 2;
    arena_config.world.num_teams = num_teams;
    nf::ArenaSession arena(arena_config, 42);

    auto fighter_ind = nf::snapshot_to_individual(fighter_snap);
    auto squad_ind = nf::snapshot_to_individual(squad_snap);
    auto ntm_ind = nf::snapshot_to_individual(ntm_snap);

    std::vector<std::vector<neuralnet::Network>> team_ntm(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_leader(num_teams);
    std::vector<std::vector<neuralnet::Network>> team_fighter(num_teams);

    const std::size_t fighters_per_team = sk_config.world.fighters_per_squad * sk_config.world.num_squads;
    for (std::size_t t = 0; t < num_teams; ++t) {
        team_ntm[t].push_back(ntm_ind.build_network());
        team_leader[t].push_back(squad_ind.build_network());
        for (std::size_t f = 0; f < fighters_per_team; ++f) {
            team_fighter[t].push_back(fighter_ind.build_network());
        }
    }

    const std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent(total_ships,
        std::vector<float>(fighter_design.memory_slots, 0.0f));
    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) ship_teams[i] = arena.team_of(i);

    // Build assignments (match format: teams 0 and 1)
    std::vector<nf::ShipAssignment> assignments(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const int team = ship_teams[i];
        // Count how many ships before this one on the same team
        std::size_t team_ship_idx = 0;
        for (std::size_t j = 0; j < i; ++j) {
            if (ship_teams[j] == team) ++team_ship_idx;
        }
        assignments[i].team_id = static_cast<std::size_t>(team);
        assignments[i].squad_index = 0;
        assignments[i].fighter_index = team_ship_idx;
    }

    constexpr int BASELINE_TICKS = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int tick = 0; tick < BASELINE_TICKS && !arena.is_over(); ++tick) {
        nf::tick_team_arena_match(arena, arena_config, fighter_design,
            assignments, team_ntm, team_leader, team_fighter,
            recurrent, ship_teams);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return elapsed_ms / BASELINE_TICKS;
}

/// Create a fresh ArenaSession + GraphNetwork nets for a new match.
struct GraphMatch {
    nf::ArenaConfig arena_config;
    std::unique_ptr<nf::ArenaSession> arena;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_ntm;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_leader;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_fighter;
    std::vector<std::vector<float>> recurrent;
    std::vector<int> ship_teams;
    std::vector<nf::ShipAssignment> assignments;
};

static GraphMatch create_graph_match(
    const neuralnet::NeuralGenome& fighter_genome,
    const neuralnet::NeuralGenome& squad_genome,
    const neuralnet::NeuralGenome& ntm_genome,
    const nf::ShipDesign& fighter_design,
    uint32_t seed, std::mt19937& rng) {

    GraphMatch m;
    nf::SkirmishConfig sk_config;
    m.arena_config.world = sk_config.world;
    m.arena_config.time_limit_ticks = sk_config.time_limit_ticks;
    m.arena_config.sector_size = sk_config.sector_size;
    m.arena_config.ntm_sector_radius = sk_config.ntm_sector_radius;

    const std::size_t num_teams = 2;
    m.arena_config.world.num_teams = num_teams;
    m.arena = std::make_unique<nf::ArenaSession>(m.arena_config, seed);

    const std::size_t fighters_per_team =
        sk_config.world.fighters_per_squad * sk_config.world.num_squads;

    m.team_ntm.resize(num_teams);
    m.team_leader.resize(num_teams);
    m.team_fighter.resize(num_teams);

    for (std::size_t t = 0; t < num_teams; ++t) {
        // Each team gets its own randomized copy of each genome template
        auto ntm_copy = ntm_genome;
        for (auto& c : ntm_copy.connections) c.weight = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
        m.team_ntm[t].emplace_back(ntm_copy);

        auto sq_copy = squad_genome;
        for (auto& c : sq_copy.connections) c.weight = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
        m.team_leader[t].emplace_back(sq_copy);

        for (std::size_t f = 0; f < fighters_per_team; ++f) {
            auto f_copy = fighter_genome;
            for (auto& c : f_copy.connections) c.weight = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
            m.team_fighter[t].emplace_back(f_copy);
        }
    }

    const std::size_t total_ships = m.arena->ships().size();
    m.recurrent.assign(total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));
    m.ship_teams.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) m.ship_teams[i] = m.arena->team_of(i);

    m.assignments.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const int team = m.ship_teams[i];
        std::size_t team_ship_idx = 0;
        for (std::size_t j = 0; j < i; ++j) {
            if (m.ship_teams[j] == team) ++team_ship_idx;
        }
        m.assignments[i].team_id = static_cast<std::size_t>(team);
        m.assignments[i].squad_index = 0;
        m.assignments[i].fighter_index = team_ship_idx;
    }

    return m;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    // Load reference snapshots
    std::cout << "Loading reference snapshots...\n";
    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    auto squad_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin");
    auto ntm_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin");

    const auto& fighter_design = fighter_snap.ship_design;

    std::cout << "Fighter topology: " << fighter_snap.topology.input_size << " inputs, "
              << fighter_snap.topology.layers.size() << " layers\n";
    std::cout << "Squad leader topology: " << squad_snap.topology.input_size << " inputs, "
              << squad_snap.topology.layers.size() << " layers\n";
    std::cout << "NTM topology: " << ntm_snap.topology.input_size << " inputs, "
              << ntm_snap.topology.layers.size() << " layers\n";

    // Convert to GraphNetwork genomes (dense, random weights)
    std::mt19937 rng(std::random_device{}());
    auto fighter_genome = mlp_snapshot_to_graph_genome(fighter_snap, rng);
    auto squad_genome = mlp_snapshot_to_graph_genome(squad_snap, rng);
    auto ntm_genome = mlp_snapshot_to_graph_genome(ntm_snap, rng);

    std::cout << "Fighter graph: " << fighter_genome.nodes.size() << " nodes, "
              << fighter_genome.connections.size() << " connections\n";

    // Run MLP baseline
    std::cout << "Running MLP baseline (1000 ticks)...\n";
    double mlp_ms = run_mlp_baseline(fighter_snap, squad_snap, ntm_snap, fighter_design);
    std::cout << "MLP baseline: " << mlp_ms << " ms/tick\n";

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    auto* window = SDL_CreateWindow("GraphNetwork Skirmish Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    // Arena view
    nf::ArenaGameView arena_view(sdl_renderer);
    arena_view.set_bounds(0, 0, WIN_W, WIN_H);

    // Camera
    nf::Camera camera;
    camera.x = 2000.0f;
    camera.y = 2000.0f;
    camera.zoom = static_cast<float>(WIN_W) / 4000.0f;

    // Create first match
    auto match = create_graph_match(fighter_genome, squad_genome, ntm_genome,
                                     fighter_design, 42, rng);

    // Timing
    TimingStats stats;
    stats.mlp_ms_per_tick = mlp_ms;
    double tick_accum_ms = 0;
    int tick_count = 0;
    int speed = 1;

    bool running = true;
    while (running) {
        // Events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_1: speed = 1; break;
                    case SDLK_2: speed = 5; break;
                    case SDLK_3: speed = 20; break;
                    case SDLK_4: speed = 100; break;
                    default: break;
                }
            }
        }

        // Tick
        for (int s = 0; s < speed; ++s) {
            if (match.arena->is_over()) {
                // Reset with new seed
                stats.generation++;
                uint32_t seed = rng();
                match = create_graph_match(fighter_genome, squad_genome, ntm_genome,
                                            fighter_design, seed, rng);
            }

            auto t0 = std::chrono::high_resolution_clock::now();
            graph_demo::graph_tick_team_arena_match(
                *match.arena, match.arena_config, fighter_design,
                match.assignments, match.team_ntm, match.team_leader,
                match.team_fighter, match.recurrent, match.ship_teams);
            auto t1 = std::chrono::high_resolution_clock::now();
            tick_accum_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            tick_count++;

            if (tick_count >= 60) {
                stats.graph_ms_per_tick = tick_accum_ms / tick_count;
                tick_accum_ms = 0;
                tick_count = 0;
            }
        }

        // Render
        SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 20, 255);
        SDL_RenderClear(sdl_renderer);

        arena_view.render(*match.arena, camera, -1, match.ship_teams);

        // ImGui overlay
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("Performance", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

        ImGui::Text("GraphNetwork Skirmish Demo");
        ImGui::Separator();
        ImGui::Text("Speed: %dx  (keys 1-4)", speed);
        ImGui::Text("Generation: %d", stats.generation);
        ImGui::Text("Tick: %u / %u",
            match.arena->current_tick(), match.arena_config.time_limit_ticks);
        ImGui::Separator();
        ImGui::Text("MLP:          %.3f ms/tick  (%.0f ticks/s)",
            stats.mlp_ms_per_tick,
            stats.mlp_ms_per_tick > 0 ? 1000.0 / stats.mlp_ms_per_tick : 0);
        ImGui::Text("GraphNetwork: %.3f ms/tick  (%.0f ticks/s)",
            stats.graph_ms_per_tick,
            stats.graph_ms_per_tick > 0 ? 1000.0 / stats.graph_ms_per_tick : 0);
        if (stats.mlp_ms_per_tick > 0 && stats.graph_ms_per_tick > 0) {
            double ratio = stats.graph_ms_per_tick / stats.mlp_ms_per_tick;
            ImGui::Text("Ratio:        %.2fx %s",
                ratio, ratio > 1.0 ? "(slower)" : "(faster)");
        }
        ImGui::End();

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

- [ ] **Step 2: Build**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target graph_skirmish_demo`
Expected: Compiles and links.

- [ ] **Step 3: Run the demo**

Run: `./build/graph_skirmish_demo`
Expected: SDL window opens showing the arena with colored ships, bullets, bases. ImGui overlay shows MLP vs GraphNetwork timing. Ships move around (random behavior since weights are random). Escape quits.

- [ ] **Step 4: Commit**

```
feat(demo): implement GraphNetwork skirmish demo with SDL rendering and timing
```

---

### Task 5: Run full test suite to verify no regressions

- [ ] **Step 1: Build and run all tests**

Run: `cmake --build build && build/tests/neuroflyer_tests`
Expected: All tests pass (including the two new MlpToGraph tests).

- [ ] **Step 2: Run the demo and verify output**

Run: `./build/graph_skirmish_demo`
Expected: Window shows arena simulation. Performance overlay displays both MLP and GraphNetwork ms/tick numbers. Speed keys 1-4 work. Match resets when time expires. Escape exits cleanly.

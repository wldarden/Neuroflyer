# GraphNetwork Evolution Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add NEAT evolution to the GraphNetwork skirmish demo — fighters evolve via structural mutations while squad brains stay fixed from trained weights.

**Architecture:** Convert 3 trained MLP snapshots to GraphNetwork genomes preserving weights. Seed a fighter population from the converted fighter genome. Each generation: run one arena match (2 teams × 1 squad × 8 fighters), score all 16 fighters, evolve via tournament selection + NEAT mutations. Squad leader and NTM use trained weights unchanged. Save best genomes to dev_ui/data/.

**Tech Stack:** neuralnet (GraphNetwork, serialization), evolve (NEAT operators, InnovationCounter), SDL2, ImGui

**Spec:** `docs/superpowers/specs/2026-04-10-graphnet-evolution-demo-design.md`

---

### Task 1: Weight-preserving MLP→GraphNetwork conversion

**Files:**
- Modify: `src/demos/mlp_to_graph.h`
- Modify: `tests/evolution_test.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/evolution_test.cpp`:

```cpp
TEST(EvolutionTest, MlpToGraphWithWeightsPreservesValues) {
    std::mt19937 rng(42);
    // Create a small net: 3 inputs -> 2 hidden -> 1 output
    auto ind = nf::Individual::random(3, {2}, 1, rng);
    auto net_mlp = ind.build_network();

    // Build a snapshot with the individual's topology and weights
    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};
    // Flatten weights+biases in the same order as build_network()
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (ind.genome.has_gene(lp + "_weights")) {
            const auto& wg = ind.genome.gene(lp + "_weights");
            snap.weights.insert(snap.weights.end(), wg.values.begin(), wg.values.end());
        }
        if (ind.genome.has_gene(lp + "_biases")) {
            const auto& bg = ind.genome.gene(lp + "_biases");
            snap.weights.insert(snap.weights.end(), bg.values.begin(), bg.values.end());
        }
    }

    auto genome = mlp_snapshot_to_graph_genome_with_weights(snap);
    neuralnet::GraphNetwork net_graph(genome);

    // Both nets should produce the same output for the same input
    std::vector<float> input = {0.5f, -0.3f, 0.8f};
    auto out_mlp = net_mlp.forward(input);
    auto out_graph = net_graph.forward(input);

    ASSERT_EQ(out_mlp.size(), out_graph.size());
    for (std::size_t i = 0; i < out_mlp.size(); ++i) {
        EXPECT_NEAR(out_mlp[i], out_graph[i], 1e-5f) << "output " << i;
    }
}

TEST(EvolutionTest, MlpToGraphWithWeightsTargetInputSize) {
    std::mt19937 rng(42);
    // Net with 3 inputs, but we want 5 inputs in the graph (2 extra)
    auto ind = nf::Individual::random(3, {2}, 1, rng);

    nf::Snapshot snap;
    snap.topology = ind.topology;
    snap.ship_design = nf::ShipDesign{};
    for (std::size_t l = 0; l < ind.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (ind.genome.has_gene(lp + "_weights"))
            for (auto v : ind.genome.gene(lp + "_weights").values) snap.weights.push_back(v);
        if (ind.genome.has_gene(lp + "_biases"))
            for (auto v : ind.genome.gene(lp + "_biases").values) snap.weights.push_back(v);
    }

    auto genome = mlp_snapshot_to_graph_genome_with_weights(snap, 5, 1);
    neuralnet::GraphNetwork net(genome);

    EXPECT_EQ(net.input_size(), 5u);
    EXPECT_EQ(net.output_size(), 1u);

    // Should forward without crashing
    std::vector<float> input(5, 0.5f);
    auto output = net.forward(input);
    EXPECT_EQ(output.size(), 1u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build && build/tests/neuroflyer_tests --gtest_filter="EvolutionTest.MlpToGraphWith*"`
Expected: FAIL — `mlp_snapshot_to_graph_genome_with_weights` not defined.

- [ ] **Step 3: Implement the weight-preserving conversion**

Add to `src/demos/mlp_to_graph.h` after the existing `mlp_snapshot_to_graph_genome`:

```cpp
/// Convert an MLP snapshot to a NeuralGenome preserving trained weights.
/// If target_input_size or target_output_size are non-zero and differ from the
/// snapshot's sizes, extra input/output nodes are added with zero-weight
/// connections (for forward compatibility with newer input schemas).
inline neuralnet::NeuralGenome mlp_snapshot_to_graph_genome_with_weights(
    const neuroflyer::Snapshot& snap,
    std::size_t target_input_size = 0,
    std::size_t target_output_size = 0) {

    const auto& topo = snap.topology;
    const std::size_t snap_inputs = topo.input_size;
    const std::size_t num_inputs = (target_input_size > 0) ? target_input_size : snap_inputs;
    const std::size_t snap_outputs = topo.layers.empty() ? 0 : topo.layers.back().output_size;
    const std::size_t num_outputs = (target_output_size > 0) ? target_output_size : snap_outputs;

    neuralnet::NeuralGenome genome;
    uint32_t node_id = 0;
    uint32_t innovation = 0;

    struct LayerRange { uint32_t first; uint32_t count; };
    std::vector<LayerRange> layer_ranges;

    // Input nodes
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_inputs)});
    for (std::size_t i = 0; i < num_inputs; ++i) {
        genome.nodes.push_back({
            .id = node_id++,
            .role = evolve::NodeRole::Input,
            .props = neuralnet::NeuralNodeProps{}
        });
    }

    // Parse the flat weight vector: [L0 weights][L0 biases][L1 weights][L1 biases]...
    std::size_t w_offset = 0;

    for (std::size_t l = 0; l < topo.layers.size(); ++l) {
        const bool is_output = (l == topo.layers.size() - 1);
        const auto& layer = topo.layers[l];
        // For the output layer, use target_output_size if it differs
        const std::size_t layer_out = is_output ? num_outputs : layer.output_size;
        const std::size_t snap_layer_out = layer.output_size;
        // prev_count for weight indexing uses the SNAPSHOT's previous layer size
        const std::size_t snap_prev = (l == 0) ? snap_inputs
            : topo.layers[l - 1].output_size;
        // prev_count for connections uses the graph's previous layer range
        const auto& prev_range = layer_ranges.back();

        layer_ranges.push_back({node_id, static_cast<uint32_t>(layer_out)});

        // Weight matrix offset in snap.weights: snap_prev * snap_layer_out
        const std::size_t weight_block = snap_prev * snap_layer_out;
        const std::size_t bias_offset = w_offset + weight_block;

        // Create nodes with biases and activations from snapshot
        for (std::size_t n = 0; n < layer_out; ++n) {
            auto act = layer.activation;
            if (n < layer.node_activations.size()) {
                act = layer.node_activations[n];
            }
            float bias = 0.0f;
            if (n < snap_layer_out && (bias_offset + n) < snap.weights.size()) {
                bias = snap.weights[bias_offset + n];
            }

            genome.nodes.push_back({
                .id = node_id++,
                .role = is_output ? evolve::NodeRole::Output : evolve::NodeRole::Hidden,
                .props = neuralnet::NeuralNodeProps{
                    .activation = act,
                    .type = neuralnet::NodeType::Stateless,
                    .bias = bias,
                    .tau = 1.0f
                }
            });
        }

        // Create connections with weights from snapshot
        const auto& to_range = layer_ranges.back();
        for (uint32_t out = 0; out < to_range.count; ++out) {
            for (uint32_t in = 0; in < prev_range.count; ++in) {
                float weight = 0.0f;
                // Only copy weight if both indices are within snapshot dimensions
                if (out < snap_layer_out && in < snap_prev) {
                    std::size_t idx = w_offset + out * snap_prev + in;
                    if (idx < snap.weights.size()) {
                        weight = snap.weights[idx];
                    }
                }
                genome.connections.push_back({
                    .from_node = prev_range.first + in,
                    .to_node = to_range.first + out,
                    .weight = weight,
                    .enabled = true,
                    .innovation = innovation++
                });
            }
        }

        // Advance past this layer's weights + biases
        w_offset += weight_block + snap_layer_out;
    }

    return genome;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build && build/tests/neuroflyer_tests --gtest_filter="EvolutionTest.MlpToGraphWith*"`
Expected: PASS — MLP and GraphNetwork produce identical outputs; target_input_size extension works.

- [ ] **Step 5: Commit**

```
feat(demo): add weight-preserving MLP to GraphNetwork conversion
```

---

### Task 2: Evolution infrastructure

**Files:**
- Create: `src/demos/graph_evolution.h`

- [ ] **Step 1: Create the evolution header**

```cpp
// src/demos/graph_evolution.h
//
// Lightweight evolution functions for the GraphNetwork demo.
// Uses NEAT structural mutations with tournament selection — no speciation.
#pragma once

#include <neuralnet/graph_network.h>
#include <neuralnet/neural_node_props.h>
#include <neuralnet/neural_neat_policy.h>
#include <neuralnet/serialization.h>

#include <evolve/graph_gene.h>
#include <evolve/innovation.h>
#include <evolve/neat_operators.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace graph_demo {

// ── Config ──────────────────────────────────────────────────────────────────

struct EvolutionConfig {
    std::size_t elite_count = 3;
    std::size_t tournament_size = 3;

    // NEAT structural mutation rates (per genome per generation)
    float add_connection_rate     = 0.05f;
    float add_node_rate           = 0.03f;
    float disable_connection_rate = 0.01f;

    // Weight mutation
    float weight_mutate_rate      = 0.80f;
    float weight_perturb_strength = 0.3f;

    // Neural property mutation
    float bias_mutate_rate        = 0.40f;
    float bias_perturb_strength   = 0.2f;
    float activation_mutate_rate  = 0.05f;

    int save_interval = 50;  // generations between auto-saves
};

// ── Population ──────────────────────────────────────────────────────────────

struct FighterPopulation {
    std::vector<neuralnet::NeuralGenome> genomes;
    std::vector<float> fitness;

    void resize(std::size_t n, const neuralnet::NeuralGenome& seed) {
        genomes.assign(n, seed);
        fitness.assign(n, 0.0f);
    }

    void clear_fitness() {
        std::fill(fitness.begin(), fitness.end(), 0.0f);
    }
};

// ── Selection ───────────────────────────────────────────────────────────────

inline std::size_t tournament_select(
    const std::vector<float>& fitness,
    std::size_t tournament_size,
    std::mt19937& rng) {

    std::uniform_int_distribution<std::size_t> dist(0, fitness.size() - 1);
    std::size_t best = dist(rng);
    for (std::size_t i = 1; i < tournament_size; ++i) {
        std::size_t candidate = dist(rng);
        if (fitness[candidate] > fitness[best]) best = candidate;
    }
    return best;
}

// ── Mutation ────────────────────────────────────────────────────────────────

inline void mutate_genome(
    neuralnet::NeuralGenome& genome,
    evolve::InnovationCounter& innovation,
    const evolve::NeatPolicy<neuralnet::NeuralNodeProps>& policy,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    // Weight perturbation
    evolve::NeatWeightConfig wc;
    wc.weight_mutate_rate = config.weight_mutate_rate;
    wc.weight_perturb_strength = config.weight_perturb_strength;
    evolve::mutate_weights(genome, wc, rng);

    // Structural mutations
    if (chance(rng) < config.add_connection_rate)
        evolve::add_connection(genome, innovation, rng);
    if (chance(rng) < config.add_node_rate)
        evolve::add_node(genome, innovation, policy, rng);
    if (chance(rng) < config.disable_connection_rate)
        evolve::disable_connection(genome, rng);

    // Neural property mutations
    neuralnet::NeuralMutationConfig nmc;
    nmc.bias_mutate_rate = config.bias_mutate_rate;
    nmc.bias_perturb_strength = config.bias_perturb_strength;
    nmc.activation_mutate_rate = config.activation_mutate_rate;
    neuralnet::mutate_biases(genome, nmc, rng);
    neuralnet::mutate_activations(genome, nmc, rng);
}

// ── Evolution step ──────────────────────────────────────────────────────────

/// Evolve a fighter population in-place. Keeps top elites, fills the rest
/// with mutated copies of tournament winners.
inline void evolve_fighters(
    FighterPopulation& pop,
    evolve::InnovationCounter& innovation,
    const evolve::NeatPolicy<neuralnet::NeuralNodeProps>& policy,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    const std::size_t n = pop.genomes.size();
    if (n == 0) return;

    // Sort indices by fitness (descending)
    std::vector<std::size_t> ranked(n);
    std::iota(ranked.begin(), ranked.end(), 0);
    std::sort(ranked.begin(), ranked.end(), [&](auto a, auto b) {
        return pop.fitness[a] > pop.fitness[b];
    });

    // Build next generation
    std::vector<neuralnet::NeuralGenome> next_gen;
    next_gen.reserve(n);

    // Elites (unchanged)
    std::size_t elites = std::min(config.elite_count, n);
    for (std::size_t i = 0; i < elites; ++i) {
        next_gen.push_back(pop.genomes[ranked[i]]);
    }

    // Fill rest with mutated tournament winners
    innovation.new_generation();
    while (next_gen.size() < n) {
        auto winner = tournament_select(pop.fitness, config.tournament_size, rng);
        auto child = pop.genomes[winner];
        mutate_genome(child, innovation, policy, config, rng);
        next_gen.push_back(std::move(child));
    }

    pop.genomes = std::move(next_gen);
    pop.clear_fitness();
}

// ── Save/Load ───────────────────────────────────────────────────────────────

inline void save_genome(const neuralnet::NeuralGenome& genome,
                        const std::string& path) {
    neuralnet::GraphNetwork net(genome);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open for writing: " << path << "\n";
        return;
    }
    neuralnet::save(net, out);
    std::cout << "Saved: " << path << "\n";
}

inline neuralnet::NeuralGenome load_genome(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open: " + path);
    }
    auto loaded = neuralnet::load(in);
    return std::get<neuralnet::GraphNetwork>(loaded).genome();
}

} // namespace graph_demo
```

- [ ] **Step 2: Build to verify compilation**

Run: `cmake --build build --target graph_skirmish_demo`
(The header is included in the next task, but we can verify it parses by temporarily adding `#include "graph_evolution.h"` to the demo and building.)

- [ ] **Step 3: Commit**

```
feat(demo): add NEAT evolution infrastructure for GraphNetwork demo
```

---

### Task 3: Rewrite demo main for evolution

**Files:**
- Modify: `src/demos/graph_skirmish_demo.cpp`

This is a substantial rewrite. The demo changes from "random nets, timing comparison" to "trained-weight seeds, evolving fighters, generation tracking."

- [ ] **Step 1: Replace the demo with the evolution version**

Replace the entire contents of `src/demos/graph_skirmish_demo.cpp` with:

```cpp
// src/demos/graph_skirmish_demo.cpp
//
// GraphNetwork evolution demo: NEAT structural mutations on fighter nets,
// trained squad brains from MLP seeds, SDL rendering with stats overlay.

#include "mlp_to_graph.h"
#include "graph_arena_tick.h"
#include "graph_evolution.h"

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/views/arena_game_view.h>

#include <neuralnet/graph_network.h>
#include <neuralnet/neural_neat_policy.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace nf = neuroflyer;
namespace fs = std::filesystem;

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 800;

// ── Arena config ────────────────────────────────────────────────────────────
static constexpr std::size_t NUM_TEAMS = 2;
static constexpr std::size_t NUM_SQUADS = 1;
static constexpr std::size_t FIGHTERS_PER_SQUAD = 8;
static constexpr float WORLD_SIZE = 4000.0f;
static constexpr std::size_t TOWER_COUNT = 50;
static constexpr std::size_t TOKEN_COUNT = 30;

// Total fighters = NUM_TEAMS * FIGHTERS_PER_SQUAD = 16 (all in one population)
static constexpr std::size_t POP_SIZE = NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD;

// ── Scoring ─────────────────────────────────────────────────────────────────
static constexpr float KILL_POINTS = 100.0f;
static constexpr float DEATH_PENALTY = 50.0f;
static constexpr float SURVIVAL_BONUS = 20.0f;

// ── Paths ───────────────────────────────────────────────────────────────────
static const std::string SEED_DIR = "dev_ui/data/seeds";
static const std::string EVOLVED_DIR = "dev_ui/data/evolved";

// ── Match setup ─────────────────────────────────────────────────────────────

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

static GraphMatch create_match(
    const graph_demo::FighterPopulation& pop,
    const neuralnet::NeuralGenome& squad_genome,
    const neuralnet::NeuralGenome& ntm_genome,
    const nf::ShipDesign& fighter_design,
    uint32_t seed) {

    GraphMatch m;
    nf::SkirmishConfig sk;
    sk.world.world_width = WORLD_SIZE;
    sk.world.world_height = WORLD_SIZE;
    sk.world.num_teams = NUM_TEAMS;
    sk.world.num_squads = NUM_SQUADS;
    sk.world.fighters_per_squad = FIGHTERS_PER_SQUAD;
    sk.world.tower_count = TOWER_COUNT;
    sk.world.token_count = TOKEN_COUNT;

    m.arena_config.world = sk.world;
    m.arena_config.time_limit_ticks = sk.time_limit_ticks;
    m.arena_config.sector_size = sk.sector_size;
    m.arena_config.ntm_sector_radius = sk.ntm_sector_radius;

    m.arena = std::make_unique<nf::ArenaSession>(m.arena_config, seed);

    m.team_ntm.resize(NUM_TEAMS);
    m.team_leader.resize(NUM_TEAMS);
    m.team_fighter.resize(NUM_TEAMS);

    // Squad brains: same trained weights for both teams (fixed, no evolution)
    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        m.team_ntm[t].emplace_back(ntm_genome);
        m.team_leader[t].emplace_back(squad_genome);
    }

    // Fighters: first 8 genomes → team 0, next 8 → team 1
    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        std::size_t base = t * FIGHTERS_PER_SQUAD;
        for (std::size_t f = 0; f < FIGHTERS_PER_SQUAD; ++f) {
            m.team_fighter[t].emplace_back(pop.genomes[base + f]);
        }
    }

    const std::size_t total_ships = m.arena->ships().size();
    m.recurrent.assign(total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));
    m.ship_teams.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) m.ship_teams[i] = m.arena->team_of(i);

    m.assignments.resize(total_ships);
    std::vector<std::size_t> team_counters(NUM_TEAMS, 0);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto team = static_cast<std::size_t>(m.ship_teams[i]);
        const std::size_t idx = team_counters[team]++;
        m.assignments[i].team_id = team;
        m.assignments[i].squad_index = 0;
        m.assignments[i].fighter_index = idx;
    }

    return m;
}

/// Score all fighters after a match. Maps ship index → population index.
static void score_fighters(
    graph_demo::FighterPopulation& pop,
    const GraphMatch& match) {

    const auto& arena = *match.arena;
    const auto& kills = arena.enemy_kills();
    const std::size_t total_ships = arena.ships().size();

    for (std::size_t i = 0; i < total_ships; ++i) {
        // Map ship index to population index:
        // team 0 ships are pop indices 0..7, team 1 ships are 8..15
        const auto team = static_cast<std::size_t>(match.ship_teams[i]);
        const auto& assign = match.assignments[i];
        std::size_t pop_idx = team * FIGHTERS_PER_SQUAD + assign.fighter_index;
        if (pop_idx >= pop.fitness.size()) continue;

        float score = 0.0f;
        score += KILL_POINTS * static_cast<float>(kills[i]);
        if (arena.ships()[i].alive) {
            score += SURVIVAL_BONUS;
        } else {
            score -= DEATH_PENALTY;
        }
        pop.fitness[pop_idx] += score;
    }
}

// ── Seed management ─────────────────────────────────────────────────────────

static void convert_and_save_seeds(
    const nf::ShipDesign& fighter_design) {

    std::cout << "Converting MLP snapshots to GraphNetwork seeds...\n";

    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    auto squad_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin");
    auto ntm_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin");

    // Fighter: use snapshot's own input/output sizes (they match the ShipDesign)
    auto fighter_genome = mlp_snapshot_to_graph_genome_with_weights(fighter_snap);

    // Squad leader: snapshot has 14 inputs, current code expects 17
    constexpr std::size_t SL_INPUTS = 17;
    constexpr std::size_t SL_OUTPUTS = 5;
    auto squad_genome = mlp_snapshot_to_graph_genome_with_weights(
        squad_snap, SL_INPUTS, SL_OUTPUTS);

    // NTM: 7 inputs matches current code
    auto ntm_genome = mlp_snapshot_to_graph_genome_with_weights(ntm_snap);

    fs::create_directories(SEED_DIR);
    graph_demo::save_genome(fighter_genome, SEED_DIR + "/fighter.nnpk");
    graph_demo::save_genome(squad_genome, SEED_DIR + "/squad_leader.nnpk");
    graph_demo::save_genome(ntm_genome, SEED_DIR + "/ntm.nnpk");

    std::cout << "Seeds saved to " << SEED_DIR << "/\n";
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::unitbuf;

    // Load or convert seeds
    if (!fs::exists(SEED_DIR + "/fighter.nnpk")) {
        // Need fighter_design to check input sizes, load snapshot header
        auto fighter_snap = nf::load_snapshot(
            "data/genomes/ArenaFighter/Ace Fighter-1.bin");
        convert_and_save_seeds(fighter_snap.ship_design);
    }

    std::cout << "Loading seeds...\n";
    auto fighter_seed = graph_demo::load_genome(SEED_DIR + "/fighter.nnpk");
    auto squad_genome = graph_demo::load_genome(SEED_DIR + "/squad_leader.nnpk");
    auto ntm_genome = graph_demo::load_genome(SEED_DIR + "/ntm.nnpk");

    // Get fighter ShipDesign from snapshot (needed for sensor queries)
    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    const auto fighter_design = fighter_snap.ship_design;

    std::cout << "Fighter seed: " << fighter_seed.nodes.size() << " nodes, "
              << fighter_seed.connections.size() << " connections\n";

    // Initialize fighter population (all start as copies of the trained seed)
    graph_demo::FighterPopulation pop;
    pop.resize(POP_SIZE, fighter_seed);

    // Check for evolved genomes to resume from
    int start_gen = 0;
    // Find latest gen_NNNN directory
    if (fs::exists(EVOLVED_DIR)) {
        int latest = -1;
        for (const auto& entry : fs::directory_iterator(EVOLVED_DIR)) {
            if (!entry.is_directory()) continue;
            auto name = entry.path().filename().string();
            if (name.substr(0, 4) == "gen_") {
                int gen = std::stoi(name.substr(4));
                if (gen > latest) latest = gen;
            }
        }
        if (latest >= 0) {
            std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(latest);
            if (fs::exists(gen_dir + "/fighter.nnpk")) {
                std::cout << "Resuming from generation " << latest << "\n";
                auto evolved_fighter = graph_demo::load_genome(gen_dir + "/fighter.nnpk");
                pop.resize(POP_SIZE, evolved_fighter);
                start_gen = latest;
            }
        }
    }

    // Evolution state
    std::mt19937 rng(std::random_device{}());
    evolve::InnovationCounter innovation;
    // Seed innovation counter with the initial connection count
    for (const auto& c : fighter_seed.connections) {
        innovation.get_or_create(c.from_node, c.to_node);
    }
    auto policy = neuralnet::make_neural_neat_policy(neuralnet::NeuralMutationConfig{});
    graph_demo::EvolutionConfig evo_config;

    // Apply initial mutations so not all fighters are identical
    if (start_gen == 0) {
        for (std::size_t i = evo_config.elite_count; i < POP_SIZE; ++i) {
            graph_demo::mutate_genome(pop.genomes[i], innovation, policy, evo_config, rng);
        }
    }

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    auto* window = SDL_CreateWindow("GraphNetwork Evolution",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    nf::ArenaGameView arena_view(sdl_renderer);
    arena_view.set_bounds(0, 0, WIN_W, WIN_H);

    nf::Camera camera;
    camera.x = WORLD_SIZE / 2.0f;
    camera.y = WORLD_SIZE / 2.0f;
    camera.zoom = static_cast<float>(WIN_W) / WORLD_SIZE;

    // Create first match
    int generation = start_gen;
    pop.clear_fitness();
    auto match = create_match(pop, squad_genome, ntm_genome, fighter_design, rng());

    // Stats
    float best_fitness = 0;
    float avg_fitness = 0;
    int speed = 1;

    bool running = true;
    while (running) {
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
                    case SDLK_s: {
                        // Manual save
                        fs::create_directories(EVOLVED_DIR + "/gen_" + std::to_string(generation));
                        auto ranked = pop.fitness;
                        auto best_idx = std::distance(ranked.begin(),
                            std::max_element(ranked.begin(), ranked.end()));
                        graph_demo::save_genome(pop.genomes[best_idx],
                            EVOLVED_DIR + "/gen_" + std::to_string(generation) + "/fighter.nnpk");
                        break;
                    }
                    default: break;
                }
            }
        }

        // Tick simulation
        for (int s = 0; s < speed; ++s) {
            if (match.arena->is_over()) {
                // Score fighters from this match
                score_fighters(pop, match);

                // Compute stats
                best_fitness = *std::max_element(pop.fitness.begin(), pop.fitness.end());
                float total = 0;
                for (auto f : pop.fitness) total += f;
                avg_fitness = total / static_cast<float>(pop.fitness.size());

                // Auto-save
                if (generation > 0 && generation % evo_config.save_interval == 0) {
                    std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(generation);
                    fs::create_directories(gen_dir);
                    auto best_idx = std::distance(pop.fitness.begin(),
                        std::max_element(pop.fitness.begin(), pop.fitness.end()));
                    graph_demo::save_genome(pop.genomes[best_idx],
                        gen_dir + "/fighter.nnpk");
                    graph_demo::save_genome(squad_genome, gen_dir + "/squad_leader.nnpk");
                    graph_demo::save_genome(ntm_genome, gen_dir + "/ntm.nnpk");
                }

                // Evolve
                graph_demo::evolve_fighters(pop, innovation, policy, evo_config, rng);
                generation++;

                // Start next match with evolved population
                match = create_match(pop, squad_genome, ntm_genome, fighter_design, rng());
            }

            graph_demo::graph_tick_team_arena_match(
                *match.arena, match.arena_config, fighter_design,
                match.assignments, match.team_ntm, match.team_leader,
                match.team_fighter, match.recurrent, match.ship_teams);
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
        ImGui::Begin("Evolution", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

        ImGui::Text("GraphNetwork Evolution");
        ImGui::Text("%zu teams x %zu fighters, pop %zu",
            NUM_TEAMS, FIGHTERS_PER_SQUAD, POP_SIZE);
        ImGui::Separator();
        ImGui::Text("Speed: %dx  (1-4)", speed);
        ImGui::Text("Generation: %d", generation);
        ImGui::Text("Tick: %u / %u",
            match.arena->current_tick(), match.arena_config.time_limit_ticks);
        ImGui::Separator();
        ImGui::Text("Best fitness: %.1f", best_fitness);
        ImGui::Text("Avg fitness:  %.1f", avg_fitness);
        ImGui::Text("Nodes (best): %zu", pop.genomes[0].nodes.size());
        ImGui::Text("Conns (best): %zu", pop.genomes[0].connections.size());
        ImGui::Separator();
        ImGui::Text("S: save  |  Esc: quit");
        ImGui::Text("Auto-save every %d gens", evo_config.save_interval);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
    }

    // Final save
    {
        std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(generation);
        fs::create_directories(gen_dir);
        auto best_idx = std::distance(pop.fitness.begin(),
            std::max_element(pop.fitness.begin(), pop.fitness.end()));
        graph_demo::save_genome(pop.genomes[best_idx], gen_dir + "/fighter.nnpk");
        graph_demo::save_genome(squad_genome, gen_dir + "/squad_leader.nnpk");
        graph_demo::save_genome(ntm_genome, gen_dir + "/ntm.nnpk");
        std::cout << "Final save at generation " << generation << "\n";
    }

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
Expected: First run converts MLP seeds (creates `dev_ui/data/seeds/*.nnpk`). SDL window shows arena with 16 fighters (2 teams of 8). ImGui overlay shows generation counter, fitness stats. When a match ends, population evolves and new match starts. Fighters should initially behave like the trained net (not random). Speed keys 1-4 work. S saves. Escape exits and auto-saves.

- [ ] **Step 4: Commit**

```
feat(demo): GraphNetwork evolution demo with NEAT mutations and trained seeds
```

---

### Task 4: Verify full test suite

- [ ] **Step 1: Build and run all tests**

Run: `cmake --build build && build/tests/neuroflyer_tests`
Expected: All tests pass (including the two new weight-preservation tests from Task 1).

- [ ] **Step 2: Verify seed files were created**

Run: `ls -la dev_ui/data/seeds/`
Expected: `fighter.nnpk`, `squad_leader.nnpk`, `ntm.nnpk` files exist.

- [ ] **Step 3: Verify evolution runs for several generations**

Run the demo at 4x speed for 30+ seconds, then check:
- Generation counter advances
- Fitness values change over generations
- `dev_ui/data/evolved/gen_50/` directory created after 50 generations

- [ ] **Step 4: Verify resume from saved genomes**

Kill the demo, restart it. It should print "Resuming from generation N" and continue from the saved state.

- [ ] **Step 5: Commit**

```
test: verify GraphNetwork evolution demo end-to-end
```

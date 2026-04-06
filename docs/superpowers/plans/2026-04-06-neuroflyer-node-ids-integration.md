# NeuroFlyer Node IDs Integration (Phase 2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate the neuralnet library's node ID system into NeuroFlyer — populate IDs on all topologies, serialize them in snapshots (v8), replace manual weight remapping with adapt_topology_inputs(), and show UI modals when nets need adaptation.

**Architecture:** Add ID helper functions that wrap existing label generators, set IDs at topology creation points (Individual::from_design, TeamIndividual::create), bump snapshot format to v8, replace convert_variant_to_fighter() internals with adapt_topology_inputs(), and add ConfirmModal integration in variant viewer action handlers.

**Tech Stack:** C++20, neuralnet library (adapt.h), Google Test

**Spec:** `docs/superpowers/specs/2026-04-06-neuroflyer-node-ids-integration-design.md`

---

## File Structure

| Component | File | Change |
|-----------|------|--------|
| Output ID helpers | `include/neuroflyer/sensor_engine.h` + `src/engine/sensor_engine.cpp` | Add `build_solo_output_ids()`, `build_ntm_input_ids()`, `build_ntm_output_ids()` |
| Fighter output IDs | `include/neuroflyer/arena_sensor.h` + `src/engine/arena_sensor.cpp` | Add `build_fighter_output_ids()` |
| Topology ID population | `src/engine/evolution.cpp` | Set IDs in `Individual::from_design()` |
| Team topology IDs | `src/engine/team_evolution.cpp` | Set IDs in `TeamIndividual::create()` |
| Snapshot v8 | `src/engine/snapshot_io.cpp` | Write/read input_ids + output_ids |
| Adaptation helper | `include/neuroflyer/snapshot_utils.h` + `src/engine/evolution.cpp` | Add `adapt_individual_inputs()` + `AdaptReport` |
| convert_variant_to_fighter | `src/engine/evolution.cpp` | Replace manual remap with adapt_topology_inputs() |
| UI modal | `src/ui/screens/hangar/variant_viewer_screen.cpp` | Show ConfirmModal on mismatch |
| Tests | `tests/snapshot_io_test.cpp`, `tests/evolution_test.cpp` | v8 round-trip, ID population, adaptation |

## Key Existing Code

| Function | File | Purpose |
|----------|------|---------|
| `build_input_labels(design)` | `sensor_engine.h/cpp` | Solo input labels — reuse as input IDs |
| `build_arena_fighter_input_labels(design)` | `arena_sensor.h/cpp` | Fighter input labels — reuse as input IDs |
| `build_squad_leader_input_labels()` | `sensor_engine.h/cpp` | Squad leader 14 input labels |
| `build_squad_leader_output_labels()` | `sensor_engine.h/cpp` | Squad leader 5 output labels |
| `write_string()` / `read_string()` | `snapshot_io.cpp` | String serialization (uint16 len + bytes) |
| `neuralnet::adapt_topology_inputs()` | `neuralnet/adapt.h` | Weight matrix adaptation by ID |

---

### Task 1: Add ID Helper Functions

**Files:**
- Modify: `include/neuroflyer/sensor_engine.h`
- Modify: `src/engine/sensor_engine.cpp`
- Modify: `include/neuroflyer/arena_sensor.h`
- Modify: `src/engine/arena_sensor.cpp`

Add these new functions:

**In `sensor_engine.h` (declarations, after `build_squad_leader_output_labels`):**
```cpp
/// Output node IDs for solo/fighter nets: UP, DN, LF, RT, SH + memory slots.
[[nodiscard]] std::vector<std::string> build_output_ids(const ShipDesign& design);

/// NTM sub-net input node IDs (7 fixed threat features).
[[nodiscard]] std::vector<std::string> build_ntm_input_ids();

/// NTM sub-net output node IDs (1: threat score).
[[nodiscard]] std::vector<std::string> build_ntm_output_ids();
```

**In `sensor_engine.cpp` (implementations):**
```cpp
std::vector<std::string> build_output_ids(const ShipDesign& design) {
    std::vector<std::string> ids = {"UP", "DN", "LF", "RT", "SH"};
    for (std::size_t i = 0; i < design.memory_slots; ++i) {
        ids.push_back("M" + std::to_string(i));
    }
    return ids;
}

std::vector<std::string> build_ntm_input_ids() {
    return {"H Sin", "H Cos", "Dist", "HP", "Alive%", "IsShip", "IsBase"};
}

std::vector<std::string> build_ntm_output_ids() {
    return {"Threat"};
}
```

No new function needed for fighter output IDs — `build_output_ids(design)` works for both solo and fighter (same outputs: 5 actions + memory).

- [ ] **Step 1:** Add declarations to `sensor_engine.h`
- [ ] **Step 2:** Add implementations to `sensor_engine.cpp`
- [ ] **Step 3:** Build: `cmake --build build --target neuroflyer_tests`
- [ ] **Step 4:** Commit

---

### Task 2: Populate IDs in Individual::from_design() and TeamIndividual::create()

**Files:**
- Modify: `src/engine/evolution.cpp` (Individual::from_design)
- Modify: `src/engine/team_evolution.cpp` (TeamIndividual::create)

**In `evolution.cpp`, `Individual::from_design()` — after setting `ind.topology.input_size` and before `ind.genome = build_genome_skeleton(...)`, add:**
```cpp
ind.topology.input_ids = build_input_labels(design);
ind.topology.output_ids = build_output_ids(design);
```

This requires adding `#include <neuroflyer/sensor_engine.h>` at the top of evolution.cpp (if not already present).

**In `team_evolution.cpp`, `TeamIndividual::create()` — after each sub-individual is created, set its IDs:**

After NTM creation (line ~21):
```cpp
team.ntm_individual.topology.input_ids = build_ntm_input_ids();
team.ntm_individual.topology.output_ids = build_ntm_output_ids();
```

After squad leader creation (line ~33):
```cpp
team.squad_individual.topology.input_ids = build_squad_leader_input_labels();
team.squad_individual.topology.output_ids = build_squad_leader_output_labels();
```

After fighter creation (line ~47 for random, or after convert/copy for variant):
```cpp
team.fighter_individual.topology.input_ids = build_arena_fighter_input_labels(fighter_design);
team.fighter_individual.topology.output_ids = build_output_ids(fighter_design);
```

This requires adding `#include <neuroflyer/sensor_engine.h>` and `#include <neuroflyer/arena_sensor.h>` to team_evolution.cpp.

- [ ] **Step 1:** Add ID population to `Individual::from_design()` in evolution.cpp
- [ ] **Step 2:** Add ID population to `TeamIndividual::create()` in team_evolution.cpp
- [ ] **Step 3:** Build and run tests
- [ ] **Step 4:** Commit

---

### Task 3: Snapshot v8 — Serialize input_ids and output_ids

**Files:**
- Modify: `src/engine/snapshot_io.cpp`

**Changes:**

1. Bump version: `constexpr uint16_t CURRENT_VERSION = 8;`

2. In `write_payload()`, after writing topology layers (after the layer loop, around line 135), add:
```cpp
// v8: node IDs
write_val<uint32_t>(out, static_cast<uint32_t>(snap.topology.input_ids.size()));
for (const auto& id : snap.topology.input_ids) {
    write_string(out, id);
}
write_val<uint32_t>(out, static_cast<uint32_t>(snap.topology.output_ids.size()));
for (const auto& id : snap.topology.output_ids) {
    write_string(out, id);
}
```

3. In `parse_payload()`, after reading topology layers (after the layer loop, around line 210), add:
```cpp
// v8: node IDs
if (version >= 8) {
    uint32_t input_id_count = read_val<uint32_t>(in);
    snap.topology.input_ids.reserve(input_id_count);
    for (uint32_t i = 0; i < input_id_count; ++i) {
        snap.topology.input_ids.push_back(read_string(in));
    }
    uint32_t output_id_count = read_val<uint32_t>(in);
    snap.topology.output_ids.reserve(output_id_count);
    for (uint32_t i = 0; i < output_id_count; ++i) {
        snap.topology.output_ids.push_back(read_string(in));
    }
}
```

- [ ] **Step 1:** Bump CURRENT_VERSION to 8
- [ ] **Step 2:** Add write logic for IDs in write_payload()
- [ ] **Step 3:** Add read logic for IDs in parse_payload() (version >= 8 guard)
- [ ] **Step 4:** Build and run tests
- [ ] **Step 5:** Commit

---

### Task 4: Snapshot v8 Tests

**Files:**
- Modify: `tests/snapshot_io_test.cpp`

**Tests to add:**

```cpp
TEST(SnapshotIOTest, V8RoundTripWithNodeIds) {
    nf::Snapshot snap;
    snap.name = "test-ids";
    snap.topology.input_size = 3;
    snap.topology.input_ids = {"sensor_0", "heading", "speed"};
    snap.topology.layers = {{2, neuralnet::Activation::Tanh, {}}};
    snap.topology.output_ids = {"left", "right"};
    snap.weights = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    snap.net_type = nf::NetType::Fighter;

    // Save and reload
    std::string path = /* temp path */;
    nf::save_snapshot(snap, path);
    auto loaded = nf::load_snapshot(path);

    EXPECT_EQ(loaded.topology.input_ids.size(), 3u);
    EXPECT_EQ(loaded.topology.input_ids[0], "sensor_0");
    EXPECT_EQ(loaded.topology.input_ids[1], "heading");
    EXPECT_EQ(loaded.topology.input_ids[2], "speed");
    EXPECT_EQ(loaded.topology.output_ids.size(), 2u);
    EXPECT_EQ(loaded.topology.output_ids[0], "left");
    EXPECT_EQ(loaded.topology.output_ids[1], "right");
}

TEST(SnapshotIOTest, V7BackwardCompatNoIds) {
    // Load an existing v7 snapshot (or save one with v7, load back)
    // Verify input_ids and output_ids are empty, no crash
}
```

- [ ] **Step 1:** Add v8 round-trip test
- [ ] **Step 2:** Add v7 backward compat test
- [ ] **Step 3:** Build and run: `./build/tests/neuroflyer_tests --gtest_filter='SnapshotIO*'`
- [ ] **Step 4:** Commit

---

### Task 5: adapt_individual_inputs() + AdaptReport

**Files:**
- Modify: `include/neuroflyer/snapshot_utils.h` (add declarations)
- Modify: `src/engine/evolution.cpp` (add implementation)

**In `snapshot_utils.h`, add:**
```cpp
#include <neuralnet/adapt.h>
#include <random>

struct AdaptReport {
    std::vector<std::string> added;
    std::vector<std::string> removed;
    [[nodiscard]] bool needed() const { return !added.empty() || !removed.empty(); }
    [[nodiscard]] std::string message() const;
};

/// Adapt an Individual's topology to match target input IDs.
/// Returns the adapted Individual and a report of what changed.
/// If source has no input_ids (legacy), returns source unchanged with empty report.
[[nodiscard]] std::pair<Individual, AdaptReport> adapt_individual_inputs(
    const Individual& source,
    const std::vector<std::string>& target_input_ids,
    const ShipDesign& design,
    std::mt19937& rng);
```

**In `evolution.cpp`, implement:**

`AdaptReport::message()`:
```cpp
std::string AdaptReport::message() const {
    std::string msg;
    if (!added.empty()) {
        msg += "Adding nodes with random weights:";
        for (const auto& id : added) msg += " " + id;
        msg += "\n";
    }
    if (!removed.empty()) {
        msg += "Removing unused nodes:";
        for (const auto& id : removed) msg += " " + id;
    }
    return msg;
}
```

`adapt_individual_inputs()`:
1. If `source.topology.input_ids.empty()`: return `{source, {}}` — no IDs means legacy, can't adapt by ID.
2. If source input_ids == target_input_ids: return `{source, {}}` — exact match, no adaptation needed.
3. Call `neuralnet::adapt_topology_inputs(source.topology, source.genome.flatten("layer_"), target_input_ids, rng)`.
4. Build adapted Individual from result: set topology, rebuild genome skeleton, fill weights from adapted_weights.
5. Set output_ids from `build_output_ids(design)`.
6. Return `{adapted, {result.added_ids, result.removed_ids}}`.

- [ ] **Step 1:** Add declarations to `snapshot_utils.h`
- [ ] **Step 2:** Implement `AdaptReport::message()` and `adapt_individual_inputs()` in `evolution.cpp`
- [ ] **Step 3:** Build and run tests
- [ ] **Step 4:** Commit

---

### Task 6: Replace convert_variant_to_fighter() Internals

**Files:**
- Modify: `src/engine/evolution.cpp`

Replace the manual column remapping logic (the ~60 lines of weight matrix manipulation in convert_variant_to_fighter) with:

```cpp
Individual convert_variant_to_fighter(const Individual& variant, const ShipDesign& design) {
    auto target_ids = build_arena_fighter_input_labels(design);
    auto target_output_ids = build_output_ids(design);

    auto source = variant;

    // If source has no input_ids, assign scroller labels
    if (source.topology.input_ids.empty()) {
        source.topology.input_ids = build_input_labels(design);
    }
    if (source.topology.output_ids.empty()) {
        source.topology.output_ids = build_output_ids(design);
    }

    // Adapt topology + weights to arena fighter layout
    auto flat_weights = source.genome.flatten("layer_");
    std::mt19937 rng(std::random_device{}());
    auto result = neuralnet::adapt_topology_inputs(
        source.topology, flat_weights, target_ids, rng);

    // Build adapted Individual
    Individual adapted;
    adapted.topology = result.adapted_topology;
    adapted.topology.output_ids = target_output_ids;
    adapted.genome = build_genome_skeleton(design, adapted.topology);

    // Fill genome weights from adapted flat weights
    std::size_t offset = 0;
    for (std::size_t l = 0; l < adapted.topology.layers.size(); ++l) {
        std::string lp = "layer_" + std::to_string(l);
        if (adapted.genome.has_gene(lp + "_weights")) {
            auto& gene = adapted.genome.gene(lp + "_weights");
            for (auto& v : gene.values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
        if (adapted.genome.has_gene(lp + "_biases")) {
            auto& gene = adapted.genome.gene(lp + "_biases");
            for (auto& v : gene.values) {
                v = (offset < result.adapted_weights.size())
                    ? result.adapted_weights[offset++] : 0.0f;
            }
        }
    }

    return adapted;
}
```

This requires `#include <neuralnet/adapt.h>` and `#include <neuroflyer/arena_sensor.h>` in evolution.cpp.

- [ ] **Step 1:** Replace convert_variant_to_fighter() body
- [ ] **Step 2:** Build and run all tests (especially evolution_test, fighter_drill, attack_run tests)
- [ ] **Step 3:** Commit

---

### Task 7: UI Modal on Mismatch

**Files:**
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp`

In the `Action::FighterDrill` and `Action::AttackRuns` handlers, after loading the snapshot and before pushing the screen, check if adaptation is needed:

```cpp
case Action::FighterDrill: {
    // ... existing variant selection ...
    try {
        auto snap = load_snapshot(variant_path(sel));
        auto ind = snapshot_to_individual(snap);

        // Check if adaptation is needed for fighter context
        auto target_ids = build_arena_fighter_input_labels(snap.ship_design);
        if (!ind.topology.input_ids.empty() && ind.topology.input_ids != target_ids) {
            auto [adapted, report] = adapt_individual_inputs(
                ind, target_ids, snap.ship_design, state.rng);
            if (report.needed()) {
                // Show modal, then launch on confirm
                std::string msg = report.message();
                auto snap_copy = snap;
                snap_copy.topology = adapted.topology;
                snap_copy.weights = adapted.genome.flatten("layer_");
                ui.push_modal(std::make_unique<ConfirmModal>(
                    "Network Adaptation",
                    msg + "\nContinue?",
                    [this, snap_copy, &state, &ui]() {
                        state.return_to_variant_view = true;
                        ui.push_screen(std::make_unique<FighterDrillScreen>(
                            snap_copy, vs_.genome_dir, /* variant name */));
                    }));
                break;
            }
        }
        // No adaptation needed — launch directly
        state.return_to_variant_view = true;
        ui.push_screen(std::make_unique<FighterDrillScreen>(
            std::move(snap), vs_.genome_dir, sel.name));
    } catch (...) { /* existing error handling */ }
    break;
}
```

Same pattern for `Action::AttackRuns`. For `Action::SquadSkirmish`, the adaptation happens inside `TeamIndividual::create()` which already handles mismatches via the input_size check — the modal would trigger at that level.

This requires adding `#include <neuroflyer/snapshot_utils.h>` and `#include <neuroflyer/arena_sensor.h>` to variant_viewer_screen.cpp.

- [ ] **Step 1:** Add adaptation check + modal in FighterDrill handler
- [ ] **Step 2:** Add adaptation check + modal in AttackRuns handler
- [ ] **Step 3:** Build and verify app compiles
- [ ] **Step 4:** Commit

---

### Task 8: Tests for ID Population and Adaptation

**Files:**
- Modify: `tests/evolution_test.cpp`

**Tests:**

```cpp
TEST(EvolutionTest, FromDesignSetsInputIds) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    EXPECT_FALSE(ind.topology.input_ids.empty());
    EXPECT_EQ(ind.topology.input_ids.size(), ind.topology.input_size);
    EXPECT_FALSE(ind.topology.output_ids.empty());
}

TEST(EvolutionTest, AdaptIndividualNoOp) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    auto target = ind.topology.input_ids;  // same IDs
    auto [adapted, report] = nf::adapt_individual_inputs(ind, target, design, rng);
    EXPECT_FALSE(report.needed());
}

TEST(EvolutionTest, AdaptIndividualAddsRemoves) {
    nf::ShipDesign design;
    design.memory_slots = 2;
    std::mt19937 rng(42);
    auto ind = nf::Individual::from_design(design, {4}, rng);
    // Add a fake ID, remove one existing
    auto target = ind.topology.input_ids;
    target.push_back("NEW_INPUT");
    target.erase(target.begin());  // remove first
    auto [adapted, report] = nf::adapt_individual_inputs(ind, target, design, rng);
    EXPECT_TRUE(report.needed());
    EXPECT_FALSE(report.added.empty());
    EXPECT_FALSE(report.removed.empty());
}
```

- [ ] **Step 1:** Add tests
- [ ] **Step 2:** Build and run: `./build/tests/neuroflyer_tests --gtest_filter='Evolution*'`
- [ ] **Step 3:** Commit

---

### Task 9: Full Build + All Tests

- [ ] **Step 1:** Run full test suite: `./build/tests/neuroflyer_tests`
- [ ] **Step 2:** Build main app: `cmake --build build --target neuroflyer`
- [ ] **Step 3:** Verify no regressions
- [ ] **Step 4:** Manual test: load old v7 snapshot in variant viewer → no crash, IDs empty. Create new variant → save → reload → IDs preserved.

---

## Verification

1. `./build/tests/neuroflyer_tests` — all tests pass
2. `cmake --build build --target neuroflyer` — clean build
3. Old v7 snapshots load without crash (empty IDs)
4. New snapshots saved with v8 format contain IDs
5. Opening Fighter Drill with a mismatched net shows modal
6. Manual: Hangar → select variant → Fighter Drill → if net needs adaptation, modal appears with node list → Continue proceeds, Cancel goes back

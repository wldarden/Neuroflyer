# NeuroFlyer Node IDs Integration — Design Spec (Phase 2)

**Date:** 2026-04-06
**Status:** Draft
**Depends on:** Phase 1 (neuralnet library node IDs — completed)

## Overview

Integrate the neuralnet library's node ID system into NeuroFlyer. Every network topology gets string IDs on its input and output nodes. Snapshots serialize these IDs (format v8). `convert_variant_to_fighter()` is replaced with the library's `adapt_topology_inputs()`. When a net is loaded into a context where its IDs don't match, a modal warns the user about added/removed nodes before proceeding.

## Requirements

1. All topology creation paths populate `input_ids` and `output_ids` using existing label functions.
2. Snapshot format bumped to v8 — writes/reads ID vectors. v7 and earlier load with empty IDs (backward compatible).
3. `convert_variant_to_fighter()` replaced with `adapt_topology_inputs()` for automatic weight adaptation.
4. UI modal shown when loading a net that needs adaptation (lists added/removed node names).
5. No changes to `forward()` call sites — they remain positional. IDs are for adaptation and user communication, not runtime validation.

## Architecture

### 1. ID Population — Topology Creation

Every path that creates a `NetworkTopology` must also set `input_ids` and `output_ids`.

**Helper functions** (new, in existing files):

| Function | File | Returns |
|----------|------|---------|
| `build_solo_output_ids(ShipDesign)` | `sensor_engine.h/cpp` | `{"UP", "DN", "LF", "RT", "SH", "M0", "M1", ...}` |
| `build_fighter_output_ids(ShipDesign)` | `arena_sensor.h/cpp` | Same as solo: `{"UP", "DN", "LF", "RT", "SH", "M0", ...}` |
| `build_ntm_input_ids()` | `sensor_engine.h/cpp` | `{"H Sin", "H Cos", "Dist", "HP", "Alive%", "IsShip", "IsBase"}` |
| `build_ntm_output_ids()` | `sensor_engine.h/cpp` | `{"Threat"}` |

Existing functions that already return the right labels (reuse directly as IDs):
- `build_input_labels(design)` → solo input IDs
- `build_arena_fighter_input_labels(design)` → fighter input IDs
- `build_squad_leader_input_labels()` → squad leader input IDs
- `build_squad_leader_output_labels()` → squad leader output IDs

**Where IDs get set:**

| Creation path | File | Change |
|---------------|------|--------|
| `Individual::random(input, hidden, output, rng)` | `evolution.cpp` | No change — caller must set IDs on topology after creation (generic function doesn't know net type) |
| `Individual::from_design(design, hidden, rng)` | `evolution.cpp` | Set `topology.input_ids = build_input_labels(design)`, `topology.output_ids = build_solo_output_ids(design)` |
| `TeamIndividual::create(...)` | `team_evolution.cpp` | After creating each sub-individual, set IDs: NTM gets `build_ntm_input/output_ids()`, squad leader gets `build_squad_leader_input/output_labels()`, fighter gets `build_arena_fighter_input_labels(design)` + `build_fighter_output_ids(design)` |
| `create_population_from_snapshot(...)` | `evolution.cpp` | Snapshot already has IDs (from v8+) or empty (from v7-). No change needed — IDs propagate through mutation. |

### 2. Snapshot Serialization (v8)

**File:** `src/engine/snapshot_io.cpp`

Bump `CURRENT_VERSION` from 7 to 8.

**write_payload():** After writing topology layers, write:
```
write_val<uint32_t>(count of input_ids)
for each id: write_val<uint16_t>(string length), write string bytes
write_val<uint32_t>(count of output_ids)
for each id: write_val<uint16_t>(string length), write string bytes
```

**parse_payload():** When version >= 8, after reading topology layers, read the ID vectors using the same format. When version < 8, leave `input_ids`/`output_ids` empty.

### 3. Replace `convert_variant_to_fighter()` with `adapt_topology_inputs()`

**File:** `src/engine/evolution.cpp`

Current `convert_variant_to_fighter()` (lines 247-327) manually remaps ~70 lines of column-by-column weight logic. Replace with:

```cpp
Individual convert_variant_to_fighter(const Individual& variant, const ShipDesign& design) {
    // Build target input IDs for arena fighter
    auto target_ids = build_arena_fighter_input_labels(design);

    // If source has no input_ids, assign them based on its topology
    auto source = variant;
    if (source.topology.input_ids.empty()) {
        source.topology.input_ids = build_input_labels(design);  // scroller labels
    }

    // Adapt topology + weights to new input layout
    auto flat_weights = source.genome.flatten("layer_");
    std::mt19937 rng(std::random_device{}());
    auto result = neuralnet::adapt_topology_inputs(
        source.topology, flat_weights, target_ids, rng);

    // Build new Individual with adapted topology
    Individual adapted;
    adapted.topology = result.adapted_topology;
    adapted.topology.output_ids = build_fighter_output_ids(design);
    adapted.genome = build_genome_skeleton(design, adapted.topology);
    // Fill genome from adapted weights...
    // (same unflatten pattern as snapshot_to_individual)

    return adapted;
}
```

This handles any input layout mismatch automatically — sensors that match get their weights preserved, new inputs get random weights, dropped inputs are removed.

### 4. UI Modal on Mismatch

When a screen loads a net and needs to adapt it, show a `ConfirmModal` with the adaptation report.

**Where it triggers:** In the variant viewer's action handlers (`Action::FighterDrill`, `Action::AttackRuns`, `Action::SquadSkirmish`) and in `TeamIndividual::create()` when a variant's topology doesn't match the expected input size.

**Helper function** (new, in `snapshot_utils.h/cpp`):

```cpp
struct AdaptReport {
    std::vector<std::string> added;
    std::vector<std::string> removed;
    bool needed() const { return !added.empty() || !removed.empty(); }
    std::string message() const;  // "Adding: X, Y, Z\nRemoving: A, B"
};

/// Try to adapt an Individual to match expected input IDs.
/// Returns the adapted Individual + a report of what changed.
std::pair<Individual, AdaptReport> adapt_individual_inputs(
    const Individual& source,
    const std::vector<std::string>& target_input_ids,
    const std::vector<std::string>& target_output_ids,
    std::mt19937& rng);
```

**UI flow:**
1. Load snapshot → `snapshot_to_individual()`
2. Compute expected IDs for the target context (fighter drill, attack runs, etc.)
3. If `individual.topology.input_ids` is non-empty and doesn't match target: call `adapt_individual_inputs()`
4. If adaptation was needed: push `ConfirmModal` with the report message + "Continue"/"Cancel"
5. On "Continue": proceed with adapted individual
6. On "Cancel": pop back to variant viewer

### 5. File Layout

| Component | File | Change |
|-----------|------|--------|
| ID helpers | `sensor_engine.h/cpp` | Add `build_solo_output_ids()`, `build_ntm_input_ids()`, `build_ntm_output_ids()` |
| ID helpers | `arena_sensor.h/cpp` | Add `build_fighter_output_ids()` |
| Topology creation | `evolution.cpp` | Set IDs in `from_design()` |
| Topology creation | `team_evolution.cpp` | Set IDs in `TeamIndividual::create()` |
| Snapshot I/O | `snapshot_io.cpp` | v8: read/write ID vectors |
| Conversion | `evolution.cpp` | Replace `convert_variant_to_fighter()` with adapt-based version |
| Adaptation helper | `snapshot_utils.h/cpp` | Add `AdaptReport`, `adapt_individual_inputs()` |
| UI warnings | `variant_viewer_screen.cpp` | Show modal on mismatch in drill launch actions |
| Tests | `tests/` | Test v8 round-trip, adapt_individual_inputs, ID population |

### Tests

1. **Snapshot v8 round-trip** — save snapshot with IDs, load back, verify IDs preserved.
2. **Snapshot v7 backward compat** — load old v7 snapshot, verify empty IDs, no crash.
3. **Individual::from_design sets IDs** — create from design, verify input_ids/output_ids non-empty and correct size.
4. **TeamIndividual::create sets IDs** — create team, verify all 3 sub-individuals have IDs.
5. **adapt_individual_inputs: no-op** — same IDs → no changes, report empty.
6. **adapt_individual_inputs: adds/removes** — different IDs → adapted individual works, report lists changes.
7. **convert_variant_to_fighter with IDs** — convert a scroller net to fighter, verify adapted topology has fighter input IDs.

## Out of Scope

- Runtime input validation in `forward()` (IDs are for adaptation, not per-tick checks)
- Output node adaptation (outputs rarely mismatch — only inputs change between contexts)
- Hidden node IDs (not needed — only boundary nodes participate in cross-context matching)
- Automatic adaptation without user confirmation (always show modal)

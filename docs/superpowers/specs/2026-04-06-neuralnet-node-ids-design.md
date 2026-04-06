# Neuralnet Library: Node IDs — Design Spec

**Date:** 2026-04-06
**Status:** Draft
**Scope:** neuralnet library only (Phase 1). Neuroflyer integration is a follow-up.

## Overview

Add optional string IDs to input and output nodes in `NetworkTopology`. This enables identity-based matching when a network is moved between contexts with different input/output layouts — consumers can detect which nodes are missing or extra, adapt weights accordingly, and warn the user.

## Problem

Networks currently use positional indices for inputs and outputs. When a network saved with one sensor configuration is loaded into a context expecting a different one, `Network::forward()` throws an input size mismatch exception and the app crashes. There's no way to know *which* inputs are missing or extra — just that the count is wrong.

## Requirements

1. `NetworkTopology` gains optional `input_ids` and `output_ids` string vectors.
2. When IDs are present, their count must equal `input_size` / last layer `output_size`.
3. When IDs are empty (legacy), behavior is unchanged — purely positional.
4. `Network` exposes the IDs via const accessors.
5. `forward()` is unchanged — still validates by size, not by ID.
6. An `adapt_input()` utility reorders/pads/trims a float vector to match a target ID set.
7. An `adapt_topology_inputs()` utility rewires first-layer weights to match a new input ID set, reporting which IDs were added (random weights) and removed (dropped).
8. All existing code continues to work without modification (backward compatible).

## Architecture

### NetworkTopology Changes

```cpp
struct NetworkTopology {
    std::size_t input_size;
    std::vector<LayerDef> layers;

    // Optional: named input/output nodes for identity-based matching.
    // When non-empty, size must equal input_size / last layer output_size.
    // When empty, nodes are positional (legacy behavior).
    std::vector<std::string> input_ids;
    std::vector<std::string> output_ids;
};
```

No changes to `LayerDef`. Hidden nodes don't get IDs — they're internal.

### Network Class Changes

Add const accessors only:

```cpp
class Network {
public:
    // ... existing API unchanged ...

    [[nodiscard]] const std::vector<std::string>& input_ids() const noexcept;
    [[nodiscard]] const std::vector<std::string>& output_ids() const noexcept;
};
```

Constructor stores the IDs from topology. `forward()` unchanged.

### Adaptation Utilities

New header: `include/neuralnet/adapt.h`

```cpp
#pragma once

#include <neuralnet/network.h>

#include <random>
#include <string>
#include <vector>

namespace neuralnet {

/// Reorder/pad/trim a float vector from source IDs to target IDs.
/// - Present in both: copied to correct position in output.
/// - In target but not source (missing): filled with default_value.
/// - In source but not target (extra): ignored.
/// Returns vector sized to target_ids.size().
[[nodiscard]] std::vector<float> adapt_input(
    const std::vector<std::string>& source_ids,
    const std::vector<float>& values,
    const std::vector<std::string>& target_ids,
    float default_value = 0.0f);

/// Result of adapting a topology's input layout to a new set of IDs.
struct AdaptResult {
    std::vector<float> adapted_weights;    // new flat weight vector
    NetworkTopology adapted_topology;       // topology with updated input_size + input_ids
    std::vector<std::string> added_ids;     // IDs added (missing from source, filled with random weights)
    std::vector<std::string> removed_ids;   // IDs removed (extra in source, columns dropped)
};

/// Adapt a topology's first-layer weights to match a new input ID set.
/// - Columns for matching IDs are preserved in the new order.
/// - Columns for missing IDs are filled with small random weights.
/// - Columns for extra IDs are dropped.
/// Higher layers and output IDs are unchanged.
/// Requires source_topology.input_ids to be non-empty (throws if empty).
[[nodiscard]] AdaptResult adapt_topology_inputs(
    const NetworkTopology& source_topology,
    const std::vector<float>& source_weights,
    const std::vector<std::string>& target_input_ids,
    std::mt19937& rng);

} // namespace neuralnet
```

### Implementation Details

**`adapt_input()`:**
1. Build a map: `source_id → index` from source_ids.
2. For each target_id at position j: if found in map, copy `values[map[source_id]]` to `result[j]`. Otherwise, set `result[j] = default_value`.

**`adapt_topology_inputs()`:**
1. Build a map: `source_id → column_index` from source topology's input_ids.
2. Compute new input_size = target_input_ids.size().
3. For the first layer (layer 0): rewrite its weight matrix.
   - Old layout: `old_output_size × old_input_size` (row-major).
   - New layout: `old_output_size × new_input_size` (row-major).
   - For each output row, for each target input column j:
     - If target_input_ids[j] exists in source: copy `old_weights[row * old_input_size + source_col]`.
     - If missing: fill with `uniform(-0.1, 0.1)` from rng.
   - Biases for layer 0: unchanged.
4. Higher layer weights: unchanged (copied as-is from source_weights).
5. Build new topology: same layers, new `input_size`, new `input_ids`. `output_ids` preserved.
6. Report `added_ids` (in target but not source) and `removed_ids` (in source but not target).

### File Layout

| Component | Header | Source |
|-----------|--------|--------|
| Topology + Network changes | `include/neuralnet/network.h` | `src/network.cpp` |
| Adaptation utilities | `include/neuralnet/adapt.h` | `src/adapt.cpp` |
| Tests | — | `tests/adapt_test.cpp` |

### Tests

1. **TopologyWithIds** — construct topology with input_ids/output_ids, verify sizes.
2. **TopologyWithoutIds** — construct without IDs, verify empty vectors.
3. **NetworkExposesIds** — build Network with IDs, verify accessors return them.
4. **NetworkLegacyNoIds** — build Network without IDs, verify empty vectors.
5. **AdaptInputExactMatch** — same IDs same order, output == input.
6. **AdaptInputReorder** — same IDs different order, values rearranged.
7. **AdaptInputMissing** — target has IDs not in source, filled with default.
8. **AdaptInputExtra** — source has IDs not in target, ignored, output sized to target.
9. **AdaptInputMixed** — reorder + missing + extra combined.
10. **AdaptTopologyAddColumns** — new input IDs get random weight columns, topology.input_size updated.
11. **AdaptTopologyRemoveColumns** — extra IDs dropped, weight matrix shrinks.
12. **AdaptTopologyReportsAddedRemoved** — verify AdaptResult.added_ids and removed_ids.
13. **AdaptTopologyPreservesHigherLayers** — layers 1+ weights unchanged.
14. **AdaptTopologyPreservesOutputIds** — output_ids unchanged after adaptation.

## Out of Scope

- Neuroflyer integration (snapshot serialization, UI warnings, auto-adaptation on load) — Phase 2.
- Hidden node IDs — not needed for cross-context matching.
- Output node adaptation (only input adaptation for now; output mismatches are rarer).
- Changes to `Layer` class internals.
- Changes to `forward()` behavior.

# NeuroFlyer Structured Evolution — Design Spec

**Date:** 2026-03-24
**Status:** Ready to implement (blocked on save system refactor completion)
**Scope:** `neuroflyer/` evolution system — migrate Individual from flat Genome to StructuredGenome
**Depends on:** Save system refactor (in progress by sibling), StructuredGenome in libs/evolve (DONE)

## Problem

NeuroFlyer's `Individual` uses a flat `evolve::Genome` (vector of floats) where every float is a network weight. This means:

1. Sensor parameters (range, width, angle) can't evolve — they're on global config, not per-individual
2. Crossover mixes floats blindly — parent A's layer 0 weights get scrambled with parent B's layer 1 weights
3. No per-gene mutation control — network weights and hypothetical sensor params would get the same mutation rate
4. Adding evolvable parameters requires reserving floats at arbitrary positions, which breaks when topology mutations resize the weight vector

## Solution

Replace `evolve::Genome` on `Individual` with `evolve::StructuredGenome`. Each concept (sensor range, layer weights, memory slots, speed) becomes a named gene with its own mutation config. Related genes form linkage groups that crossover atomically.

## Prerequisites (must be done first)

- [x] `StructuredGenome` implemented in `libs/evolve/` (24 tests passing)
- [ ] Save system refactor landed (sibling's work — new `Snapshot` format with `ShipDesign`)
- [ ] `save_population`/`load_population` deprecated calls resolved in main.cpp

## Design

### Individual v2

```cpp
struct Individual {
    neuralnet::NetworkTopology topology;
    evolve::StructuredGenome genome;
    float fitness = 0.0f;

    [[nodiscard]] neuralnet::Network build_network() const;
    [[nodiscard]] ShipDesign effective_ship_design() const;
};
```

`build_network()` calls `genome.flatten("weight_")` to extract weights.
`effective_ship_design()` reads sensor genes to produce a `ShipDesign` reflecting this individual's evolved sensor layout.

### Gene Layout

Built from a `ShipDesign` (the genome-level template):

```
Per sensor (linkage group "sensor_N"):
  sensor_N_angle    — float, radians        — from ShipDesign.sensors[N].angle
  sensor_N_range    — float, pixels          — from ShipDesign.sensors[N].range
  sensor_N_width    — float, radians         — from ShipDesign.sensors[N].width
  sensor_N_noise    — float, 0-1             — default 0.0

Per network layer (linkage group "layer_N"):
  weight_LN         — M floats               — random init
  bias_LN           — K floats               — zero init

Standalone genes:
  speed             — float                   — from GameConfig.ship_speed
  memory_slots      — float (rounded to int)  — from ShipDesign.memory_slots
```

### Which Genes Are Evolvable

The `ShipDesign` (genome template) defines the INITIAL values. A separate config (per-genome, not per-individual) controls which genes are evolvable:

```cpp
struct EvolvableConfig {
    bool evolve_sensor_range = false;
    bool evolve_sensor_width = false;
    bool evolve_sensor_angle = false;
    bool evolve_sensor_noise = false;
    bool evolve_speed = false;
    bool evolve_memory_slots = false;
    // Weights are always evolvable
};
```

When a gene is not evolvable, its `GeneMutationConfig.evolvable = false` — it's frozen and always inherited from parent A during crossover.

### Population Creation

```
ShipDesign (template) + EvolvableConfig + hidden_layers
    → build StructuredGenome skeleton (genes + linkage groups)
    → randomize weight genes
    → set sensor genes from ShipDesign initial values
    → clone N times with weight mutations for diversity
    → population ready
```

### Evolution Loop Changes

Current:
```cpp
// In evolve_population():
child.genome = evolve::crossover_uniform(parent_a.genome, parent_b.genome, rng);
evolve::mutate(child.genome, mutation_config, rng);
```

New:
```cpp
// crossover respects linkage groups, per-gene evolvable flags
child.genome = evolve::crossover(parent_a.genome, parent_b.genome, rng);
// mutate respects per-gene rates, strengths, clamps, evolvable flags
evolve::mutate(child.genome, rng);
```

No separate `MutationConfig` passed in — each gene carries its own config.

### Topology Mutations

`add_node`, `remove_node`, `add_layer`, `remove_layer` currently resize the flat genome. With StructuredGenome:

- `add_node(ind)` → find the `weight_LN` gene for a random hidden layer, expand its values array, update topology
- `remove_node(ind)` → shrink the `weight_LN` gene's values array
- `add_layer(ind)` → insert new `weight_LN` and `bias_LN` genes, add to a new linkage group
- `remove_layer(ind)` → remove the genes and linkage group

The weight genes have variable-length `values` vectors. This is fine — `StructuredGenome` doesn't require fixed sizes. The gene tags stay the same; only the value count changes.

### Game Loop Integration

Each tick, instead of reading sensor config from global state:

```cpp
// OLD:
auto rays = cast_rays(tri, towers, tokens, RAY_RANGE, NUM_RAYS);

// NEW:
ShipDesign design = individual.effective_ship_design();
auto inputs = compute_inputs(tri, towers, tokens, design);
```

The `effective_ship_design()` method reads sensor genes and builds a `ShipDesign` with that individual's evolved values. This means different individuals in the same generation can have different sensor ranges/widths if those genes are evolvable.

### Renderer Impact

When viewing a specific individual (Best/Worst view), the renderer should display THAT individual's sensor layout, not the genome template. This means:

- Occulus oval sizes vary per individual
- Ray ranges vary per individual
- The "focused individual" drives the sensor visualization

### What Changes vs Current Code

| Component | Current | New |
|---|---|---|
| `Individual.genome` | `evolve::Genome` (flat floats) | `evolve::StructuredGenome` (named genes) |
| `build_network()` | `Network(topology, genome.genes())` | `Network(topology, genome.flatten("weight_"))` |
| Crossover | `crossover_uniform` (per-float coin flip) | `crossover` (per-gene/linkage-group) |
| Mutation | One global rate/strength | Per-gene rate/strength/clamp |
| Sensor params | Global `GameConfig` | Per-individual genes |
| `EvolutionConfig` | Flat struct with rates | Simplified — per-gene configs on the genome |
| Topology mutations | Resize flat genome vector | Resize specific weight/bias gene values |

### What Does NOT Change

- `neuralnet::Network` — still takes flat weights, doesn't know about genes
- `GameSession` — still runs the game, doesn't know about evolution
- Ray casting / Occulus detection — still works the same, just reads from `ShipDesign`
- `ShipDesign` struct — unchanged, used as both template and per-individual state
- Save format — sibling's new format stores `ShipDesign` + weights, which can be reconstructed into a `StructuredGenome`

### Compatibility with Save System

The sibling's `Snapshot` stores: `ShipDesign` + `NetworkTopology` + `weights` (flat vector).

To save a `StructuredGenome`:
- `weights` = `genome.flatten("weight_")` + `genome.flatten("bias_")`
- `ShipDesign` = `individual.effective_ship_design()` (reads sensor genes)

To load into a `StructuredGenome`:
- Build gene skeleton from `ShipDesign` (sensor genes) + `NetworkTopology` (weight genes)
- Fill weight genes from the flat `weights` vector
- Sensor genes get their values from `ShipDesign.sensors[N].range/width/angle`

This round-trips cleanly. The `StructuredGenome` is an in-memory representation; the save format doesn't need to know about gene names or linkage groups.

## Implementation Phases

### Phase 1: Core Migration
- Change `Individual.genome` to `StructuredGenome`
- Update `Individual::random()` to build gene skeleton
- Update `build_network()` to use `flatten("weight_")`
- Update `evolve_population()` to use new crossover/mutate
- Update topology mutations to operate on named genes
- Update all tests

### Phase 2: Evolvable Sensor Params
- Add sensor range/width/angle genes
- Add `EvolvableConfig` to control which genes evolve
- Update game loop to read sensor params from individual
- Update renderer to show per-individual sensors
- Add UI toggle for "evolve sensors"

### Phase 3: Speed Gene
- Add `speed` gene
- Scale fitness by speed (fast ships earn less per distance unit)
- Update game loop to use per-individual speed

### Phase 4: Sensor Noise
- Add `noise` gene per sensor
- Add Gaussian noise to sensor readings scaled by gene value

### Phase 5 (Future): Structural Sensor Mutations
- Add/remove sensors (changes input size → needs careful handling)
- Toggle sensor type (Raycast ↔ Occulus)
- Toggle is_full_sensor

## Open Questions

1. **Should `EvolvableConfig` be per-genome or per-run?** Per-genome seems right — "Lotus has evolvable sensors, TripleThreat doesn't." But this means it needs to be saved with the genome.

2. **How to handle crossover between individuals with different topologies?** Currently topology mutations can make two individuals incompatible for crossover (different weight counts). The existing code tries 5 times to find a same-topology partner, then falls back to asexual. With StructuredGenome, crossover already requires matching gene tags — same solution applies.

3. **Should evolved sensor params feed back into ShipDesign for the save format?** Yes — `effective_ship_design()` produces a `ShipDesign` reflecting the individual's genes, and that's what gets saved in the Snapshot. On load, the sensor genes are initialized from the saved ShipDesign.

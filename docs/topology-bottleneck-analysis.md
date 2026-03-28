# Topology Bottleneck Problem

**Discovered:** 2026-03-24, analyzing the "Lotus-2" saved population

## The Problem

The best individual in Lotus-2 (fitness 356k) evolved a 5-layer topology: `36 -> 12 -> 9 -> 13 -> 1 -> 10`. The single-neuron layer (13 -> 1) is a dead bottleneck — its sum|w| = 14.5 with bias +2.4, so its output is permanently saturated at +1 regardless of input. The entire output layer receives a constant signal and produces fixed actions (always UP, always SHOOT, never LEFT). The network scores well by doing one thing reliably, but it can't actually react to its environment.

## Weight Analysis

Every layer has mean |w| > 1.0. All neurons in layers 0-3 are fully saturated (sum|w| per neuron ranges from 12 to 53). The Tanh activations are operating as hard binary switches rather than smooth functions. Memory inputs are strongly wired in layer 0 (|w| > 1.0 on average), but memory outputs go through the dead bottleneck so they write constant values — memory is functionally dead.

## How It Happened

1. **`add_layer` inserts layers with 1 neuron.** A single neuron is an information-theoretic bottleneck — it can only pass one scalar, destroying all intermediate features the prior layers computed.

2. **The bottleneck is hard to undo.** `remove_layer` fires rarely (0.2%), picks a random layer (not the problematic one), and scrambles surrounding weights via `resize_genome` (pads with tiny [-0.1, 0.1] values). Removing a layer is almost always destructive to fitness.

3. **Elitism preserves broken topologies.** The bottleneck individual is the fittest (because its pre-bottleneck layers are good feature detectors), so it survives every generation and seeds offspring with the same topology. Crossover requires matching topologies, trapping the population in a local optimum.

4. **No weight regularization.** Weights drift outward over generations — mutation adds noise but never shrinks. Once weights exceed ~3.0, Tanh saturates and further mutations to that weight have no effect on output. This is a ratchet: weights grow, neurons freeze, evolution can't fix them.

## Proposed Fixes

### 1. Minimum layer size on insertion (high impact, easy)

When `add_layer` fires, start with at least 3-4 neurons instead of 1. A 1-neuron hidden layer is almost never useful. Could use the average or minimum of adjacent layer sizes as the initial size.

### 2. Weight clamping (high impact, easy)

Clamp all weights to [-3, 3] (or [-5, 5]) after mutation. This prevents saturation and keeps Tanh in its responsive regime. Alternative: add a small weight decay term to fitness (subtract `lambda * sum(w^2)`).

### 3. Targeted layer removal (medium impact)

Instead of picking a random hidden layer to remove, preferentially remove the smallest one. A 1-neuron layer is almost certainly harmful.

### 4. Bottleneck-aware node addition (medium impact)

Before adding a node to a random layer, check if any hidden layer is smaller than its neighbors. Growing a 1-neuron layer to 2 is more impactful than growing a 12-neuron layer to 13.

## Raw Data

**Topology:** `36 -> 12(Tanh) -> 9(Tanh) -> 13(Tanh) -> 1(Tanh) -> 10(Tanh)`
**Total weights:** 725
**Population:** 75 individuals
**Fitness range:** 0.0 to 356,479.6

| Layer | Shape | Weight std | % large (>1.0) | Sum\|w\| per neuron | Saturated? |
|-------|-------|-----------|----------------|---------------------|------------|
| 0 | 36->12 | 1.65 | 56% | 42-53 | All 12 |
| 1 | 12->9 | 1.83 | 52% | 12-22 | All 9 |
| 2 | 9->13 | 1.56 | 53% | 6-16 | All 13 |
| 3 | 13->1 | 1.32 | 46% | 14.5 | 1/1 |
| 4 | 1->10 | 1.15 | 30% | 0.02-2.75 | 1/10 |

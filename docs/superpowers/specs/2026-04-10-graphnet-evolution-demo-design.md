# GraphNetwork Evolution Demo

**Date:** 2026-04-10
**Purpose:** Evolve team skirmish fighters and squad brains using GraphNetwork + NEAT structural mutations, starting from trained MLP net weights. Replaces the positional MLP evolution system with ID-based graph networks that preserve node identity across topology changes.

## Goal

Take the existing `graph_skirmish_demo` (SDL window, team skirmish simulation, timing overlay) and add:
1. Weight-preserving MLP→GraphNetwork conversion of 3 trained nets
2. NEAT structural mutations (add_node, add_connection, mutate_weights)
3. Tournament selection + reproduction across generations
4. Save/load of evolved populations to disk

## Reference Nets (Seeds)

| Net | Snapshot Path | Purpose |
|-----|--------------|---------|
| Fighter | `data/genomes/ArenaFighter/Ace Fighter-1.bin` | Per-ship sensorimotor control |
| Squad Leader | `data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin` | Tactical orders from macro state |
| NTM | `data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin` | Threat scoring per nearby enemy |

These are loaded once, converted to NeuralGenome with trained weights preserved, then used as seed genomes for the initial population.

## Directory Structure

```
dev_ui/
└── data/
    ├── seeds/                — converted seed genomes (NNPK binary)
    │   ├── fighter.nnpk
    │   ├── squad_leader.nnpk
    │   └── ntm.nnpk
    └── evolved/              — auto-saved populations
        └── gen_NNNN/         — one directory per save point
            ├── fighters.nnpk      — best fighter genome
            ├── squad_leader.nnpk  — best squad leader genome
            └── ntm.nnpk           — best NTM genome
```

## Weight-Preserving MLP Conversion

New function: `mlp_snapshot_to_graph_genome_with_weights(const Snapshot& snap) → NeuralGenome`

Same topology as `mlp_snapshot_to_graph_genome()` (dense connections between adjacent layer groups), but instead of random weights, maps MLP weights onto the equivalent graph connections:

- MLP stores weights row-major: `weight[output_node * input_count + input_node]`
- Graph connection `(from_id, to_id)` maps to MLP `weight[to_local * prev_layer_size + from_local]`
- Biases from the MLP are mapped to `NeuralNodeProps.bias` on each non-input node
- Per-node activations from the MLP topology are mapped to `NeuralNodeProps.activation`

The conversion also copies the first snapshot's `ShipDesign` for sensor configuration (needed by the tick function).

After conversion, each seed genome is saved to `dev_ui/data/seeds/` as NNPK via `neuralnet::save(GraphNetwork(genome), stream)`. Subsequent runs load seeds from NNPK instead of re-converting from MLP snapshots.

## Evolution Structure

Two populations per team, matching the existing split:

**Fighter population:** Vector of NeuralGenome, one per fighter slot per team. Each fighter is scored individually (kills, survival, base damage).

**Squad population:** Vector of paired `{NeuralGenome squad_leader, NeuralGenome ntm}`. One pair per squad per team. Scored by the sum of their fighters' scores.

### Per-Generation Loop

1. **Build nets:** For each team, construct GraphNetwork instances from current population genomes.
2. **Run matches:** Same match format as current demo (round-robin or single match depending on config). Use `graph_tick_team_arena_match()` for the featured match, headless for background matches.
3. **Score:**
   - Fighter: per-ship kill count + survival bonus - death penalty
   - Squad brain: sum of their squad's fighter scores
4. **Select + reproduce:**
   - Sort by fitness
   - Top N survive as elites (unchanged genomes)
   - Fill remaining slots via tournament selection (size 3-5)
   - Clone winner genome, apply NEAT mutations
5. **Auto-save:** Every 50 generations, save best genomes to `dev_ui/data/evolved/gen_NNNN/`

### Mutations (NEAT operators, no speciation)

Applied to each non-elite genome after reproduction:

| Mutation | Rate | Source |
|----------|------|--------|
| Weight perturbation | 80% of connections | `evolve::mutate_weights()` |
| Add connection | 5% chance per genome | `evolve::add_connection()` |
| Add node | 3% chance per genome | `evolve::add_node()` |
| Disable connection | 1% chance per genome | `evolve::disable_connection()` |
| Bias perturbation | 40% of nodes | `neuralnet::mutate_biases()` |
| Activation mutation | 5% of nodes | `neuralnet::mutate_activations()` |

Rates are configurable. We use NEAT mutation operators directly (not through NeatPopulation) and do tournament selection manually. This gives structural mutations without speciation complexity. Speciation can be added later.

An `InnovationCounter` is maintained across the entire run to assign unique innovation numbers to new connections/nodes. This is required by `add_connection()` and `add_node()`.

## Save/Load

**Genome persistence:** `neuralnet::save(GraphNetwork(genome), stream)` writes NNPK binary. `neuralnet::load(stream)` returns `variant<Network, GraphNetwork>`, extract with `std::get<GraphNetwork>(loaded).genome()`.

**Population persistence:** Save the best genome of each type (fighter, squad leader, NTM) per generation checkpoint. On startup, if evolved genomes exist, load them as seeds instead of the MLP conversions.

## Demo Executable Changes

The `graph_skirmish_demo` binary gains:

1. **Seed conversion on first run:** Load MLP snapshots → convert with weights → save to `dev_ui/data/seeds/`
2. **Evolution loop:** After each match ends, score and evolve instead of just resetting with random nets
3. **ImGui overlay additions:**
   - Generation counter
   - Best/avg fighter fitness
   - Best/avg squad fitness
   - Population size
   - Current mutation rates
4. **Keyboard:**
   - S: force-save current best genomes
   - Existing: 1-4 speed, Escape quit
5. **Auto-save:** Every 50 generations to `dev_ui/data/evolved/gen_NNNN/`
6. **Resume:** On startup, check for existing evolved genomes and resume from them

## Config

Hardcoded constants at the top of the demo (same pattern as current stress-test knobs):

```cpp
static constexpr std::size_t FIGHTER_POP_SIZE = 20;
static constexpr std::size_t SQUAD_POP_SIZE = 10;
static constexpr std::size_t ELITE_COUNT = 3;
static constexpr std::size_t TOURNAMENT_SIZE = 3;
static constexpr int SAVE_INTERVAL = 50;  // generations
```

## What's NOT In Scope

- No net viewer / brain visualization
- No config UI / pause screen
- No hangar / genome management
- No scroller mode
- No old UI migration
- No NeatPopulation / speciation (just direct mutation operators)
- No multi-match-per-generation tournament (one match per generation for now)

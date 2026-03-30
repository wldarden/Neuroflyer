# Squad Leader Net Design

**Date:** 2026-03-30
**Status:** Approved
**Amends:** [Arena Perception Design](2026-03-28-arena-perception-design.md) — replaces the simple squad broadcast net (Phase 1) with a structured squad leader net that makes discrete tactical decisions via 1-hot output groups and Near Threat Matrices.

## Overview

The original Phase 1 squad broadcast net was a minimal 8-input/4-output network that emitted opaque float signals to fighters. This design replaces it with a **squad leader net** — a net that makes interpretable tactical decisions (attack/defend, expand/contract) using a **Near Threat Matrix (NTM)** sub-net pattern for variable-count local threat processing.

The squad leader's discrete orders are interpreted by the game engine into structured fighter inputs (target heading, aggression, spacing), keeping the fighter net focused on local tactics while the squad leader handles group strategy.

## Key Concepts

### Squad Center

A common reference point — the geometric center of all alive ships in the squad. Not a net input itself, but used to compute most squad leader inputs and NTM proximity checks.

```
squad_center = mean(alive_ship_positions)
```

### 1-Hot Output Groups

Instead of continuous broadcast signals, the squad leader outputs discrete orders via 1-hot groups. Within each group, the node with the highest activation is set to 1.0 and all others to 0.0 (argmax). This forces the net to make a clear decision rather than blending signals.

### Near Threat Matrix (NTM) Pattern

Reuses the "duplicate sub-net, select top-1" pattern from the enemy analysis net design. A small fixed-topology sub-net is instantiated once per nearby threat. Each copy evaluates one threat and outputs a `threat_score`. The copy with the highest score becomes the "active" threat whose outputs feed into the squad leader net.

This lets the squad leader process a variable number of local threats through a fixed-size input interface.

---

## Sector Grid (Spatial Indexing)

### Purpose

Efficiently determine which enemies are "near" a squad without computing distances to every entity in the arena.

### Structure

The arena world is divided into a grid of **2000x2000 pixel sectors**. Each sector maintains a list of entity IDs (ships, bases) currently occupying it. Entities update their sector assignment when they move.

For the default arena size (81920 x 51200):
- Grid dimensions: ~41 x 26 sectors
- Sectors are labeled by column/row index (e.g., sector [20, 13])

### Near Radius

"Near" is defined as **Manhattan distance <= 2 sectors** from the squad center's sector. This produces a diamond-shaped search area of 13 sectors:

```
            [r-2, c]
       [r-1,c-1] [r-1,c] [r-1,c+1]
  [r,c-2] [r,c-1] [r, c] [r,c+1] [r,c+2]
       [r+1,c-1] [r+1,c] [r+1,c+1]
            [r+2, c]
```

### NTM Generation

Each tick, for each squad:
1. Compute squad center sector
2. Gather all entities in the 13-sector diamond
3. Filter to enemies (ships + bases, not own team)
4. For each enemy entity: instantiate (or reuse) an NTM sub-net instance
5. Run all NTMs, select top-1 by threat_score

When an enemy leaves the diamond, its NTM is discarded. When a new enemy enters, a new NTM is created.

### Config

```cpp
float sector_size = 2000.0f;        // pixels per sector edge
int ntm_sector_radius = 2;          // Manhattan distance for "near"
```

Both values are tunable via ArenaConfig.

---

## Near Threat Matrix (NTM) Sub-Net

A small fixed-topology network, instantiated per nearby enemy entity. All copies share the same weights (same Individual in the genome). Different inputs per threat produce different outputs.

### Inputs (6)

| # | Input | Description |
|---|-------|-------------|
| 1 | heading | Direction from squad center to this threat (radians, normalized) |
| 2 | distance | Distance from squad center to this threat (normalized to world diagonal) |
| 3 | enemy_health | HP of this entity (normalized 0-1; ships = alive ? 1.0 : 0.0; bases = hp/max_hp) |
| 4 | squad_health | This squad's alive fraction (same for all NTMs in a squad — provides context for threat evaluation) |
| 5 | is_ship | 1.0 if this threat is a ship, 0.0 otherwise |
| 6 | is_starbase | 1.0 if this threat is a starbase, 0.0 otherwise |

### Outputs (1 evolved + 2 app-calculated)

| # | Output | Description |
|---|--------|-------------|
| 1 | threat_score | Evolved output. Used to select which NTM is "active" (top-1). Also fed to squad leader as an input. |
| 2 | target_heading | **App-calculated** (not a net output). Direction from squad center to this threat entity. Fed to squad leader when this NTM is active. |
| 3 | target_distance | **App-calculated** (not a net output). Distance from squad center to this threat entity, normalized. Fed to squad leader when this NTM is active. |

The NTM sub-net itself only has **1 actual output node** (threat_score). The heading and distance are computed by the game engine and associated with whichever NTM wins the threat_score competition. This keeps the sub-net tiny and fast.

### Topology

```
Inputs: 6 → Hidden: [4] → Outputs: 1 (threat_score)
```

Fixed topology (not evolvable in v1). ~29 weights (6x4 + 4 biases + 4x1 + 1 bias).

### No-Threat Case

When no enemies are within the NTM sector diamond:
- `active_threat` input to squad leader = 0.0
- `threat_heading` = 0.0
- `threat_distance` = 0.0
- `threat_score` = 0.0

The squad leader net can learn to recognize `active_threat = 0` and ignore the other three values.

---

## Squad Leader Net

### Inputs (11)

| # | Input | Source | Description |
|---|-------|--------|-------------|
| 1 | squad_health | App-calculated | Fraction of squad ships alive (0.0 - 1.0) |
| 2 | home_distance | App-calculated | Distance from squad center to home starbase, normalized to world diagonal |
| 3 | home_heading | App-calculated | Direction from squad center to home starbase (radians, normalized) |
| 4 | home_health | App-calculated | Own starbase HP, normalized (0.0 - 1.0) |
| 5 | squad_spacing | App-calculated | Measure of squad spread. Standard deviation of ship distances from squad center, normalized. Low = bunched up, high = spread out. |
| 6 | commander_target_heading | App-calculated / Commander | Direction from squad center to the commander's designated target. **Phase 1: always points at enemy starbase.** Phase 2+: set by commander net. |
| 7 | commander_target_distance | App-calculated / Commander | Distance from squad center to the commander's designated target, normalized. **Phase 1: always distance to enemy starbase.** |
| 8 | active_threat | App-calculated | 1.0 if an NTM is active (enemies nearby), 0.0 if no enemies in sensor range |
| 9 | threat_heading | Active NTM | App-calculated heading from squad center to the active threat entity. 0.0 if no active threat. |
| 10 | threat_distance | Active NTM | App-calculated distance from squad center to the active threat entity, normalized. 0.0 if no active threat. |
| 11 | threat_score | Active NTM | The evolved threat_score output from the active NTM. 0.0 if no active threat. Tells the squad leader *how* threatening the threat is. |

### Outputs (5, in two 1-hot groups)

**Group 1: Spacing Order (2 nodes)**

| Node | Order | Meaning |
|------|-------|---------|
| 0 | expand | Squad should spread out |
| 1 | contract | Squad should tighten formation |

Argmax within group: winning node set to 1.0, other set to 0.0.

**Group 2: Tactical Order (3 nodes)**

| Node | Order | Target entity | Aggression |
|------|-------|---------------|------------|
| 0 | attack_starbase | Enemy starbase | +1 (attack) |
| 1 | attack_ship | Active NTM threat entity | +1 (attack) |
| 2 | defend_home | Own starbase | -1 (defend) |

Argmax within group: winning node set to 1.0, others set to 0.0.

### Target Mapping (Game Engine Interprets)

Based on the active tactical order, the game engine computes the target for each fighter:

| Active Order | `squad_target` points to... |
|--------------|----------------------------|
| attack_starbase | Enemy starbase position |
| attack_ship | Active NTM threat entity position |
| defend_home | Own starbase position |

If `attack_ship` is active but no NTM is active (`active_threat = 0`), fall back to `attack_starbase`.

### Topology

```
Inputs: 11 → Hidden: [8] → Outputs: 5 (2 spacing + 3 tactical)
```

Fixed topology (not evolvable in v1). ~133 weights (11x8 + 8 biases + 8x5 + 5 biases).

### Execution Frequency

The squad leader net runs **once per tick per squad** (not per fighter). All fighters in the squad receive the same orders for that tick.

---

## Fighter Input Extensions

Fighters receive 6 new inputs derived from the squad leader's orders. These are appended to the existing arena fighter input vector (after nav inputs, before broadcast signals/memory).

| # | Input | Source | Description |
|---|-------|--------|-------------|
| 1 | squad_target_heading | App-calculated | Direction from this fighter to the squad leader's selected target (per tactical order). Normalized radians. |
| 2 | squad_target_distance | App-calculated | Distance from this fighter to the squad leader's selected target, normalized to world diagonal. |
| 3 | squad_center_heading | App-calculated | Direction from this fighter to the squad center. |
| 4 | squad_center_distance | App-calculated | Distance from this fighter to the squad center, normalized. |
| 5 | aggression | Order-derived | +1.0 when tactical order is attack (attack_starbase or attack_ship), -1.0 when order is defend (defend_home). |
| 6 | spacing | Order-derived | +1.0 when spacing order is "expand", -1.0 when order is "contract". |

### Updated Fighter Input Layout

```
[sensor values...]
[position/rotation (3)]
[nav inputs (7): target dir/range, home dir/range, own base HP]
[squad leader inputs (6): squad_target heading/dist, squad_center heading/dist, aggression, spacing]
[broadcast signals from squad leader (existing 4 opaque floats — REMOVED, replaced by the 6 structured inputs above)]
[recurrent memory]
```

**Note:** The 4 opaque broadcast signals from the old squad broadcast net are **replaced** by the 6 structured squad leader inputs. The squad leader's outputs are no longer opaque — they're interpreted by the engine into meaningful fighter inputs. Net increase: +2 inputs over the old layout.

---

## Updated Arena Input Size

Previous arena fighter input:
```
sensors + 3 (position) + 7 (nav) + 4 (broadcast) + memory = sensors + 14 + memory
```

New arena fighter input:
```
sensors + 3 (position) + 7 (nav) + 6 (squad leader) + memory = sensors + 16 + memory
```

The `compute_arena_input_size()` function and `build_arena_ship_input()` must be updated to reflect this change.

---

## Data Flow (Per Tick)

```
1. Compute squad centers for each squad (geometric mean of alive ships)

2. For each squad:
   a. Determine squad center sector
   b. Gather enemies in 13-sector diamond around squad center
   c. For each nearby enemy:
      - Build NTM input (heading, distance, health, squad_health, is_ship, is_starbase)
      - Run NTM sub-net → threat_score
   d. Select top-1 NTM by threat_score (or mark no active threat)
   e. Build squad leader input (11 values)
   f. Run squad leader net → 5 outputs
   g. Apply argmax to each 1-hot group
   h. Determine tactical order + spacing order

3. For each alive fighter:
   a. Determine target entity from squad leader's tactical order
   b. Compute squad_target heading/distance from this fighter to target
   c. Compute squad_center heading/distance from this fighter to squad center
   d. Set aggression and spacing from orders
   e. Build full fighter input: [sensors] [position] [nav] [squad leader inputs] [memory]
   f. Run fighter net → actions + memory writeback
   g. Apply actions to ship
```

---

## Genome Structure

One team genome now encodes **three** sets of weights (up from two):

| Section | Net | Weights (approx) | Topology |
|---------|-----|-------------------|----------|
| 1 | NTM sub-net | ~29 | 6 → [4] → 1, fixed |
| 2 | Squad leader net | ~133 | 11 → [8] → 5, fixed |
| 3 | Fighter net | variable | Sensor-dependent, evolvable topology |

**Evolution:**
- NTM weights mutate (Gaussian noise, same rates)
- Squad leader weights mutate
- Fighter weights mutate + topology mutations (existing)
- All three sections are part of the same `TeamIndividual` and evolve together

**Squad-only training mode:**
- Fighter weights are frozen (loaded from a saved fighter variant)
- NTM + squad leader weights evolve
- This lets users train squad leaders against a known fighter behavior

---

## Training Scenarios

Each scenario trains the squad leader (+ NTM) with a frozen fighter variant.

### Squad vs Squad (Primary)

Two teams, each with 1 squad of 8 fighters and a starbase. Symmetric 1v1. Both sides have squad leaders.

**Fitness:**
- Enemy base damage dealt (primary)
- Own base survival
- Ships alive at round end

**What it trains:** Full tactical decision-making. Attack vs defend tradeoffs. Threat assessment. Spacing management under fire.

### Base Attack

One squad of 8 fighters attacking an undefended starbase. No enemy fighters.

**Fitness:**
- Time to destroy base (faster = better)
- Ships alive at round end

**What it trains:** Navigation, focus fire, formation management. Baseline coordination before introducing combat.

### Base Defense (Future)

One squad defending a starbase against waves of simple AI attackers (or another evolving squad).

**What it trains:** Defensive positioning, threat prioritization, formation around the base.

**Deferred** — requires AI attackers or a more complex scenario setup.

---

## Config Additions

New fields on `ArenaConfig`:

```cpp
// Sector grid
float sector_size = 2000.0f;
int ntm_sector_radius = 2;

// NTM sub-net topology
std::size_t ntm_input_size = 6;
std::vector<std::size_t> ntm_hidden_sizes = {4};
std::size_t ntm_output_size = 1;  // just threat_score

// Squad leader topology
std::size_t squad_leader_input_size = 11;
std::vector<std::size_t> squad_leader_hidden_sizes = {8};
std::size_t squad_leader_output_size = 5;  // 2 spacing + 3 tactical

// Squad leader fighter inputs (replaces squad_broadcast_signals)
std::size_t squad_leader_fighter_inputs = 6;  // heading, dist, center heading, center dist, aggression, spacing
```

---

## Changes from Previous Design

| Aspect | Old (Perception Phase 1) | New (Squad Leader) |
|--------|--------------------------|---------------------|
| Squad net outputs | 4 opaque broadcast floats | 5 nodes in 2 one-hot groups (interpretable orders) |
| Squad net inputs | 8 (basic squad stats) | 11 (squad stats + NTM threat data) |
| Threat awareness | None (squad net had no local threat info) | NTM sub-net per nearby enemy, top-1 selection |
| Fighter receives | 4 opaque floats | 6 structured values (target, center, aggression, spacing) |
| Spatial indexing | None | Sector grid (2000x2000) with Manhattan distance diamond |
| Genome sections | 2 (squad broadcast + fighter) | 3 (NTM + squad leader + fighter) |
| Game engine role | Passes signals through | Interprets orders → computes target geometry for fighters |

---

## Integration with Four-Net North Star

This design replaces the simple squad broadcast with a much richer squad leader. It fits into the four-net architecture as follows:

| Net | Status |
|-----|--------|
| Enemy Analysis Net | Deferred (Phase 3). Squad leader's NTM handles local threats. Enemy analysis handles cross-map strategic threats. |
| Commander Net | Deferred (Phase 2). Squad leader inputs 6-7 are placeholders ("commander target heading/distance") that point at enemy starbase. Commander will set these. |
| **Squad Leader Net** | **This design. Replaces squad broadcast net.** |
| Fighter Net | Extended with 6 structured inputs from squad leader orders (replaces 4 opaque broadcasts). |

When the commander net is added (Phase 2), it will provide the `commander_target_heading` and `commander_target_distance` inputs to the squad leader, replacing the current hardcoded "always enemy starbase" values. The squad leader's interface doesn't change — it just gets better strategic input.

---

## Implementation Scope

**In scope (implement now):**
- Sector grid data structure (entity → sector mapping, diamond neighbor query)
- NTM sub-net config, creation, execution
- Squad leader net config, creation, execution (replaces squad broadcast)
- 1-hot argmax on squad leader outputs
- Game engine order interpretation (target mapping, aggression, spacing)
- Fighter input extension (6 new inputs replacing 4 old broadcasts)
- Updated `TeamIndividual` with 3 net sections (NTM + squad leader + fighter)
- Updated `build_arena_ship_input()` for new layout
- Updated `run_arena_match()` for new tick loop
- `squad_spacing` calculation (stddev of ship-to-center distances)
- Training scenario support (squad vs squad, base attack)
- Updated arena game screen UI for new tick flow

**Deferred:**
- Commander net (Phase 2) — squad leader inputs 6-7 hardcoded for now
- Base defense scenario
- NTM topology evolution
- Squad leader topology evolution
- Multiple squads per team
- Visual debugging of squad leader decisions (order display, threat indicators)

# Fighter Drill Mode — Design Spec

**Date:** 2026-03-30
**Status:** Draft

## Overview

A training mode that evolves fighter nets to obey squad leader commands. Instead of a real evolved squad net, the system injects scripted squad inputs through three timed phases (expand, contract, attack target). 200 individual fighters are scored on how well they follow commands, then evolved using the same individual-based evolution as the scroller.

The goal is to produce fighter variants that reliably respond to the 6 squad leader input channels, so they perform well when later paired with a real evolved squad leader in arena mode.

## Architecture

**Approach:** New dedicated `FighterDrillSession` (engine) + `FighterDrillScreen` (UI), separate from ArenaGameScreen/ArenaSession. Reuses arena movement physics (rotation + thrust), arena sensors, and individual-based evolution. Collision and movement math duplicated from ArenaSession rather than extracting a shared module — keeps scope tight.

### New Files

| Type | Header | Source |
|------|--------|--------|
| Engine | `include/neuroflyer/fighter_drill_session.h` | `src/engine/fighter_drill_session.cpp` |
| Screen | `include/neuroflyer/ui/screens/fighter_drill_screen.h` | `src/ui/screens/game/fighter_drill_screen.cpp` |
| Test | — | `tests/fighter_drill_session_test.cpp` |

### FighterDrillSession (Engine)

Pure logic class, zero SDL/ImGui dependencies.

**Responsibilities:**
- Manages the drill world: ships (200 `Triangle`s), towers, tokens, one enemy starbase, bullets
- Tracks current drill phase and tick count
- Arena-style movement: left/right rotate, up/down thrust (not strafe)
- Collision: ships vs towers (death), ships vs tokens (collect), bullets vs starbase (damage), bullets vs towers (destroy)
- World wrapping enabled (toroidal)
- Per-tick movement scoring (see Scoring section)
- Exposes `get_scores()` for evolution at round end

**State:**
```
ships_[200]           — Triangle (position, rotation, velocity, alive)
towers_[]             — static obstacles
tokens_[]             — collectibles
bullets_[]            — pooled from all ships
starbase_             — single enemy Base
squad_center_         — fixed position (world center), does not move
phase_                — current DrillPhase enum (Expand, Contract, Attack)
phase_tick_           — ticks elapsed in current phase
scores_[200]          — accumulated fitness per fighter
alive_[200]           — alive/dead state
tokens_collected_[200] — count per fighter
```

**No squad net, no teams, no NTM.** Just ships + scripted inputs + phase timer.

### FighterDrillScreen (UI)

UIScreen subclass driving the training loop.

**State:**
```
population_[200]       — Individual (not TeamIndividual)
fighter_nets_[200]     — compiled networks
recurrent_states_[200] — per-fighter recurrent memory
drill_session_         — FighterDrillSession instance
generation_            — current generation counter
ship_design_           — ShipDesign from source variant
speed_multiplier_      — 1x/5x/20x/100x
view_mode_             — SWARM/BEST/WORST (Tab cycles)
```

**Per frame:**
1. Compute scripted squad inputs for current phase (see Phase Inputs table)
2. For each alive fighter:
   - Compute per-fighter squad inputs (squad_center_heading/distance depend on fighter position)
   - Build arena sensor input via `build_arena_ship_input()`
   - Forward fighter net
   - Decode output → ship actions
3. Tick drill session
4. Render world (ships, towers, tokens, starbase, bullets, phase HUD)

**Per generation:**
1. Collect scores from `drill_session_.get_scores()`
2. Evolve population via `evolve_population()` (individual-based, same as scroller)
3. Rebuild nets, reset recurrent states
4. Reset drill session (re-spawn ships at squad center, new tower/token/starbase positions)

**Controls:**
- Tab: cycle view (SWARM/BEST/WORST)
- Space: push pause screen
- 1-4: speed (1x/5x/20x/100x)
- Escape: back to hangar

## Drill Phases

Three sequential phases, 20 seconds each (1200 ticks at 60 ticks/sec), 60 seconds total per generation.

### Phase 1: Expand (0–20s)

Squad command says "spread out." Fighters score by moving away from squad center.

### Phase 2: Contract (20–40s)

Squad command says "regroup." Fighters score by moving toward squad center.

### Phase 3: Attack Target (40–60s)

Squad command says "attack the starbase." Fighters score by moving toward it and shooting it.

### Scripted Squad Inputs Per Phase

These 6 values are injected into the fighter's input vector in the same slots as real squad leader orders:

| Input | Expand | Contract | Attack |
|-------|--------|----------|--------|
| `squad_target_heading` | 0.0 | 0.0 | heading from fighter to starbase |
| `squad_target_distance` | 0.0 | 0.0 | normalized distance to starbase |
| `squad_center_heading` | *computed per-fighter* | *computed per-fighter* | *computed per-fighter* |
| `squad_center_distance` | *computed per-fighter* | *computed per-fighter* | *computed per-fighter* |
| `aggression` | 0.0 | 0.0 | +1.0 |
| `spacing` | +1.0 | -1.0 | 0.0 |

`squad_center_heading` and `squad_center_distance` are always computed from the fighter's current position to the fixed squad center point. `squad_target_heading` and `squad_target_distance` are computed from the fighter's position to the starbase (only in attack phase).

**Implementation:** Construct a `SquadLeaderOrder` per phase (setting `spacing`, `tactical`, and `target_x/y` fields), then pass it through the existing `compute_squad_leader_fighter_inputs()` function. This guarantees heading/distance values are computed identically to real squad leader mode — same normalization, same rotation-relative encoding.

## Scoring

All scoring is **movement-based** (per-tick velocity dot product), not absolute-position-based. This prevents a fighter's spawn position or drift from unfairly affecting fitness.

### Per-Tick Scoring

**Expand phase:**
```
direction = normalize(fighter_pos - squad_center)
score_delta = dot(fighter_velocity, direction) * expand_weight
```
Positive when moving away from center, negative when moving toward it.

**Contract phase:**
```
direction = normalize(squad_center - fighter_pos)
score_delta = dot(fighter_velocity, direction) * contract_weight
```
Positive when moving toward center, negative when moving away.

**Attack phase:**
```
direction = normalize(starbase_pos - fighter_pos)
travel_score = dot(fighter_velocity, direction) * attack_travel_weight
hit_score = bullet_hits_this_tick * attack_hit_bonus
score_delta = travel_score + hit_score
```

**Always active (all phases):**
- Token collection: `+token_bonus` per token collected
- Tower collision: ship dies, no further scoring

### Default Scoring Weights

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `expand_weight` | 1.0 | Per-tick expand movement score multiplier |
| `contract_weight` | 1.0 | Per-tick contract movement score multiplier |
| `attack_travel_weight` | 1.0 | Per-tick attack approach score multiplier |
| `attack_hit_bonus` | 500.0 | Points per bullet hit on starbase |
| `token_bonus` | 50.0 | Points per token collected |

## World Setup

| Parameter | Value |
|-----------|-------|
| World size | 4000 x 4000 |
| World wrapping | Enabled (toroidal) |
| Squad center | Fixed at (2000, 2000), does not move |
| Fighter spawn | All 200 at squad center, random facing directions |
| Starbase | 1500 units from squad center, random direction (new each generation) |
| Towers | ~50, random positions |
| Tokens | ~30, random positions |
| Population | 200 individual fighters |

## Evolution

Individual-based evolution, identical to scroller mode:

- **Elitism:** Top 10 survive unchanged
- **Selection:** Tournament (size 5)
- **Crossover:** Uniform, same-topology only
- **Weight mutation:** 10% rate, Gaussian noise (0.3 std)
- **Topology mutations:** Add/remove nodes and layers (same configurable rates)

Uses `evolve_population()` directly — no team wrappers.

## Entry Point

From the hangar fighter tab, user selects a variant and clicks "Fighter Drill."

1. Load the selected variant's snapshot
2. If the variant is not already a fighter net, convert via `convert_variant_to_fighter()` (adds arena sensor extras + squad leader input slots)
3. Seed 200 copies with mutation spread from the converted design
4. Push `FighterDrillScreen`

## Pause Screen

Space pushes a pause screen (reusing the existing pause screen pattern). Tabs:

- **Evolution:** Mutation rates, topology mutation config (same as scroller pause screen)
- **Save Variants:** Multi-select from population, save as named fighter variants to the source genome directory

Saved variants use the same snapshot format and lineage tracking as existing fighter variants.

## Rendering

**Camera modes (Tab to cycle):**
- SWARM: zoom to fit swarm bounding box
- BEST: follow highest-scoring alive fighter
- WORST: follow lowest-scoring alive fighter

**Phase HUD overlay:**
- Current phase name and color (Expand=blue, Contract=orange, Attack=red)
- Time remaining in phase (countdown)
- Generation counter
- Best/average fitness

**Speed controls:** 1-4 keys for 1x/5x/20x/100x. At high speeds, skip rendering.

## Future Extensions (Not In Scope)

- Custom phase sequences (user-defined drill programs)
- Mixed drills (expand + dodge, contract + shoot)
- Moving squad center
- Multiple starbases
- Extracting shared physics module from ArenaSession + FighterDrillSession

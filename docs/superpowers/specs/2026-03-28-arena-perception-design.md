# Arena Perception & Hierarchical Brain Design

**Date:** 2026-03-28
**Status:** Approved
**Amends:** [Arena Mode Design](2026-03-28-arena-mode-design.md) — fills the deferred perception gap and adds bases, hierarchical nets, and team-level evolution.

## Overview

The existing arena spec creates a large 2D shared world where ships battle in teams, but defers inter-ship perception. This design solves the perception problem via a hierarchical four-net brain architecture: ships don't need to see far because a commander layer handles macro awareness and broadcasts strategic signals down to tactical fighters.

Additionally, this design adds **bases** as team objectives — each team has a home structure to defend and enemy structures to destroy — creating the attack/defend dynamic that drives emergent role specialization.

## Full Vision (North Star)

The complete architecture has four neural networks per team, encoded in a single genome:

1. **Enemy Analysis Net** — shared weights, duplicated per enemy team. Evaluates threats and selects strategic modes.
2. **Commander Net** — one per team, recurrent. Temporal anchor that sees macro state and outputs strategic signals.
3. **Squad Broadcast Net** — shared weights, duplicated per squad. Translates commander state into squad-specific signals.
4. **Fighter Net** — shared weights, duplicated per ship. Local sensors + navigational inputs + squad broadcast signals.

The duplication pattern handles variable counts at runtime: N enemies, K squads, M fighters per squad. Architecture is fixed; counts are config.

### Phased Implementation

The full four-net architecture is the goal, but it will be implemented incrementally to validate each layer before adding the next:

- **Phase 1 (implement now):** Squad broadcast net + fighters + bases. Single squad of 8 fighters. Prove coordination works.
- **Phase 2:** Add commander net. Multiple squads per team. Prove commander layer adds strategic value.
- **Phase 3:** Add enemy analysis net. Multi-team matches with variable enemy count. Full team-level evolution.

Each phase validates the layer below before adding complexity on top.

---

## Bases

Each team has one base — the central objective.

- **Structure:** Single large circle with HP
- **HP:** Configurable (default 1000)
- **Radius:** Configurable (default 100px)
- **Damage:** Ships damage enemy bases by shooting them (bullet-circle collision, same as towers)
- **Position:** Center of each team's spawn pie slice (as defined in the existing arena spec)
- **Base does not fight back** — no turrets in v1
- **Round end conditions:** Time limit OR all but one base destroyed (amends existing spec which used "last team standing")

Bases are static entities — they don't move, rotate, or regenerate HP. They're collision targets for bullets only (ships don't take damage from touching a base).

---

## The Four Nets (Full Architecture)

### Enemy Analysis Net

Shared-weight network, instantiated once per enemy team. Same weights, different inputs per enemy.

**Inputs (~8):**
- Enemy base HP (normalized 0-1)
- Enemy ships alive (fraction of starting count)
- Enemy base direction from own base (sin/cos pair)
- Enemy base range from own base (normalized to world diagonal)
- Enemy average distance to our base (normalized)
- Enemy cluster centroid direction from own base (sin/cos pair)

**Outputs (~5):**
- `threat_score` — gates which enemy's outputs flow to commander (top-1 selection)
- `defend_home` — mode signal
- `attack_base` — mode signal
- `attack_squad` — mode signal
- 1-2 analysis signals passed through to commander

**Top-1 selection:** After running all N copies, the enemy with the highest `threat_score` is the "active" enemy. Its mode signals and analysis signals are used; all other copies' outputs are discarded.

**Mode selection (game engine):** The engine reads which of `defend_home`, `attack_base`, `attack_squad` is strongest from the active enemy's outputs, and computes the appropriate navigational target for each fighter:

| Strongest mode | Fighter gets dir+range to... |
|----------------|------------------------------|
| `defend_home` | Own base |
| `attack_base` | Active enemy's base |
| `attack_squad` | Active enemy's ship centroid |

The nets learn *when* to activate each mode. The engine handles geometry. Nets never learn coordinate spaces.

**Deferred to Phase 3.**

### Commander Net

Single network per team. Recurrent — maintains internal state across ticks for temporal continuity. When the top-threat enemy changes, the commander's memory smooths the strategic transition.

**Inputs (~50):**
- Own base HP (normalized)
- Team ships alive (fraction)
- Round time remaining (normalized)
- Sector grid: 4x4 grid covering the arena, 2 values per cell (ally count, enemy count) = 32 inputs
- Top-threat enemy analysis signals (from enemy analysis net)
- Per-squad stats: alive %, average distance to home base (per squad)
- Recurrent memory slots

**Outputs (~8-12):**
- Commander state signals (fed to all squad broadcast nets)
- Recurrent memory writeback

**Key property:** The recurrent memory means strategy persists even when tactical situations change rapidly. The commander is the team's long-term memory.

**Deferred to Phase 2.**

### Squad Broadcast Net

Shared-weight network, instantiated once per squad. Same weights, different squad stats produce different broadcast signals — this is the mechanism for squad differentiation and emergent role specialization.

**Inputs (~10):**
- Commander state signals (from commander net; in Phase 1 this is omitted)
- This squad's alive fraction
- This squad's average distance to mode target (normalized)
- This squad's average distance to home base (normalized)
- This squad's centroid direction relative to home base (sin/cos pair)

**Outputs (~4-6):**
- Broadcast signals delivered to every fighter in this squad
- These are opaque floats — evolution assigns meaning

**Duplication:** Same weights, different per-squad stats → different signals per squad. A squad near the base naturally gets different broadcasts than a squad deep in enemy territory, even though the net weights are identical.

**Implemented in Phase 1** (without commander inputs — squad net sees squad stats directly).

### Fighter Net

Shared-weight network, instantiated once per alive ship. Structurally the same as the existing scroller fighter net, with additional input slots.

**Inputs (three groups):**

*Local tactical (existing + bullet):*
- Ray/Occulus sensor readings: distance, is_tower, is_token, token_value per full sensor
- `is_friend` flag: 1.0 = ally, -1.0 = enemy, 0.0 = non-ship entity (already exists)
- **`is_bullet` (new):** 1.0 if sensor hit a bullet, 0.0 otherwise. Added to full sensor output.
- Position (normalized to world dimensions)
- Velocity (magnitude and direction)
- Recurrent memory slots

*Strategic navigational (~5):*
- Direction + range to **mode target** (computed by game engine from active mode; in Phase 1, always points at enemy base)
- Direction + range to **home base**
- Own base HP (normalized)

*Commander signals (~4-6):*
- Squad broadcast signals (opaque floats from squad broadcast net)

**Outputs:**
- Rotate CW / CCW
- Thrust forward / reverse
- Shoot
- Recurrent memory writeback

**Implemented in Phase 1** with strategic nav + squad broadcasts.

---

## Evolution Model

### Genome

One genome encodes one complete team brain — all four nets' weights in a single `StructuredGenome`. The genome has four weight sections:

1. Enemy analysis net weights + biases
2. Commander net weights + biases
3. Squad broadcast net weights + biases
4. Fighter net weights + biases

Sensor genes and topology evolution apply to the fighter net. Commander, squad, and enemy analysis nets have fixed topology in v1 (topology evolution for these nets is deferred).

### Fitness

Team-level fitness. After a round, the team genome receives a single score:

- Base survival time (ticks your base survived / max ticks)
- Enemy base damage dealt (HP removed / enemy starting HP)
- Ships alive at round end (fraction)
- Tokens collected (count, normalized)
- Weights for each component are tunable via config

### Selection & Reproduction

- **Population:** P team genomes (e.g., 20)
- **Match format:** Small group matches. Randomly divide P genomes into groups of G (e.g., 4 teams per arena). Run `rounds_per_generation` rounds with reshuffled groups. Accumulate fitness.
- **Selection:** Tournament selection (same as existing pipeline)
- **Crossover:** Uniform crossover on same-topology genomes. Crossover operates within each weight section independently (fighter weights cross with fighter weights, commander with commander, etc.)
- **Mutation:** Weight mutation (Gaussian noise, same rates as existing). Topology mutation on fighter net only (v1).
- **Elitism:** Top N team genomes survive unchanged

### Squad Assignment

Fixed for the duration of a round. Fighters are assigned to squads at spawn time (evenly divided). Assignment does not change mid-round.

---

## Phase 1: Squad Mode (Implement Now)

Phase 1 validates the smallest meaningful unit: can a squad broadcast net coordinate 8 fighters toward a shared objective?

### What's included

- **Bases** — one per team, HP, collision with bullets
- **Squad broadcast net** — one instance (no commander, no enemy analysis)
- **Fighter net** — existing sensors + bullet detection + strategic nav inputs + squad broadcast inputs
- **Team-level evolution** — one genome encodes squad net + fighter net weights
- **Two training scenarios:**
  1. Squad vs undefended base — 8 fighters try to destroy a stationary base. No enemy fighters. Validates basic coordination and navigation.
  2. Squad vs squad — two teams of 8 fighters, each with a base. Symmetric 1v1. Validates competitive coordination.

### What's omitted (deferred)

- Commander net (Phase 2)
- Enemy analysis net (Phase 3)
- Multiple squads per team (Phase 2)
- Multi-team matches beyond 1v1 (Phase 3)
- Sector grid inputs (Phase 2 — commander needs these, fighters don't)
- Mode selection from enemy analysis (Phase 1 uses fixed mode: always "attack enemy base")

### Phase 1 squad net inputs

Without a commander, the squad net in Phase 1 sees only its own stats:
- Squad alive fraction
- Squad average distance to enemy base (normalized)
- Squad average distance to home base (normalized)
- Squad centroid direction relative to home base (sin/cos pair)
- Own base HP (normalized)
- Enemy base HP (normalized)

### Phase 1 fighter navigational inputs

Without mode selection, fighters in Phase 1 always get:
- Direction + range to enemy base (fixed target)
- Direction + range to home base
- Own base HP

### Phase 1 fitness

- Enemy base damage dealt (primary signal — did you attack effectively?)
- Base survival time (secondary — did you also protect your own base in squad vs squad?)
- Ships alive at round end

### Phase 1 population

- P team genomes (e.g., 20-50)
- Each team = 1 squad of 8 fighters
- Small group matches: groups of 2 (1v1 with bases)
- Multiple rounds per generation with different pairings

---

## Integration with Existing Arena Spec

### Amended sections

| Existing spec section | Change |
|-----------------------|--------|
| Round end conditions | Add: "all but one base destroyed" as end condition |
| Scoring | Replace survival-time-only with team fitness function (base damage, survival, ships alive, tokens) |
| Sensor input (v1) | Add `is_bullet` to full sensor output. Add strategic nav inputs (dir+range to target, dir+range to home, base HP). Add squad broadcast inputs. |
| Evolution integration | Genome now encodes squad net + fighter net (Phase 1). Team-level fitness replaces individual fitness. |
| ArenaConfig | Add: `base_hp`, `base_radius`, `num_squads`, `squad_broadcast_signals`, `fitness_weights` |
| ArenaSession | Add: base entities, squad assignments, squad net execution in tick loop |

### New entities

- `Base` struct: `float x, y, radius, hp, max_hp; int team_id;`

### New config fields

```
base_hp              — default 1000
base_radius          — default 100.0
num_squads           — default 1 (Phase 1), >1 in Phase 2
squad_broadcast_signals — number of opaque floats per squad (default 4)
fighters_per_squad   — default 8
```

---

## Deferred Features (Beyond Phase 3)

- **Base turrets** — defensive structures that shoot at nearby enemies, giving bases their own simple AI
- **Sub-unit training scenarios** — isolated training for just fighters or just commanders to bootstrap before full arena training
- **Additional strategic modes** — escort, patrol, retreat, regroup
- **Commander/squad topology evolution** — evolving the structure of the higher-level nets, not just weights
- **Health/HP for ships** — multi-hit ships
- **Minimap** — overview of full arena showing bases, ship clusters
- **Designed map layouts** — walls, corridors, enclosed objectives

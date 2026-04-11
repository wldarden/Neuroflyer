# Arena Tick: Centralized Net Execution

**Date:** 2026-04-07
**Scope:** Move neural net execution from UI screens into the engine layer

## Problem

4 of 6 arena-based sim types run the per-tick neural net execution loop (build sensor input → forward pass → decode output → apply actions) in UI screen code. This means:

1. Simulation logic is split between engine and UI — changing how input is built or output is decoded requires updating both layers
2. The same net execution pattern is duplicated across 3 UI screens and 1 engine file
3. UI screens contain ~70-120 lines of pure simulation logic that has nothing to do with rendering

The 2 newest modes (Skirmish, Team Skirmish) already have engine-side tick functions. This refactor brings the older modes in line.

## Design Principle

**UI screens set actions and render. The engine runs the simulation.**

UI screens should:
- Compute scripted squad leader inputs (for drills)
- Call a single engine function to run all nets
- Read back visualization data via optional output parameters
- Render the world

UI screens should NOT:
- Build sensor input vectors
- Call `net.forward()`
- Decode neural net output
- Call `set_ship_actions()`

## Two Tick Variants

### `tick_fighters_scripted()`

For drill modes (FighterDrill, AttackRun) where squad leader inputs are scripted, not learned. Runs fighter nets only.

```cpp
TickEvents tick_fighters_scripted(
    ArenaWorld& world,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> fighter_nets,           // one per ship
    std::span<const SquadLeaderFighterInputs> sl_inputs,  // one per ship, pre-computed
    std::vector<std::vector<float>>& recurrent_states,    // one per ship
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr  // optional viz
);
```

Per-tick behavior:
1. For each alive ship: build arena sensor input from ship state + SL inputs + recurrent memory
2. Forward fighter net
3. Decode output (actions + memory)
4. Apply actions to ArenaWorld
5. Update recurrent state
6. Optionally capture fighter input for visualization
7. Call `world.tick()` and return its TickEvents

Callers (drill screens) compute the scripted SL inputs based on drill phase, then pass them in. The tick function doesn't know or care that they're scripted.

### `tick_arena_with_leader()`

For arena mode where NTM + squad leader nets are learned. Runs the full 3-tier pipeline per team.

```cpp
TickEvents tick_arena_with_leader(
    ArenaWorld& world,
    const ArenaWorldConfig& config,
    const ShipDesign& fighter_design,
    std::span<neuralnet::Network> ntm_nets,        // one per team
    std::span<neuralnet::Network> leader_nets,     // one per team
    std::span<neuralnet::Network> fighter_nets,    // one per team (shared across team's fighters)
    std::vector<std::vector<float>>& recurrent_states,  // one per ship
    const std::vector<int>& ship_teams,
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs = nullptr,
    std::vector<std::vector<float>>* out_leader_inputs = nullptr,
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr
);
```

Per-tick behavior:
1. Build sector grid for spatial NTM queries
2. Per team: compute squad stats → gather threats → run NTM → compute leader inputs → run squad leader → produce SquadLeaderOrder
3. Per fighter: compute SL fighter inputs → build arena sensor input → forward fighter net → decode → apply actions
4. Optionally capture SL inputs, leader inputs, fighter inputs for visualization
5. Call `world.tick()` and return its TickEvents

This is the existing `tick_arena_match()` from `skirmish_tournament.cpp` extracted into a shared location.

## New Files

| File | Purpose |
|------|---------|
| `include/neuroflyer/arena_tick.h` | Declarations for both tick variants |
| `src/engine/arena_tick.cpp` | Single authoritative implementation of both |

## Consumer Changes

### FighterDrillScreen

**Before:** ~80 lines of net execution in `run_tick()` — computes scripted SL inputs, builds sensor input, runs fighter net, decodes output, applies actions, updates memory.

**After:** `run_tick()` computes scripted SL inputs based on drill phase (this stays — it's game-mode logic), then calls `tick_fighters_scripted(world_, design_, nets_, sl_inputs, recurrent_states_, &out_inputs)`. The screen reads `out_inputs` for the net viewer.

### AttackRunScreen

**Before:** Same pattern as FighterDrillScreen.

**After:** Same refactor — compute scripted SL inputs (fixed aggression=1.0, target heading toward starbase), call `tick_fighters_scripted()`.

### ArenaGameScreen

**Before:** ~120 lines in `tick_arena()` — builds sector grid, runs NTM per team, runs squad leader per team, computes SL fighter inputs, builds sensor input, runs fighter net, decodes output, applies actions.

**After:** `tick_arena()` calls `tick_arena_with_leader(world_, config, design, ntm_nets_, leader_nets_, fighter_nets_, recurrent_states_, ship_teams_, &sl_inputs, &leader_inputs, &fighter_inputs)`. The screen reads the optional outputs for the net viewer.

### SkirmishTournament

**Before:** Has its own `tick_arena_match()` in anonymous namespace (~170 lines), identical logic to what we're extracting.

**After:** Calls `tick_arena_with_leader()` from `arena_tick.h`. The anonymous namespace helper is removed.

### TeamSkirmishSession

**No change.** `tick_team_arena_match()` uses per-squad NTM/leader nets and per-ship fighter nets — a fundamentally different signature. It stays as its own function.

## What Stays in UI Screens

- **Scripted SL computation** (drill phase logic) — game-mode-specific, computes the `SquadLeaderFighterInputs` values
- **Network construction** from populations/snapshots — happens at session init, not per-tick
- **Visualization state management** — reading optional output params, passing to net viewer
- **Speed control** — how many ticks to run per frame
- **Follow mode, camera, rendering** — pure UI concerns

## Testing Strategy

1. **Existing tests pass** — ArenaSession, FighterDrill, AttackRun, Skirmish tests all continue to pass since the behavior is identical, just moved
2. **New unit tests** for `tick_fighters_scripted()` — construct a small ArenaWorld, provide dummy nets, verify actions are applied and TickEvents returned
3. **New unit tests** for `tick_arena_with_leader()` — construct a 2-team ArenaWorld, provide NTM+leader+fighter nets, verify full pipeline runs

## Files Modified

| File | Change |
|------|--------|
| `include/neuroflyer/arena_tick.h` | New — declarations |
| `src/engine/arena_tick.cpp` | New — implementations |
| `src/ui/screens/game/fighter_drill_screen.cpp` | Replace net loop with `tick_fighters_scripted()` call |
| `src/ui/screens/game/attack_run_screen.cpp` | Same |
| `src/ui/screens/arena/arena_game_screen.cpp` | Replace net loop with `tick_arena_with_leader()` call |
| `src/engine/skirmish_tournament.cpp` | Replace anonymous `tick_arena_match()` with `tick_arena_with_leader()` call |
| `CMakeLists.txt` | Add arena_tick.cpp to sources |
| `tests/arena_tick_test.cpp` | New — unit tests |
| `tests/CMakeLists.txt` | Add test source |

## Future Work (Not In This Refactor)

- **FlySessionScreen** net execution — scroller mode uses a completely different sensor system. Separate refactor if needed.
- **Consolidate `tick_team_arena_match()`** — if the per-squad net pattern can be generalized to cover both shared-per-team and per-squad cases, all tick functions could unify. Currently not worth the complexity.

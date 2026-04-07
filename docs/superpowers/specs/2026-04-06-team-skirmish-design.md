# Team Skirmish Mode — Design Spec

## Overview

A new game mode where multiple teams co-evolve independently against each other. Unlike Squad Skirmish (which evolves only squad leader + NTM nets with frozen fighters), Team Skirmish evolves **complete team brains** — fighters, squad leaders, and NTMs all evolve within each team's own population pool. Teams compete via round-robin 1v1 matches or free-for-all arenas, and each team's population evolves based on how its individuals performed against other teams.

## Motivation

Current training modes bootstrap individual net types in isolation: fighter drills for fighters, attack runs for combat fighters, squad skirmish for squad leaders. Team Skirmish is the next stage — evolving teams as cohesive units where squad leaders and fighters co-adapt to each other.

## Entry Point

Main menu button: "Team Skirmish" → pushes `TeamSkirmishConfigScreen`.

## Config Screen — `TeamSkirmishConfigScreen`

A new UIScreen with these sections:

### Team Setup

1. **Number of teams** — integer input, range 2-8.
2. **Per-team net selection** — for each team (1..N):
   - Pick a squad snapshot from the hangar (squad leader + NTM are seeded from this).
   - Pick a fighter snapshot from the hangar.
   - Teams can share the same snapshots or use different ones.
3. **Competition mode** — radio toggle: "Round Robin" or "Free-for-All".

### Arena Parameters (reused from `SkirmishConfig`)

- Squads per team, fighters per squad
- World size (width, height)
- Tower count, token count
- Base HP, base radius, base bullet damage
- Rotation speed, bullet max range
- Wrap N/S, wrap E/W
- Friendly fire toggle
- Sector size, NTM sector radius
- Match duration (time limit ticks)

### Scoring Parameters (reused from `SkirmishConfig`)

- Kill points, death points, base hit points

### Evolution Parameters

- Weight mutation rate, weight mutation strength
- Add/remove node chance
- Add/remove layer chance
- Elitism count
- Shared across all teams (the interesting variable is different seed genomes, not different mutation rates).

### Launch

"Start" button validates config (at least 2 teams, each team has both snapshots selected) and pushes `TeamSkirmishScreen` with the config + selected snapshots.

## Engine — `TeamSkirmishConfig`

New config struct in `include/neuroflyer/team_skirmish.h`:

```cpp
enum class CompetitionMode { RoundRobin, FreeForAll };

struct TeamSeed {
    Snapshot squad_snapshot;    // squad leader + NTM seed
    Snapshot fighter_snapshot;  // fighter seed
    std::string squad_genome_dir;   // origin genome for saving squad variants
    std::string fighter_genome_dir; // origin genome for saving fighter variants
};

struct TeamSkirmishConfig {
    SkirmishConfig arena;              // reuse all arena/scoring params
    CompetitionMode competition_mode = CompetitionMode::RoundRobin;
    std::vector<TeamSeed> team_seeds;  // one per team
};
```

## Engine — `TeamPool`

Per-team evolution state:

```cpp
struct TeamPool {
    // Squad brains: NTM + squad leader pairs (one per squad)
    std::vector<TeamIndividual> squad_population;

    // Fighters: individual nets (one per fighter)
    std::vector<Individual> fighter_population;

    // Accumulated fitness scores (indexed same as populations)
    std::vector<float> squad_scores;   // size = num_squads_per_team
    std::vector<float> fighter_scores; // size = fighters_per_squad * num_squads_per_team

    // Seed info for saving
    TeamSeed seed;
};
```

**Squad scoring rule:** A squad leader's fitness is the **sum** of its fighters' scores. This penalizes squad leaders whose fighters die (losing their score contribution) and rewards squad leaders that keep fighters alive and productive.

## Engine — `TeamSkirmishSession`

New engine class in `include/neuroflyer/team_skirmish.h`, `src/engine/team_skirmish.cpp`.

Owns the generation lifecycle. Pure engine — no SDL/ImGui.

### Public API

```cpp
class TeamSkirmishSession {
public:
    TeamSkirmishSession(const TeamSkirmishConfig& config,
                        std::vector<TeamPool> team_pools,
                        uint32_t seed);

    bool step();  // advance featured match one tick; returns true when generation complete
    void run_background_work(int budget_ms);  // run background matches within time budget
    [[nodiscard]] bool is_complete() const noexcept;
    [[nodiscard]] const ArenaSession* current_arena() const noexcept;  // featured match
    [[nodiscard]] const std::vector<TeamPool>& team_pools() const noexcept;

    // Per-ship visualization data (same pattern as SkirmishTournament)
    [[nodiscard]] const std::vector<SquadLeaderFighterInputs>& last_squad_inputs() const noexcept;
    [[nodiscard]] const std::vector<std::vector<float>>& last_fighter_inputs() const noexcept;
    [[nodiscard]] neuralnet::Network* fighter_net(std::size_t ship_index) noexcept;
    [[nodiscard]] neuralnet::Network* leader_net(std::size_t ship_index) noexcept;
    [[nodiscard]] std::size_t current_match_index() const noexcept;
    [[nodiscard]] std::size_t total_matches() const noexcept;
};
```

### State

- `std::vector<TeamPool> team_pools_` — one per team
- Match schedule for the current generation
- Current featured match arena (for live rendering)
- Per-ship mapping: `ship_index → (team_id, squad_index, fighter_index)`
- RNG

### Generation Flow

1. **Populate:** For each team, mutate the seed snapshots into the team's population:
   - `num_squads_per_team` squad brains (each = mutated NTM + squad leader from seed). Generation 1: first individual is unmutated seed, rest are mutated. Subsequent generations: population comes from evolution.
   - `fighters_per_squad * num_squads_per_team` fighter nets (each = mutated fighter from seed). Same seeding logic.

2. **Schedule matches:**
   - Round-robin: all unique team pairs → `N*(N-1)/2` matchups.
   - Free-for-all: one match with all N teams.

3. **Run matches:** Each match is an arena session.
   - **Featured match** (index 0): stepped one tick at a time via `step()` for live rendering.
   - **Background matches:** run headlessly via a new `run_team_skirmish_match()` function, processed during `run_background_work(budget_ms)`.

4. **Ship-to-individual mapping:** When building an arena match, each ship is tagged with `(team_id, squad_index, fighter_index)`:
   - `team_id`: which team pool this ship belongs to
   - `squad_index`: index into `squad_population` (determines which NTM + squad leader drives this ship's squad)
   - `fighter_index`: index into `fighter_population` (determines which fighter net drives this ship)

5. **Score accumulation:** After each match completes:
   - Per-fighter scores (kills, damage, survival from the kill-based scoring in `run_skirmish_match`) are added to `fighter_scores[fighter_index]`.
   - Per-squad-leader scores = sum of their fighters' match scores, added to `squad_scores[squad_index]`.
   - Same mutants play all matches — scores accumulate across matchups.

6. **Signal complete:** When all matches are done, `is_complete()` returns true.

### Key Difference from `SkirmishTournament`

`SkirmishTournament` runs an elimination bracket where variants from a single population compete against each other. `TeamSkirmishSession` runs matches between distinct teams with pre-assigned mutants. There is no bracket — every match in the schedule runs, and scores accumulate. The purpose is fitness evaluation for independent team evolution, not competitive elimination.

### `run_team_skirmish_match()`

A new function (or modification of `run_skirmish_match`) that handles N teams where each team has **different nets per squad and per fighter**. Current `run_skirmish_match()` assumes one `TeamIndividual` per team (shared nets for all squads/fighters on that team). The new function needs:

- Per-squad NTM + squad leader nets (not one per team)
- Per-ship fighter nets (not one per team)
- Ship-to-squad mapping to route the correct squad leader inputs to each fighter
- Per-ship score tracking (not just per-team totals)

This is a fork of `run_skirmish_match()` with the net lookup changed from per-team to per-squad/per-ship.

### Per-Ship Score Tracking

The match result struct needs per-ship granularity:

```cpp
struct TeamSkirmishMatchResult {
    // Per-ship scores indexed by global ship index
    std::vector<float> ship_scores;
    uint32_t ticks_elapsed = 0;
    bool completed = false;
};
```

Per-ship scoring (computed at match end):
- `+kill_points` per enemy killed by this ship
- `-death_points` if this ship died
- `+base_hit_points` proportional share of base damage dealt by this ship's team (or tracked per-ship if ArenaSession supports it)
- `+base_kill_points` proportional share if enemy base destroyed by this ship's team

Note: ArenaSession currently tracks kills per ship (`enemy_kills()`) but base damage is tracked per-base, not per-shooter. For initial implementation, base damage/kill bonus can be split evenly across alive fighters on the team. Future refinement could track per-ship base damage.

## Evolution — Screen-Driven

After `TeamSkirmishSession` signals generation complete, the screen (not the engine) runs evolution:

For each team pool:
1. Assign accumulated `fighter_scores` → `fighter_population[i].fitness`
2. Assign accumulated `squad_scores` → `squad_population[i].fitness`
3. Call `evolve_population(fighter_population, fighter_evo_config, rng)` → new fighter population
4. Call `evolve_squad_only(squad_population, squad_evo_config, rng)` → new squad population (NTM + squad leader mutated)
5. Reset scores to zero
6. Create new `TeamSkirmishSession` with evolved populations

**Important:** `evolve_squad_only` expects `vector<TeamIndividual>` — it freezes the fighter inside each `TeamIndividual`. Since our squad population is already stored as `TeamIndividual` (with a dummy/unused fighter field), this works as-is. The fighter field in the squad population's `TeamIndividual` is irrelevant — actual fighters live in the separate `fighter_population`.

## Game Screen — `TeamSkirmishScreen`

New UIScreen driving the generation loop.

### Initialization

1. Load snapshots from config's `TeamSeed` entries
2. Convert to `Individual` (adapt if needed, same as `SkirmishScreen::initialize`)
3. Build initial `TeamPool` for each team (mutate seeds into populations)
4. Create first `TeamSkirmishSession`

### Tick Loop

Same pattern as `SkirmishScreen::on_draw`:
1. If not paused, call `session_->step()` N times per frame (speed control)
2. Call `session_->run_background_work(budget_ms)` to fill remaining frame time
3. If session complete, call `evolve_generation()` and create next session
4. Render world + HUD

### Rendering

Reuse the SDL drawing helpers from `SkirmishScreen` (rotated triangles, circles, bases, bullets, tokens, towers). These can be extracted to a shared utility or duplicated (matching existing pattern of duplicated drawing helpers between drill/skirmish screens).

**Team colors:** Each team gets a distinct outline color. With up to 8 teams, use a fixed palette (blue, red, green, yellow, cyan, magenta, orange, white). Squad coloring within a team uses lighter/darker shades.

### HUD (ImGui)

- Per-team stat panels: team color, generation number, best fighter fitness, best squad fitness, fighters alive, squads alive
- Match progress: "Match 2/6" (round-robin) or tick counter (free-for-all)
- Overall generation counter
- Competition mode indicator

### Controls

Same as current skirmish:
- Tab: cycle through individual fighters
- F: toggle fighter/squad leader net view (in follow mode)
- Space: push pause screen
- 1-4: speed control
- Escape: back to swarm or exit
- Arrow keys: pan camera
- Scroll: zoom

### Follow Mode

When following a ship, the net viewer shows that specific fighter's net (unique per ship, since each ship has its own mutant). F toggles to show that ship's squad leader net. This is more interesting than current skirmish follow mode because each ship genuinely has different weights.

## Pause Screen — `TeamSkirmishPauseScreen`

New UIScreen with tabs:

### Evolution Tab

Mutation rate sliders (same as current `SkirmishPauseScreen`). Shared across all teams. On resume, updated config applies to next generation's evolution.

### Save Fighters Tab

- Dropdown or tab bar to select team (Team 1, Team 2, ...)
- List of that team's fighter population, sorted by fitness
- Multi-select checkboxes
- "Save Selected" button
- Saved as `NetType::Fighter` snapshots to the **fighter's origin genome directory** (`team_seed.fighter_genome_dir / "squad/"`)
- Variant name includes team identifier (e.g., "ts-team1-gen5-f0")

### Save Squad Leaders Tab

- Same team selector
- List of that team's squad population (NTM + squad leader pairs), sorted by fitness
- Multi-select checkboxes
- "Save Selected" button
- Saved as paired `NetType::SquadLeader` + `NetType::NTM` snapshots to the **squad's origin genome directory** (`team_seed.squad_genome_dir / "squad/"`)
- Squad leader snapshot's `paired_fighter_name` records which fighter genome it was trained alongside

## File Layout

### Headers (`include/neuroflyer/`)

- `team_skirmish.h` — `TeamSkirmishConfig`, `TeamPool`, `TeamSeed`, `CompetitionMode`, `TeamSkirmishMatchResult`, `run_team_skirmish_match()` declaration

### Headers (`include/neuroflyer/ui/screens/`)

- `team_skirmish_config_screen.h`
- `team_skirmish_screen.h`
- `team_skirmish_pause_screen.h`

### Engine (`src/engine/`)

- `team_skirmish.cpp` — `TeamSkirmishSession`, `run_team_skirmish_match()` implementation

### UI (`src/ui/screens/game/`)

- `team_skirmish_config_screen.cpp`
- `team_skirmish_screen.cpp`
- `team_skirmish_pause_screen.cpp`

### Tests (`tests/`)

- `team_skirmish_test.cpp` — tests for `run_team_skirmish_match()`, score accumulation, per-ship mapping

## Scope Boundaries

**In scope:**
- New engine session class with per-ship/per-squad net assignment and score tracking
- New match runner supporting heterogeneous nets per ship
- Config screen with per-team net selection and competition mode toggle
- Game screen with multi-team rendering and generation loop
- Pause screen with per-team fighter and squad leader save
- Main menu entry point
- Tests for the engine layer

**Out of scope (future):**
- Team snapshot format (saving/loading entire team brain as one unit)
- Cross-team breeding or migration
- Per-team mutation rate configuration
- Per-ship base damage tracking in ArenaSession (use even split for now)
- Replay/history of past generations

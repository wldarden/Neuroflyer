# Squad Skirmish Drill — Design Spec

**Date:** 2026-04-03
**Status:** Draft

## Overview

A squad net evolution drill where N mutated squad net variants (NTM + squad leader) compete in an elimination tournament. Each variant is paired with the same fixed fighter net. Variants accumulate fitness across all matches they play, then standard evolution (via `evolve_squad_only()`) breeds the next generation. This is the first drill that evolves squad-level decision-making.

## Requirements

1. Evolves squad nets (NTM + squad leader) only. Fighter nets are fixed.
2. Elimination tournament per generation: variants are paired into 1v1 matches, winners advance, losers are eliminated. All variants keep their accumulated scores for evolution.
3. Each matchup is played multiple times with different random seeds to reduce variance.
4. Match end conditions: time limit reached, only one team alive, only one starbase alive.
5. Scoring: kill points per enemy fighter killed, large bonus for destroying enemy starbase, penalty per own fighter lost.
6. Entry point from squad variant listing in VariantViewerScreen. Requires a selected squad variant and a paired fighter variant.
7. Live visualization of the current match, tournament bracket progress, and leaderboard.
8. Reuse `FighterDrillPauseScreen` for evolution config and variant saving.

## Architecture

### Engine: `SkirmishConfig`

Configuration for the skirmish drill, stored in `attack_run_session.h`-style header.

```cpp
struct SkirmishConfig {
    // Population
    std::size_t population_size = 20;

    // Tournament
    std::size_t seeds_per_match = 3;

    // Arena per match
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    std::size_t num_squads_per_team = 1;
    std::size_t fighters_per_squad = 8;
    std::size_t tower_count = 50;
    std::size_t token_count = 30;
    uint32_t time_limit_ticks = 60 * 60;  // 60 seconds at 60fps

    // Bases
    float base_hp = 1000.0f;
    float base_radius = 100.0f;
    float base_bullet_damage = 10.0f;

    // Ship physics
    float rotation_speed = 0.05f;
    float bullet_max_range = 1000.0f;
    bool wrap_ns = true;
    bool wrap_ew = true;

    // Sector grid (for NTM)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;

    // Scoring
    float kill_points = 100.0f;       // per enemy fighter killed
    float death_points = 20.0f;       // per own fighter lost (subtracted)
    // base_kill_points = kill_points * fighters_per_squad * num_squads_per_team
    // (destroying a starbase is worth almost as much as killing every fighter)

    float base_kill_points() const {
        return kill_points * static_cast<float>(fighters_per_squad * num_squads_per_team);
    }
};
```

### Engine: `run_skirmish_match()`

Runs a single arena match between 2+ teams, returning per-team scores.

```cpp
struct SkirmishMatchResult {
    std::vector<float> team_scores;
    uint32_t ticks_elapsed = 0;
    bool completed = false;
};

SkirmishMatchResult run_skirmish_match(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& teams,  // one TeamIndividual per team
    uint32_t seed);
```

**Match execution** — same tick loop structure as `run_arena_match()`:
1. Create `ArenaSession` from an `ArenaConfig` derived from `SkirmishConfig`
2. Build NTM + squad leader networks per team. Build fighter network once (shared).
3. Per tick:
   - Build sector grid
   - For each team: run NTM threat selection → run squad leader → produce orders
   - For each alive fighter: compute squad leader inputs → build arena sensor input → fighter forward pass → decode → set actions
   - `arena.tick()`
4. Match ends when: `arena.is_over()` (time limit, one team alive, or one starbase alive)

**Scoring** — computed from `ArenaSession` state at match end:
- For each team t:
  - `+kill_points` × `enemy_kills[i]` for each fighter `i` on team `t`
  - `+base_kill_points()` for each enemy base destroyed (hp <= 0)
  - `-death_points` × count of own dead fighters

All teams in a match share the same fighter `Individual` (weights cloned per team), but each team has its own squad net variant (NTM + squad leader).

### Engine: `SkirmishTournament`

Pure engine class that orchestrates the elimination bracket across all variants.

```cpp
struct TournamentRound {
    std::vector<std::pair<std::size_t, std::size_t>> matchups;  // indices into variant pool
    std::size_t bye_index = SIZE_MAX;  // variant that gets a bye (if odd count)
};

class SkirmishTournament {
public:
    SkirmishTournament(const SkirmishConfig& config,
                       const ShipDesign& fighter_design,
                       const std::vector<TeamIndividual>& variants,
                       uint32_t seed);

    // Advance one match (one seed). Returns true if tournament is complete.
    // This is called repeatedly by the UI to drive progress one match at a time,
    // allowing rendering between matches.
    bool step();

    // State queries
    std::size_t current_round() const;
    std::size_t current_match() const;
    std::size_t current_seed() const;
    std::size_t total_rounds() const;
    std::size_t matches_in_round() const;
    bool is_complete() const;

    // Access current match's ArenaSession for live rendering
    const ArenaSession* current_arena() const;

    // Results
    const std::vector<float>& variant_scores() const;  // accumulated fitness per variant

    // Access the two team indices for the current match (for UI labeling)
    std::pair<std::size_t, std::size_t> current_matchup() const;

private:
    void build_rounds();
    void advance_match();
    void advance_round();
    void run_current_match_tick();

    SkirmishConfig config_;
    ShipDesign fighter_design_;
    std::vector<TeamIndividual> variants_;

    std::vector<TournamentRound> rounds_;
    std::vector<float> scores_;          // accumulated per variant
    std::vector<std::size_t> active_;    // indices of variants still in tournament

    std::size_t round_idx_ = 0;
    std::size_t match_idx_ = 0;
    std::size_t seed_idx_ = 0;

    // Current match state
    std::unique_ptr<ArenaSession> arena_;
    // Per-team networks for current match (rebuilt each new matchup)
    std::vector<neuralnet::Network> ntm_nets_;
    std::vector<neuralnet::Network> leader_nets_;
    neuralnet::Network fighter_net_;  // shared across all teams
    std::vector<std::vector<float>> recurrent_states_;  // per fighter

    std::mt19937 rng_;
    bool complete_ = false;
};
```

**Tournament bracket construction (`build_rounds()`):**
1. Start with all N variant indices, shuffled.
2. While more than 3 variants remain:
   - If odd count, assign a bye to a random variant (advances automatically).
   - Pair remaining into 1v1 matchups → one `TournamentRound`.
3. Final round: if 2-3 variants remain, do a multi-seed free-for-all (2-way or 3-way).

**`step()` drives one simulation tick:**
- If no arena exists yet (start of a new match/seed): create `ArenaSession`, build networks for the two teams, initialize recurrent states. Then return false (no tick yet, arena just set up — UI can render the initial state).
- If arena exists and isn't over: run one full tick (build sector grid → NTM → squad leader → fighters → `arena.tick()`). Return false.
- If arena exists and just finished: record per-team scores into `scores_[]`, then advance state (next seed, next match, or next round). Destroy arena. If all rounds complete, set `complete_ = true` and return true.

This design lets the UI call `step()` in a tight loop (with speed multiplier) and render the current arena between frames. Each `step()` call does at most one arena tick worth of work.

**Finals handling:** When 2-3 variants remain, the final round uses `seeds_per_match * 2` seeds (more games for the finals) and if 3 variants, runs 3-way free-for-all matches instead of 1v1.

### UI: `SkirmishScreen`

UIScreen subclass launched from VariantViewerScreen's squad listing.

**Constructor args:** squad `Snapshot`, fighter `Snapshot`, genome_dir, variant_name.

**Initialization:**
1. Extract ship design from fighter snapshot
2. Convert squad snapshot to `TeamIndividual` template (NTM + squad leader from squad snapshot, fighter from fighter snapshot)
3. Create population of `population_size` variants by mutating the template's NTM + squad leader weights (fighter frozen)
4. Create first `SkirmishTournament`

**Tick loop:**
- Call `tournament_->step()` `ticks_per_frame_` times
- If tournament complete: evolve generation, create new tournament

**Rendering:**
- Left: live arena view of current match (same SDL rendering as arena game view — towers, tokens, ships, bases, bullets)
- Right panel (swarm mode):
  - "SQUAD SKIRMISH" title
  - Generation number
  - Tournament progress: "Round X/Y — Match M/N — Seed S/K"
  - Current matchup: "Variant A vs Variant B"
  - Speed, camera mode
  - Leaderboard: top 10 variants by accumulated score
  - Controls help
- Right panel (follow mode): compact HUD + net viewer (squad leader or fighter toggle)

**Evolution between generations:**
1. Extract `variant_scores()` from tournament → assign as fitness to population
2. `evolve_squad_only()` with evo_config
3. Rebuild all NTM + squad leader networks (fighter stays frozen)
4. Create new `SkirmishTournament` with new population

**Pause screen:** Reuse `FighterDrillPauseScreen`. The on_resume callback updates evo_config. Save Variants tab saves squad net variants to `squad/` directory with `NetType::SquadLeader`.

**Controls:** Tab (camera), 1-4 (speed), Space (pause), Escape (exit).

### Entry Point: VariantViewerScreen

Add "Squad Skirmish" button in the squad actions panel (where "Train vs Squad" and "Train Base Attack" already exist). Same pattern: requires paired fighter variant.

New action: `Action::SquadSkirmish`. Handler loads squad snapshot + fighter snapshot, pushes `SkirmishScreen`.

### File Layout

| Component | Header | Source |
|-----------|--------|--------|
| `SkirmishConfig` | `include/neuroflyer/skirmish.h` | — |
| `run_skirmish_match()` | `include/neuroflyer/skirmish.h` | `src/engine/skirmish.cpp` |
| `SkirmishTournament` | `include/neuroflyer/skirmish.h` | `src/engine/skirmish.cpp` |
| `SkirmishScreen` | `include/neuroflyer/ui/screens/skirmish_screen.h` | `src/ui/screens/game/skirmish_screen.cpp` |
| Tests | — | `tests/skirmish_test.cpp` |

### Tests: `skirmish_test.cpp`

Engine tests for tournament logic and match runner:

1. **Config defaults** — verify default scoring, population size, timing.
2. **base_kill_points formula** — verify `kill_points * fighters_per_squad * num_squads_per_team`.
3. **Tournament bracket construction** — 20 variants → correct number of rounds and matchups.
4. **Tournament bracket odd count** — 7 variants → one bye per round.
5. **Tournament bracket small count** — 3 variants → goes straight to finals.
6. **Match runs to completion** — create 2 TeamIndividuals, run match, get scores, ticks > 0.
7. **Match ends on time limit** — set short time limit, verify match completes.
8. **Match scoring: kills increase score** — verify kill_points applied.
9. **Tournament step drives progress** — call step() in a loop, verify tournament completes.
10. **Tournament scores accumulate** — verify variants that play more rounds accumulate more score.
11. **Full tournament with evolution** — run a tournament, evolve, verify population changes.

## Out of Scope

- Fighter evolution (fighters are frozen in this drill)
- Multi-squad teams (num_squads_per_team > 1 is configurable but default is 1)
- Saving/loading tournament state mid-generation
- Spectator mode for replaying past matches
- Network visualization of NTM sub-net during matches
- Configurable bracket structure (always single-elimination)

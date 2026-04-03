# Squad Skirmish Drill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a squad net evolution drill where N mutated squad net variants compete in an elimination tournament of arena matches, with kill-based scoring and standard evolution between generations.

**Architecture:** Three engine components (`SkirmishConfig`, `run_skirmish_match()`, `SkirmishTournament`) plus one UI screen (`SkirmishScreen`). The match runner forks `run_arena_match()` with kill-based scoring. The tournament orchestrates an elimination bracket with per-tick stepping for live rendering. Entry point from squad variant listing in VariantViewerScreen.

**Tech Stack:** C++20, SDL2, ImGui, Google Test

**Spec:** `docs/superpowers/specs/2026-04-03-squad-skirmish-drill-design.md`

---

## Context

Squad nets (NTM + squad leader) currently have no dedicated evolution drill. The existing arena training mode uses `ArenaGameScreen` which evolves full `TeamIndividual`s (all 3 nets). We need a focused drill that evolves only the squad-level decision-making while keeping fighter behavior fixed, using an elimination tournament to generate competitive fitness signal.

## File Structure

| Component | Header | Source | Responsibility |
|-----------|--------|--------|----------------|
| Config + match runner | `include/neuroflyer/skirmish.h` | `src/engine/skirmish.cpp` | `SkirmishConfig`, `SkirmishMatchResult`, `run_skirmish_match()` |
| Tournament engine | `include/neuroflyer/skirmish_tournament.h` | `src/engine/skirmish_tournament.cpp` | `SkirmishTournament` class with `step()`, bracket construction |
| UI screen | `include/neuroflyer/ui/screens/skirmish_screen.h` | `src/ui/screens/game/skirmish_screen.cpp` | Screen driving tournament, rendering arena, showing HUD |
| Match runner tests | — | `tests/skirmish_test.cpp` | Config, match scoring, match completion |
| Tournament tests | — | `tests/skirmish_tournament_test.cpp` | Bracket construction, step progression, score accumulation |

## Key Existing Code to Reuse

| Code | Location | Used For |
|------|----------|----------|
| `run_arena_match()` tick loop | `src/engine/arena_match.cpp` | Template for match runner + tournament tick |
| `TeamIndividual::create()` | `team_evolution.h/cpp` | Creating population with squad variant + fighter variant |
| `evolve_squad_only()` | `team_evolution.h/cpp` | Evolution between generations (fighter frozen) |
| `ArenaSession` | `arena_session.h` | World simulation per match |
| `SectorGrid` | `sector_grid.h` | NTM spatial queries per tick |
| `gather_near_threats()`, `run_ntm_threat_selection()`, `run_squad_leader()` | `squad_leader.h/cpp` | Squad AI per tick |
| `compute_squad_leader_fighter_inputs()` | `squad_leader.h/cpp` | Fighter input from squad orders |
| `build_arena_ship_input()`, `decode_output()` | `arena_sensor.h`, `sensor_engine.h` | Fighter IO |
| `FighterDrillScreen` | `fighter_drill_screen.cpp` | Template for SkirmishScreen structure |
| `FighterPairingModal` | `fighter_pairing_modal.h` | Fighter selection for entry point |

---

### Task 1: SkirmishConfig + run_skirmish_match() Header and Implementation

**Files:**
- Create: `include/neuroflyer/skirmish.h`
- Create: `src/engine/skirmish.cpp`
- Modify: `CMakeLists.txt` (add engine source after `attack_run_session.cpp`)
- Modify: `tests/CMakeLists.txt` (add engine source after `../src/engine/attack_run_session.cpp`)

**What to build:**
- `SkirmishConfig` struct with defaults: population_size=20, seeds_per_match=3, world 4000x4000, fighters_per_squad=8, time_limit=3600 ticks, kill_points=100, death_points=20, base_kill_points() formula, `to_arena_config()` converter
- `SkirmishMatchResult` struct: team_scores vector, ticks_elapsed, completed bool
- `run_skirmish_match()` function: fork of `run_arena_match()` (in `src/engine/arena_match.cpp`) with kill-based scoring instead of weighted fitness

**Scoring difference from `run_arena_match()`:** Instead of the weighted `damage_dealt + survival + alive_frac + tokens` formula, use:
- `+kill_points` × sum of `enemy_kills[i]` for each fighter on team
- `+base_kill_points()` for each enemy base with `!alive()`
- `-death_points` × count of own dead fighters

**The tick loop** (sector grid → NTM → squad leader → fighters → arena.tick()) is identical to `run_arena_match()`. Copy it verbatim and only change the scoring section at the end.

- [ ] **Step 1:** Create `include/neuroflyer/skirmish.h` with SkirmishConfig, SkirmishMatchResult, run_skirmish_match declaration
- [ ] **Step 2:** Create `src/engine/skirmish.cpp` — implement `to_arena_config()` and `run_skirmish_match()` by forking `src/engine/arena_match.cpp`
- [ ] **Step 3:** Add to both CMakeLists.txt files
- [ ] **Step 4:** Build: `cmake --build build --target neuroflyer_tests`
- [ ] **Step 5:** Commit

---

### Task 2: Match Runner Tests

**Files:**
- Create: `tests/skirmish_test.cpp`
- Modify: `tests/CMakeLists.txt` (add test file after `attack_run_session_test.cpp`)

**Tests to write:**
1. `SkirmishConfigTest::Defaults` — verify population_size=20, seeds_per_match=3, kill_points=100, death_points=20, fighters_per_squad=8, time_limit=3600
2. `SkirmishConfigTest::BaseKillPointsFormula` — verify kill_points × fighters × squads
3. `SkirmishConfigTest::ToArenaConfig` — verify field mapping
4. `SkirmishMatchTest::RunsToCompletion` — create 2 TeamIndividuals with `TeamIndividual::create()`, run match with short time limit, verify completed=true, ticks>0, scores.size()=2
5. `SkirmishMatchTest::EndsOnTimeLimit` — indestructible bases (hp=999999), verify match ends near time limit
6. `SkirmishMatchTest::ScoresAreFinite` — verify all scores are finite floats

**Helper:** `make_test_team()` using `TeamIndividual::create(design, {4}, ntm_cfg, leader_cfg, rng)` and `make_test_design()` returning a ShipDesign with memory_slots=2.

- [ ] **Step 1:** Create test file
- [ ] **Step 2:** Add to tests/CMakeLists.txt
- [ ] **Step 3:** Run: `./build/tests/neuroflyer_tests --gtest_filter='Skirmish*'`
- [ ] **Step 4:** Commit

---

### Task 3: SkirmishTournament Header and Implementation

**Files:**
- Create: `include/neuroflyer/skirmish_tournament.h`
- Create: `src/engine/skirmish_tournament.cpp`
- Modify: `CMakeLists.txt` (add after `skirmish.cpp`)
- Modify: `tests/CMakeLists.txt` (add after `../src/engine/skirmish.cpp`)

**What to build:**

**Header:** `TournamentRound` struct (matchups vector + bye_index). `SkirmishTournament` class with:
- Constructor(config, fighter_design, variants, seed)
- `step()` → bool (true when complete)
- State queries: current_round/match/seed, total_rounds, matches_in_round, is_complete
- `current_arena()` → const ArenaSession* for rendering
- `variant_scores()` → accumulated fitness per variant
- `current_matchup()` → pair of variant indices
- `current_ship_teams()` → ship-to-team mapping for rendering

**Implementation:**

`build_rounds()`:
- Operates on `active_` (variant indices still in tournament)
- If active_.size() > 3: assign bye if odd, pair rest into 1v1 matchups, build ONE round
- If active_.size() <= 3: build final round (2 → one matchup; 3 → three matchups for each pair combination)
- Called initially and after each round completes

`start_match()`:
- Get matchup indices from current round
- Build ArenaConfig via `config_.to_arena_config()` with num_teams=2
- Create ArenaSession with random seed
- Build NTM + squad leader networks per team, fighter network per team (from variants)
- Init recurrent states and ship_teams

`run_tick()`:
- Same tick loop as `run_skirmish_match()`: sector grid → NTM → squad leader → fighters → arena.tick()

`finish_match()`:
- Compute kill-based scores and add to accumulated `scores_[]`
- Advance state: next seed → next match → next round
- When a round completes: determine winners (higher score per matchup), update active_, build next round
- When no more rounds possible (active_.size() <= 1): set complete_

`step()`:
- If no match in progress and not complete: call start_match(), return false
- If arena exists and not over: call run_tick(), return false
- If arena exists and over: call finish_match(), return complete_

**Finals:** Last round uses `seeds_per_match * 2` seeds.

- [ ] **Step 1:** Create header
- [ ] **Step 2:** Create implementation
- [ ] **Step 3:** Add to both CMakeLists.txt
- [ ] **Step 4:** Build to verify
- [ ] **Step 5:** Commit

---

### Task 4: Tournament Tests

**Files:**
- Create: `tests/skirmish_tournament_test.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Tests:**
1. `CompletesWithFourVariants` — 4 variants, small config, step until complete, verify is_complete() and scores.size()=4
2. `CompletesWithSixVariants` — 6 variants, verify completes
3. `CompletesWithOddCount` — 5 variants (bye handling), verify completes
4. `ScoresAccumulate` — 4 variants with 2 seeds, verify at least one nonzero score
5. `ArenaAccessibleDuringMatch` — step twice, verify current_arena() != nullptr
6. `TwoVariantsTournament` — minimal: 2 variants, verify completes with scores.size()=2

**Helper functions:** `make_test_variants(count, design, rng)`, `small_config()` returning tiny world (800x800, 2 fighters, 0 obstacles, 60 tick limit, 1 seed).

- [ ] **Step 1:** Create test file
- [ ] **Step 2:** Add to tests/CMakeLists.txt
- [ ] **Step 3:** Run: `./build/tests/neuroflyer_tests --gtest_filter='SkirmishTournament*'`
- [ ] **Step 4:** Commit

---

### Task 5: SkirmishScreen Header and Implementation

**Files:**
- Create: `include/neuroflyer/ui/screens/skirmish_screen.h`
- Create: `src/ui/screens/game/skirmish_screen.cpp`
- Modify: `CMakeLists.txt` (add UI source after `attack_run_screen.cpp`)

**What to build:**

**Header:** `SkirmishScreen : UIScreen` with constructor(squad_snapshot, fighter_snapshot, genome_dir, variant_name). Members: config, tournament, evo_config, population (vector<TeamIndividual>), camera, generation, ticks_per_frame, net_viewer_state, rng.

**Implementation** — fork `FighterDrillScreen` with these changes:

1. **initialize():**
   - Extract ship_design from fighter_snapshot
   - Convert squad snapshot → Individual (squad_seed)
   - Convert fighter snapshot → Individual (fighter variant)
   - Create population: for each of population_size, call `TeamIndividual::create(ship_design, {fighter_hidden}, ntm_cfg, leader_cfg, rng, &fighter_ind, &squad_ind)`, then apply mutations to NTM + squad only (via `apply_mutations()`)
   - Create first `SkirmishTournament(config, ship_design, population, seed)`

2. **Tick loop (in on_draw):**
   - Call `tournament_->step()` × ticks_per_frame
   - If tournament->is_complete(): call evolve_generation()

3. **evolve_generation():**
   - Copy tournament->variant_scores() to population fitness
   - `evolve_squad_only(population, evo_config, rng)`
   - Increment generation
   - Create new SkirmishTournament with evolved population

4. **render_world():**
   - Get `tournament_->current_arena()`. If null: just clear to dark background.
   - Get ship_teams from `tournament_->current_ship_teams()`
   - Render ships: team 0 = blue, team 1 = red
   - Render bases: team-colored circles with HP bars
   - Render towers (gray), tokens (gold), bullets (yellow)
   - Same camera logic as fighter_drill_screen (swarm/best/worst)

5. **render_hud():**
   - Title: "SQUAD SKIRMISH"
   - Generation number
   - Tournament: "Round X/Y — Match M/N — Seed S/K"
   - Matchup: "Variant A vs Variant B" (from tournament->current_matchup())
   - Speed, Camera mode, Alive count
   - Leaderboard: top 10 variants sorted by tournament->variant_scores()
   - Controls help

6. **Pause:** Inline ImGui overlay (evo sliders + resume) rather than FighterDrillPauseScreen (which takes vector<Individual>, not vector<TeamIndividual>). Simple: when Space pressed, set paused_=true, draw overlay with evo_config sliders. Space/Esc resumes.

7. **Controls:** Same as fighter drill (Tab camera, 1-4 speed, Space pause, Esc exit).

- [ ] **Step 1:** Create header
- [ ] **Step 2:** Create implementation (fork fighter_drill_screen.cpp with above changes)
- [ ] **Step 3:** Add to CMakeLists.txt
- [ ] **Step 4:** Build: `cmake --build build --target neuroflyer`
- [ ] **Step 5:** Commit

---

### Task 6: Entry Point in VariantViewerScreen

**Files:**
- Modify: `include/neuroflyer/ui/screens/variant_viewer_screen.h` (add Action::SquadSkirmish)
- Modify: `src/ui/screens/hangar/variant_viewer_screen.cpp` (add include, button, handler)

**Changes:**

1. **Action enum:** Add `SquadSkirmish` after `SquadDelete`
2. **Include:** `#include <neuroflyer/ui/screens/skirmish_screen.h>`
3. **Button:** In `draw_squad_actions()`, after "Train Base Attack" button, add "Squad Skirmish" button → `action = Action::SquadSkirmish`
4. **Handler:** In main switch, after `Action::SquadDelete` case:
   - If no paired fighter: show FighterPairingModal
   - Else: load squad snapshot from `squad_variant_path(sel)`, load fighter snapshot from `genome_dir + "/" + paired_fighter_name_ + ".bin"` (fallback to genome.bin), push SkirmishScreen with both snapshots

- [ ] **Step 1:** Add Action enum value
- [ ] **Step 2:** Add include
- [ ] **Step 3:** Add button in draw_squad_actions()
- [ ] **Step 4:** Add handler in switch
- [ ] **Step 5:** Build: `cmake --build build`
- [ ] **Step 6:** Commit

---

### Task 7: Full Build + All Tests

- [ ] **Step 1:** Run full test suite: `./build/tests/neuroflyer_tests`
- [ ] **Step 2:** Build main app: `cmake --build build --target neuroflyer`
- [ ] **Step 3:** Verify no regressions
- [ ] **Step 4:** Update backlog if needed

---

## Verification

1. **Unit tests:** `./build/tests/neuroflyer_tests --gtest_filter='Skirmish*'` — all match + tournament tests pass
2. **Full suite:** `./build/tests/neuroflyer_tests` — no regressions
3. **App build:** clean compile
4. **Manual test:** Hangar → select genome → SquadNets tab → select squad variant → pair fighter → click "Squad Skirmish" → verify:
   - Arena renders with two colored teams
   - HUD shows tournament progress
   - Matches complete and advance
   - Tournament finishes, evolution runs, next generation starts
   - Speed controls and camera modes work
   - Escape exits

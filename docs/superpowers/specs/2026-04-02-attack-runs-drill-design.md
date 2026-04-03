# Attack Runs Drill — Design Spec

**Date:** 2026-04-02
**Status:** Draft

## Overview

A new fighter drill mode called "Attack Runs." Fighters evolve to locate and destroy starbases that spawn at random positions across 3 phases per generation. Scoring rewards movement toward the target and bullet hits. Destroying a target early advances to the next phase immediately.

## Requirements

1. 3 attack phases per generation. Each phase spawns a starbase at a random position.
2. Scoring: per-tick velocity toward target (`travel_weight`), big bonus per bullet hit (`attack_hit_bonus`), token collection bonus, death penalty on tower collision.
3. Destroying the starbase ends the phase early and immediately starts the next phase (fresh timer). No time carryover.
4. After phase 3, generation ends and evolution runs.
5. Scripted squad leader inputs: same as existing fighter drill's Attack phase (aggression=1.0, spacing=0.0, heading/distance to current target).
6. Reuse existing `FighterDrillPauseScreen` for evolution config and variant saving.
7. Entry point: "Attack Runs" button in `VariantViewerScreen` Fighters tab.

## Architecture

### Engine: `AttackRunSession`

Pure engine class, no SDL/ImGui dependencies. Located at `include/neuroflyer/attack_run_session.h` and `src/engine/attack_run_session.cpp`.

#### AttackRunPhase Enum

```
Phase1 (0) -> Phase2 (1) -> Phase3 (2) -> Done (3)
```

#### AttackRunConfig

```cpp
struct AttackRunConfig {
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    int population_size = 200;
    int tower_count = 50;
    int token_count = 30;

    // Starbase
    float min_starbase_distance = 1000.0f;  // minimum spawn distance from squad center
    float starbase_hp = 1000.0f;
    float starbase_radius = 100.0f;

    // Physics
    float rotation_speed = 0.05f;
    float bullet_max_range = 1000.0f;
    float base_bullet_damage = 10.0f;

    // Timing
    int phase_duration_ticks = 1200;  // 20 seconds at 60fps

    // Scoring
    float travel_weight = 1.0f;       // per-tick dot(velocity, toward_target)
    float attack_hit_bonus = 500.0f;  // per bullet hit on starbase
    float token_bonus = 50.0f;        // per token collected
    float death_penalty = 200.0f;     // on tower collision death

    // Wrapping
    bool wrap_ns = true;
    bool wrap_ew = true;
};
```

#### World Setup

- 4000x4000 toroidal arena
- Ships spawn at squad center (world center) with random rotation
- Towers: `tower_count` placed randomly, radius 15-35
- Tokens: `token_count` placed randomly, radius 10
- No starbase at init — spawned at phase start

#### Phase Logic

1. During construction, the session spawns the first starbase for Phase1. At each subsequent phase transition, `spawn_starbase()` places a new starbase at a random position at least `min_starbase_distance` from squad center. Starbase is team 1 (enemy), full HP.
2. Each phase has its own tick counter, reset to 0 at phase start.
3. Phase ends when: starbase HP <= 0 (destroyed) OR phase tick counter >= `phase_duration_ticks`.
4. On phase end: advance to next phase, reset phase tick counter, spawn new starbase (unless transitioning to Done).
5. After Phase3 ends, session state becomes Done.

#### Tick Loop

Same order of operations as `FighterDrillSession`:

1. Update ship positions (dx/dy applied)
2. Boundary wrapping (toroidal)
3. Spawn bullets (ships with wants_shoot && cooldown <= 0)
4. Update bullets (directional movement, range despawn)
5. Resolve collisions:
   - Ship-tower: kill ship, apply death penalty
   - Ship-token: remove token, apply token bonus
   - Bullet-starbase: damage starbase, apply attack_hit_bonus to shooter
   - Bullet-tower: destroy tower
6. Compute phase scores: `dot(velocity, toward_starbase) * travel_weight` for alive ships
7. Decrement cooldowns
8. Cleanup dead bullets
9. Increment tick counters
10. Check phase transition (timer expired OR starbase destroyed)

#### Public API

```cpp
void tick();
void set_ship_actions(int idx, bool up, bool down, bool left, bool right, bool shoot);

AttackRunPhase phase() const;
int current_tick() const;
int phase_ticks_remaining() const;
bool is_over() const;
int phase_number() const;  // 1, 2, or 3 (human-readable)

const std::vector<float>& get_scores() const;
const std::vector<Triangle>& ships() const;
const std::vector<Tower>& towers() const;
const std::vector<Token>& tokens() const;
const std::vector<Bullet>& bullets() const;
const Base& starbase() const;
Vec2 squad_center() const;
```

### UI: `AttackRunScreen`

UIScreen subclass. Located at `include/neuroflyer/ui/screens/attack_run_screen.h` and `src/ui/screens/game/attack_run_screen.cpp`.

Mirrors `FighterDrillScreen` with these differences:

#### Squad Leader Inputs (all phases identical)

For each alive ship, every tick:

```
spacing = 0.0
aggression = 1.0
target_heading = normalized heading from ship to current starbase, scaled to [-1, 1]
target_distance = normalized distance to current starbase
center_heading = heading to squad center
center_distance = distance to squad center
```

Input vector built via `build_arena_ship_input()` with these squad inputs + sensor readings + recurrent state.

#### HUD

**Swarm mode (right panel):**
- Generation number
- Phase: "Attack 1", "Attack 2", "Attack 3"
- Phase progress bar (ticks elapsed / phase_duration_ticks)
- Tick counter, speed multiplier
- Camera mode
- Alive count / total
- Starbase HP (or "Destroyed" if HP <= 0)
- Top 10 fighters by score
- Controls help

**Follow mode (Best/Worst):**
- Compact info panel: generation, phase, progress, alive count
- Net viewer below with last inputs and recurrent state

#### Rendering

Same as fighter drill:
- Dark blue background
- Gray towers, gold tokens, yellow bullet squares
- Blue triangles for ships
- Red starbase circle with HP bar
- White crosshair at squad center
- All via camera transform

When starbase is destroyed mid-phase, it is not rendered until the next phase spawns a new one.

#### Evolution

Identical to fighter drill:
1. Session `is_over()` -> extract scores as fitness
2. `evolve_population()` with evo_config
3. Rebuild networks, reset recurrent states
4. Increment generation
5. Create new `AttackRunSession` with fresh seed

#### Camera & Controls

Same as fighter drill: Tab cycles Swarm/Best/Worst, arrow keys pan, scroll zooms, 1-4 speed, Space pauses, Escape exits.

### Pause Screen

Reuse `FighterDrillPauseScreen` directly. Its constructor takes:
- `population`, `generation`, `ship_design`, `genome_dir`, `variant_name`, `evo_config`, `on_resume` callback

All generic — works unchanged for Attack Runs.

### Entry Point: VariantViewerScreen

Add "Attack Runs" button in the Fighters tab, next to existing "Fighter Drill" button. Same launch pattern:

```cpp
case Action::AttackRuns:
    auto snap = load_snapshot(variant_path(sel));
    state.return_to_variant_view = true;
    ui.push_screen(std::make_unique<AttackRunScreen>(
        std::move(snap), vs_.genome_dir, sel.name));
```

### Tests: `attack_run_session_test.cpp`

Test cases for the engine:

1. **Phase transitions on timer** — verify 3 phases complete after 3 * phase_duration_ticks.
2. **Early phase transition on starbase destroy** — damage starbase to 0 HP, verify phase advances immediately.
3. **Starbase respawns each phase** — verify new random position per phase, minimum distance constraint.
4. **Scoring: travel toward target** — set ship velocity toward starbase, verify positive score increment.
5. **Scoring: bullet hit bonus** — hit starbase with bullet, verify attack_hit_bonus added.
6. **Scoring: token collection** — ship collides with token, verify token_bonus added.
7. **Scoring: death penalty** — ship collides with tower, verify death_penalty subtracted.
8. **Session ends after phase 3** — verify `is_over()` returns true.
9. **Three early destroys** — destroy all 3 starbases immediately, verify session completes in minimal ticks.

## File Layout

| Component | Header | Source |
|-----------|--------|--------|
| `AttackRunSession` | `include/neuroflyer/attack_run_session.h` | `src/engine/attack_run_session.cpp` |
| `AttackRunConfig` | (in attack_run_session.h) | — |
| `AttackRunScreen` | `include/neuroflyer/ui/screens/attack_run_screen.h` | `src/ui/screens/game/attack_run_screen.cpp` |
| Tests | — | `tests/attack_run_session_test.cpp` |

No new pause screen, views, or modals needed.

## Out of Scope

- Tokens respawning after collection (they stay collected, same as fighter drill)
- Variable phase count (fixed at 3)
- Time carryover between phases
- New sensor types or squad leader input formats
- Starbase shooting back at fighters

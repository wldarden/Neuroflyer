# ArenaWorld: Unified Simulation Layer

**Date:** 2026-04-07
**Branch:** feat/team-skirmish (will branch from)
**Scope:** Extract shared arena physics into a single authoritative `ArenaWorld` class

## Problem

Arena physics (ship movement, boundary wrapping, bullet lifecycle, collision detection, token/tower spawning) is duplicated across three session classes:

- `ArenaSession` — the original, most complete
- `FighterDrillSession` — copy-pasted from ArenaSession, ~10 identical functions
- `AttackRunSession` — copy-pasted from FighterDrillSession, ~10 identical functions

This means:
1. A physics bug must be fixed in 3 places
2. Behavior can silently diverge between sim types
3. New sim types require copy-pasting ~200 lines of physics code

## Design Principle

**The simulation world is always the same. Only the games played in it differ.**

ArenaWorld handles everything about how the physical world works: ships move, bullets fly, things collide. Game modes handle everything about what those events *mean*: scoring, phases, tournaments, evolution.

## ArenaWorld Class

### Responsibilities

ArenaWorld owns all entities and advances physics by one tick. It:

- Owns ships, bullets, towers, tokens, bases
- Handles ship movement (thrust + rotation model)
- Applies boundary rules (wrap or clamp)
- Spawns bullets from ships that want to shoot
- Updates bullet positions and range despawn
- Resolves all collisions (bullet-ship, bullet-base, bullet-tower, ship-tower, ship-token)
- Tracks team/squad assignments
- Reports events (kills, hits, pickups, deaths) via `TickEvents`

### What ArenaWorld does NOT do

- **Scoring** — game modes interpret `TickEvents`
- **Phase management** — expand/contract/attack, phase transitions, starbase respawning
- **Net execution** — building sensor inputs, running forward passes, decoding outputs (separate future refactor)
- **Evolution** — population management, mutation, selection
- **Tournament structure** — brackets, matchups, seeds
- **Match end conditions** — time limits, all-ships-dead checks. Game modes call `alive_count()` or `teams_alive()` and decide themselves.
- **Squad stats computation** — while `compute_squad_stats()` is on ArenaWorld (it queries entity positions), game modes decide when and why to call it

### Public API

```cpp
struct ArenaWorldConfig {
    // World geometry
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    bool wrap_ew = true;
    bool wrap_ns = true;

    // Teams & ships
    int team_count = 2;
    int squads_per_team = 1;
    int fighters_per_squad = 5;

    // Obstacles
    std::size_t tower_count = 20;
    std::size_t token_count = 10;

    // Bases
    float base_hp = 1000.0f;
    float base_radius = 100.0f;
    float base_bullet_damage = 10.0f;

    // Combat
    bool friendly_fire = false;
    float bullet_max_range = 400.0f;
    float bullet_speed = 8.0f;
    int fire_cooldown = 15;
    float ship_hp = 1.0f;
    float bullet_ship_damage = 1.0f;

    // Ship physics
    float thrust = 0.3f;
    float rotation_speed = 0.05f;
    float max_speed = 4.0f;

    // Spatial indexing (for NTM)
    float sector_size = 2000.0f;
    int ntm_sector_radius = 2;
};
```

```cpp
// Events reported by tick()
struct ShipKill    { std::size_t killer_idx; std::size_t victim_idx; };
struct BaseHit     { std::size_t shooter_idx; std::size_t base_idx; float damage; };
struct TowerKill   { std::size_t shooter_idx; std::size_t tower_idx; };
struct ShipDeath   { std::size_t ship_idx; };   // ship hit a tower
struct TokenPickup { std::size_t ship_idx; };

struct TickEvents {
    std::vector<ShipKill>    ship_kills;
    std::vector<BaseHit>     base_hits;
    std::vector<TowerKill>   tower_kills;
    std::vector<ShipDeath>   ship_deaths;
    std::vector<TokenPickup> token_pickups;
};
```

```cpp
class ArenaWorld {
public:
    ArenaWorld(const ArenaWorldConfig& config, uint32_t seed);

    // Reset for next generation/phase: re-randomizes towers/tokens, clears bullets,
    // resets tick counter. Does NOT reposition ships or bases — game modes do that
    // after reset() via mutable ships()/bases() accessors.
    void reset(uint32_t seed);

    // --- Per-tick interface ---
    void set_ship_actions(std::size_t idx, bool up, bool down,
                          bool left, bool right, bool shoot);
    TickEvents tick();

    // --- Entity access ---
    const std::vector<Triangle>& ships() const;
    std::vector<Triangle>& ships();             // for spawn positioning by game modes
    const std::vector<Bullet>& bullets() const;
    const std::vector<Tower>& towers() const;
    const std::vector<Token>& tokens() const;
    const std::vector<Base>& bases() const;
    std::vector<Base>& bases();                 // for starbase respawning by game modes

    // --- Queries ---
    std::size_t alive_count() const;
    std::size_t teams_alive() const;
    int team_of(std::size_t ship_idx) const;
    int squad_of(std::size_t ship_idx) const;
    uint32_t current_tick() const;
    const ArenaWorldConfig& config() const;
    SquadStats compute_squad_stats(int team, int squad) const;
};
```

### Tick Order

1. Update ship positions from velocity (`ship.x += ship.dx`, `ship.y += ship.dy`)
2. Apply boundary rules (wrap/clamp based on config)
3. Spawn bullets from ships wanting to shoot (rotation-aware nose position)
4. Update bullet positions via `update_directional()`, despawn at world boundaries
5. Resolve bullet-ship collisions (respects `friendly_fire` flag, tracks kills)
6. Resolve bullet-base collisions (respects team ownership)
7. Resolve bullet-tower collisions
8. Resolve ship-tower collisions (kills ship)
9. Resolve ship-token collisions
10. Decrement shoot cooldowns, increment survival ticks
11. Cleanup dead bullets (`std::erase_if`)
12. Increment tick counter

All collision detection uses functions from `collision.h` (already centralized).

## Config Migration

Each game-mode config gains an `ArenaWorldConfig world` member. Physics fields are removed from the game-mode config and read from `world` instead.

### ArenaConfig changes

**Moves to ArenaWorldConfig:**
- `world_width`, `world_height`, `wrap_ew`, `wrap_ns`
- `rotation_speed`, `bullet_max_range`
- `num_teams` (becomes `world.team_count`)
- `num_squads` (becomes `world.squads_per_team`)
- `fighters_per_squad`
- `tower_count`, `token_count`
- `base_hp`, `base_radius`, `base_bullet_damage`
- `friendly_fire`
- `ship_hp`, `bullet_ship_damage`
- `sector_size`, `ntm_sector_radius`

**Stays on ArenaConfig:**
- `time_limit_ticks`
- `rounds_per_generation`
- `fitness_weight_base_damage`, `fitness_weight_survival`, `fitness_weight_ships_alive`, `fitness_weight_tokens`
- `squad_leader_fighter_inputs` (static constexpr)

### FighterDrillConfig changes

**Moves to ArenaWorldConfig:**
- `world_width`, `world_height`, `wrap_ew`, `wrap_ns`
- `rotation_speed`, `bullet_max_range`
- `tower_count`, `token_count`
- `starbase_hp` (becomes `world.base_hp`)
- `starbase_radius` (becomes `world.base_radius`)
- `base_bullet_damage`

**Stays on FighterDrillConfig:**
- `population_size` (used to set `world.fighters_per_squad` during construction; `world.team_count=1`, `world.squads_per_team=1`)
- `starbase_distance`
- `phase_duration_ticks`
- `expand_weight`, `contract_weight`, `attack_travel_weight`, `attack_hit_bonus`
- `token_bonus`, `death_penalty`

### AttackRunConfig changes

Same as FighterDrillConfig, plus:
- `min_starbase_distance`, `travel_weight`, `attack_hit_bonus`, `bullet_cost` stay on AttackRunConfig

### SkirmishConfig changes

**Moves to ArenaWorldConfig:**
- `world_width`, `world_height`, `wrap_ew`, `wrap_ns`
- `rotation_speed`, `bullet_max_range`
- `num_squads_per_team` (becomes `world.squads_per_team`)
- `fighters_per_squad`
- `tower_count`, `token_count`
- `base_hp`, `base_radius`, `base_bullet_damage`
- `friendly_fire`
- `sector_size`, `ntm_sector_radius`

**Stays on SkirmishConfig:**
- `population_size`, `seeds_per_match`
- `time_limit_ticks`
- `kill_points`, `death_points`, `base_hit_points`

**Removed:** `to_arena_config()` — no longer needed. Game modes construct `ArenaWorld` directly from `config.world`.

### TeamSkirmishConfig

Already wraps `SkirmishConfig arena` — benefits transitively. No direct changes needed beyond what SkirmishConfig changes.

## Session Class Changes

### ArenaSession

Holds `ArenaWorld world_` member. Delegates all physics to it. Keeps:
- Match end condition logic (`is_over()` checks `world_.teams_alive()` and tick vs `time_limit_ticks`)
- Score accumulation from `TickEvents` using fitness weights
- `get_scores()` returning accumulated fitness
- Kill/death tracking (populated from `TickEvents`)
- Public API stays the same — callers don't know ArenaWorld exists

### FighterDrillSession

Holds `ArenaWorld world_` member (constructed with `team_count=1`, `squads_per_team=1`). Removes all duplicated physics helpers:
- `apply_boundary_rules()` — removed
- `spawn_bullets_from_ships()` — removed
- `update_bullets()` — removed
- `resolve_ship_tower_collisions()` — removed
- `resolve_ship_token_collisions()` — removed
- `resolve_bullet_starbase_collisions()` — removed
- `resolve_bullet_tower_collisions()` — removed
- `spawn_obstacles()` — removed (ArenaWorld constructor handles it)

Keeps:
- `spawn_ships()` — custom spawn positioning (squad center with random rotations)
- `spawn_starbase()` — custom starbase placement (fixed distance at random angle)
- `compute_phase_scores()` — phase-specific velocity-based scoring
- Phase management (Expand → Contract → Attack → Done)
- `tick()` becomes: `auto events = world_.tick(); compute_phase_scores(); accumulate_token_scores(events);`

### AttackRunSession

Same pattern as FighterDrillSession. Additionally keeps:
- `advance_phase()` — phase transitions with starbase respawning
- `spawn_starbase()` — random distance within min/max bounds
- Bullet cost scoring

### ArenaMatch

`run_arena_match()` currently constructs an `ArenaSession`. It will continue to do so — ArenaSession now internally uses ArenaWorld.

### SkirmishTournament / TeamSkirmishSession

These use `ArenaSession` internally. They benefit transitively — no direct changes to their arena usage beyond config construction.

## New Files

| File | Purpose |
|------|---------|
| `include/neuroflyer/arena_world.h` | ArenaWorld class, ArenaWorldConfig, TickEvents structs |
| `src/engine/arena_world.cpp` | Single authoritative physics implementation |

## Modified Files

| File | Change |
|------|--------|
| `include/neuroflyer/arena_config.h` | Add `ArenaWorldConfig world` member, remove migrated fields |
| `include/neuroflyer/arena_session.h` | Add `ArenaWorld world_` member, remove physics state/helpers |
| `src/engine/arena_session.cpp` | Delegate physics to `world_`, accumulate scores from `TickEvents` |
| `include/neuroflyer/fighter_drill_session.h` | Add `ArenaWorldConfig world` to FighterDrillConfig, add `ArenaWorld` member, remove physics helpers |
| `src/engine/fighter_drill_session.cpp` | Remove 10 duplicated functions, delegate to `world_` |
| `include/neuroflyer/attack_run_session.h` | Same as FighterDrillSession |
| `src/engine/attack_run_session.cpp` | Same as FighterDrillSession |
| `include/neuroflyer/skirmish.h` | Add `ArenaWorldConfig world` to SkirmishConfig, remove migrated fields, remove `to_arena_config()` |
| `src/engine/skirmish.cpp` | Update ArenaSession construction to use `config.world` |
| `include/neuroflyer/skirmish_tournament.h` | Update for new config shape |
| `src/engine/skirmish_tournament.cpp` | Update ArenaSession construction |
| `include/neuroflyer/team_skirmish.h` | Transitively updated via SkirmishConfig |
| `src/engine/team_skirmish.cpp` | Update ArenaSession construction |
| `src/engine/arena_match.cpp` | Update ArenaSession construction |
| `tests/arena_session_test.cpp` | Update config construction |
| `tests/attack_run_session_test.cpp` | Update config construction |
| `tests/skirmish_test.cpp` | Update config construction |
| `tests/squad_leader_test.cpp` | Update config construction |
| `tests/team_evolution_test.cpp` | Update config construction |

## UI Screen Impact

**No functional changes to UI screens.** The session classes maintain their existing public APIs. Screens continue to call `set_ship_actions()`, `tick()`, `ships()`, `bullets()`, etc. through the session layer.

Config construction in config screens will update field paths (e.g., `config.world_width` becomes `config.world.world_width`), but the UI screens that drive simulation (`ArenaGameScreen`, `FighterDrillScreen`, `AttackRunScreen`, `SkirmishScreen`, `TeamSkirmishScreen`) call session APIs that don't change.

## Testing Strategy

1. **New test file: `tests/arena_world_test.cpp`** — tests ArenaWorld physics in isolation: boundary wrapping, bullet lifecycle, all collision types, TickEvents correctness
2. **Existing tests updated** — config construction changes only, test logic stays the same (verifies behavioral equivalence)
3. **Build and run full test suite** after each session class migration to catch regressions immediately

## Future Work (Not In This Refactor)

- **Move net execution from UI screens into engine** — ArenaWorld provides the simulation, a future `SimRunner` or similar handles the neural net tick loop. Currently 4 of 6 sim types run net execution in UI screen code.
- **Consolidate FighterDrillSession and AttackRunSession** — with shared physics extracted, these become very thin (phase logic + scoring only). Could merge into a single `DrillSession` parameterized by phase behavior.

# Arena Mode Design

**Date:** 2026-03-28
**Status:** Approved

## Overview

A new game mode for NeuroFlyer where ships exist in a large 2D arena and battle each other in teams. Uses the same neural net engine, evolution pipeline, genome management, and UI framework as the existing scroller mode.

## Core Concept

The scroller mode is a vertical corridor with auto-scrolling — ships dodge towers and collect tokens independently. Arena mode is a large open 2D world where all ships share one instance, can rotate freely, and fight each other.

Key differences from scroller:
- Large rectangular world (configurable, default 64 * SCREEN_W by 64 * SCREEN_H where SCREEN_W/H are the scroller viewport dimensions)
- No auto-scroll — ships navigate freely
- Ships rotate and thrust in their facing direction
- All ships exist in one shared world (not separate game sessions)
- N teams of X members, configurable (free-for-all = N teams of 1)
- Round ends on time limit or last team standing
- Friendly fire enabled

## Approach: Shared Entity Layer, Separate Orchestrators

Shared entity types (`Triangle`, `Tower`, `Token`, `Bullet`) and shared logic (collision detection, entity updates) live in a common layer. `GameSession` (scroller) and `ArenaSession` (arena) are separate orchestrators that compose these shared pieces differently. No class hierarchy — the two modes share data types and free functions, not behavior contracts.

## Movement Model

Same 5 neural net outputs, reinterpreted for arena:

| Output | Scroller | Arena |
|--------|----------|-------|
| Left | Move left | Rotate counter-clockwise |
| Right | Move right | Rotate clockwise |
| Up | Move up | Thrust forward (facing direction) |
| Down | Move down | Thrust reverse |
| Shoot | Fire upward | Fire in facing direction |

Existing genomes are structurally compatible (same output count), though they'd need retraining for arena behavior.

## Entity Changes

### Triangle (ship)

New fields:
- `float rotation = 0.0f` — radians, 0 = facing up, clockwise positive
- `float rotation_speed` — configurable turn rate

`apply_actions()` behavior depends on game mode:
- Scroller: unchanged (left/right/up/down translate position)
- Arena: left/right modify rotation, up/down apply thrust in facing direction

`update()` in arena mode: position += velocity based on facing angle.

Bullet spawn direction uses `rotation` instead of hardcoded "up."

### Bullet

New fields:
- `float dx, dy` — directional velocity (replaces hardcoded upward travel)
- `float distance_traveled` — tracks range for max-range cutoff
- `int owner_index` — index of the ship that fired this bullet (for skip-self-hit checks)

Bullets do NOT wrap regardless of world wrap settings. Bullets have a configurable max range and are destroyed when they reach it or hit a world boundary.

### Tower, Token

Unchanged. Placed at round start (not continuously spawning like in scroller).

## Collision

Collision helpers extracted from `GameSession` into free functions:
- `point_in_circle(px, py, cx, cy, r)` — already exists
- `triangle_circle_collision(tri, cx, cy, r)` — ship vertices vs circle
- `bullet_circle_collision(bullet, cx, cy, r)` — bullet point vs circle
- `bullet_triangle_collision(bullet, tri)` — new: bullet vs ship (for PvP)

Bullet-vs-ship checks skip self (ship can't shoot itself). Friendly fire is allowed (teammates can hit each other).

## ArenaSession

New class that orchestrates one shared world with all ships.

### Owns

- `std::vector<Triangle> ships` — all ships, indexed by population order
- `std::vector<Tower> towers` — static obstacles, placed at round start
- `std::vector<Token> tokens` — collectibles, placed at round start
- `std::vector<Bullet> bullets` — all bullets from all ships, pooled
- `std::vector<int> team_assignments` — maps ship index to team ID
- `ArenaConfig config`

### ArenaConfig

```
world_width, world_height       — arena dimensions in pixels (default 64 * SCREEN_W, 64 * SCREEN_H)
time_limit_ticks                — max round length
num_teams                       — N
team_size                       — X (population = N * X)
rounds_per_generation           — default 1
tower_count, token_count        — obstacle density
bullet_max_range                — max travel distance before despawn
wrap_ns                         — North/South wrap (boolean, default true)
wrap_ew                         — East/West wrap (boolean, default true)
rotation_speed                  — how fast ships turn (radians/tick, default ~0.05)
```

### Ship Initial Facing

Ships spawn facing toward the center of the arena. This naturally creates an "everyone charges in" dynamic at round start.

### tick()

1. For each alive ship: apply neural net outputs (rotate, thrust, shoot)
2. Update all ship positions (wrap or clamp per edge pair)
3. Update all bullets (move in dx/dy direction, increment distance_traveled, clamp at world boundary, destroy at max range)
4. Collision: bullets vs ships (skip self-hits), bullets vs towers
5. Collision: ships vs towers (ship dies)
6. Collision: ships vs tokens (token collected, ship scores)
7. Increment survival tick counters per ship
8. Check end conditions: time limit OR one team (or zero) remains

### Spawn Layout

Ships spawn in a ring around the arena center:
- Inner boundary: 1/3 of arena radius from center
- Outer boundary: 2/3 of arena radius from center
- "Radius" = half the shorter world dimension

Each team gets an equal pie slice of the ring. For N teams, each slice spans 360/N degrees. Ships spawn at random positions within their team's slice of the ring.

Examples:
- 2 teams: two 180-degree half-ring slices
- 4 teams: four 90-degree quarter slices
- 100 teams (FFA): 100 narrow slices, ships form a ring

Towers and tokens are placed randomly across the entire arena (no restriction to spawn ring).

### Scoring

Pluggable scoring rules. V1: ships earn 1 point per second alive (survival time).

Per-ship scores exposed for fitness assignment after the round. Future rules: kills, tokens collected, objectives captured/defended.

## World Boundaries

Per-edge-pair wrap configuration:
- `wrap_ns`: if true, ships exiting the north edge appear at south (and vice versa). If false, ships clamp at the boundary.
- `wrap_ew`: if true, ships exiting east appear at west (and vice versa). If false, clamp.
- Default: both true.
- Bullets never wrap regardless of settings — they are destroyed at world boundaries.

## Camera System

New camera for arena (scroller has no camera).

### Camera struct

```
float x, y           — world position of camera center
float zoom           — 1.0 = default, <1.0 = zoomed out, >1.0 = zoomed in
bool following       — true = locked onto a ship
int follow_index     — which ship to track
```

### Behavior

- **Follow mode** (default): camera center = followed ship's position. Tab cycles forward through alive ships, Shift+Tab backward. If followed ship dies, auto-advance to next alive ship.
- **Free mode**: arrow keys set `following = false` and pan the camera. Tab re-engages follow on next alive ship.
- **Zoom**: mouse scroll (when over arena view) or +/- keys. Works in both modes. Clamped between min (see whole arena) and max (close-up).
- **Edge clamping**: camera is clamped so it never shows past the world boundary. When zoomed out and following a ship near an edge, the camera won't center on the ship. In this case, a circle indicator is drawn around the followed ship so the user can still identify it.

### World-to-screen transform

```
screen_x = (world_x - camera.x) * zoom + viewport_w / 2
screen_y = (world_y - camera.y) * zoom + viewport_h / 2
```

All rendering in `ArenaGameView` uses this transform.

## Evolution Integration

### Round lifecycle

1. Build population from `Individual` vector (same as scroller)
2. Compile networks, init recurrent states (same as scroller)
3. Create `ArenaSession`: assign teams randomly, place ships in spawn ring, scatter towers/tokens
4. Each tick: `build_ship_input()` per alive ship, `network.forward()`, `decode_output()`, `arena.tick()`
5. Round ends (time limit or one team left)
6. If more rounds remaining: reset arena (new random teams, new positions, keep cumulative scores), go to step 3
7. After all N rounds: `arena.get_scores()` → assign to `Individual.fitness`
8. `do_evolution()` — existing pipeline, unchanged
9. Next generation: go to step 1

### Single shared population (v1)

All ships come from one population. Teams are randomly assigned each generation. After the round, everyone is ranked by individual score and evolves together.

### Sensor input (v1)

`build_ship_input()` works as-is — ships sense towers and tokens. Position inputs normalized against world size. Speed input = ship's velocity magnitude. Ships cannot see each other in v1 — inter-ship sensing is deferred to the sensor rework.

## UI Integration

### New screens and views

**ArenaConfigScreen** — pushed from hangar when user selects "Train: Arena"
- Contains `ArenaConfigView` with controls:
  - Team count, team size
  - World size (W x H)
  - Time limit
  - Rounds per generation
  - Tower count, token count
  - Bullet max range
  - N/S wrap toggle (default true), E/W wrap toggle (default true)
  - Map type dropdown (just "Random" for v1)
- "Start" button pushes `ArenaGameScreen`

**ArenaGameScreen** — main arena screen
- Same split layout as `FlySessionScreen`: 60% game, 40% info
- Left panel: `ArenaGameView`
- Right panel: `NetViewerView` (existing) when a ship is selected
- Right panel: `ArenaGameInfoView` when no ship is selected
- Owns `Camera` and handles input
- Manages round lifecycle

**ArenaGameView** — arena renderer
- Static starfield background (no scroll)
- Renders all entities through camera transform
- Ships rendered with rotation (rotated triangle sprites)
- Team-colored ships
- Followed ship gets a circle indicator (especially important when camera is edge-clamped and can't center on ship)

**ArenaGameInfoView** — stats panel
- Round timer, teams remaining, ships alive per team, top scores
- Placeholder/blank for v1

### Navigation flow

```
Hangar Screen
  └─ Select genome/variant → "Train: Arena"
      └─ ArenaConfigScreen (new)
          └─ ArenaGameScreen (new)
              ├─ Left: ArenaGameView (new)
              ├─ Right: NetViewerView (existing) — when ship selected
              ├─ Right: ArenaGameInfoView (new) — when no ship selected
              └─ Pause → PauseConfigScreen (existing)
```

### Keyboard controls

| Key | Action |
|-----|--------|
| Tab / Shift+Tab | Cycle followed ship (re-engages follow mode) |
| Arrow keys | Free camera pan (breaks follow mode) |
| Mouse scroll (over arena) | Zoom in/out |
| +/- | Zoom in/out |
| 1-4 | Speed control (1x / 5x / 20x / 100x) |
| Space | Pause (push PauseConfigScreen) |
| Escape | Quit |

## File Changes

### Modified existing files

- `game.h` / `game.cpp` — Add `rotation`, `dx`/`dy` to `Triangle` and `Bullet`. Extract collision helpers into free functions.
- `config.h` — Add `GameMode` enum (Scroller, Arena).
- `evolution.h` / `evolution.cpp` — Arena-mode variant of `tick_individual()` that works with `ArenaSession&`.
- `sensor_engine.h` / `sensor_engine.cpp` — `build_ship_input()` normalizes position against world size for arena. Speed input = velocity magnitude.
- Hangar screen — Add "Train: Arena" button.

### New files

- `arena_config.h` — `ArenaConfig` struct
- `arena_session.h` / `arena_session.cpp` — `ArenaSession` class
- `camera.h` — `Camera` struct and transform helpers
- `screens/arena/arena_config_screen.h` / `.cpp`
- `screens/arena/arena_game_screen.h` / `.cpp`
- `ui/views/arena_config_view.h` / `.cpp`
- `ui/views/arena_game_view.h` / `.cpp`
- `ui/views/arena_game_info_view.h` / `.cpp`

### Unchanged

- `GameSession` (scroller works exactly as before)
- UI framework (`UIManager`, `UIScreen`, `UIView`, `UIWidget`)
- `PauseConfigScreen` (reused)
- `Snapshot` / `GenomeManager` (genomes don't know about game modes)
- `NetViewerView` (reused)
- `neuralnet`, `evolve`, `neuralnet-ui` libraries

## Deferred Features

- **Per-team evolution pools** — coevolution, arms races, emergent team strategies. Rich area for future exploration.
- **Arena-specific sensor types** — detect other ships, bullets, allies. Requires sensor rework.
- **Health/HP system** — multi-hit ships. Simple to add later.
- **Designed map layouts** — walls of towers, enclosed objectives, etc. Users select map on config screen.
- **Minimap** — small overview of full arena.
- **Objective-based scoring** — capture points, defend buildings, collect enclosed tokens.

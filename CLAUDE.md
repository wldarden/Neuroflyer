# NeuroFlyer

A neural net playground with two game modes: a vertical-scrolling arcade where 100 neural nets learn to dodge towers and collect tokens, and a top-down arena where teams of ships with hierarchical brains (NTM + squad leader + fighter) battle for territory. Features neuroevolution, real-time brain visualization, and a genome management hangar.

## Repository

- **GitHub:** [wldarden/Neuroflyer](https://github.com/wldarden/Neuroflyer)
- **Dependencies:** [neuralnet](https://github.com/wldarden/neuralnet), [neuralnet-ui](https://github.com/wldarden/neuralnet-ui), [evolve](https://github.com/wldarden/evolve) (expected at `../libs/`)

## Architecture

NeuroFlyer is an independent repo. It depends on the `neuralnet`, `neuralnet-ui`, and `evolve` library repos (expected at `../libs/` in the workspace). No dependency on EcoSim or AntSim.

The app uses a **4-layer UI architecture** managed by `UIManager`:

1. **Screen** (UIScreen) — major app contexts (MainMenu, Hangar, Game). Managed as a stack (push/pop).
2. **View** (UIView) — panels/regions within a screen with layout bounds.
3. **Modal** (UIModal) — temporary overlays (ConfirmModal, InputModal, NodeEditorModal). Rendered on top of screens.
4. **Widget** — `ui::` namespace free functions for consistent styled controls.

```
neuroflyer/
├── include/neuroflyer/
│   ├── ui/                       — UI framework + all UI headers
│   │   ├── ui_manager.h          — Screen stack, modal stack, resolution, main draw loop
│   │   ├── ui_screen.h           — Base class: on_enter/on_exit/on_resize/on_draw
│   │   ├── ui_view.h             — Base class: bounds-aware panel within a screen
│   │   ├── ui_modal.h            — Base class: overlay with optional input blocking
│   │   ├── ui_widget.h           — ui:: namespace styled controls
│   │   ├── legacy_screen.h       — Bridge for legacy Screen enum (being phased out)
│   │   ├── screens/              — One header per screen (subclass UIScreen)
│   │   │   ├── main_menu_screen.h
│   │   │   ├── hangar_screen.h
│   │   │   ├── create_genome_screen.h
│   │   │   ├── variant_viewer_screen.h
│   │   │   ├── lineage_tree_screen.h
│   │   │   ├── fly_session_screen.h
│   │   │   ├── pause_config_screen.h
│   │   │   ├── settings_screen.h
│   │   │   ├── arena_config_screen.h
│   │   │   ├── arena_game_screen.h
│   │   │   ├── arena_pause_screen.h
│   │   │   ├── fighter_drill_screen.h
│   │   │   ├── fighter_drill_pause_screen.h
│   │   │   ├── attack_run_screen.h
│   │   │   ├── skirmish_config_screen.h
│   │   │   ├── skirmish_screen.h
│   │   │   ├── skirmish_pause_screen.h
│   │   │   ├── team_skirmish_config_screen.h
│   │   │   ├── team_skirmish_screen.h
│   │   │   └── team_skirmish_pause_screen.h
│   │   ├── views/                — Reusable panels (subclass UIView)
│   │   │   ├── topology_preview_view.h
│   │   │   ├── net_viewer_view.h
│   │   │   ├── test_bench_view.h
│   │   │   ├── arena_config_view.h
│   │   │   ├── arena_game_view.h
│   │   │   └── arena_game_info_view.h
│   │   └── modals/               — Popup dialogs (subclass UIModal)
│   │       ├── confirm_modal.h
│   │       ├── editor_modal.h
│   │       ├── input_modal.h
│   │       ├── layer_editor_modal.h
│   │       └── node_editor_modal.h
│   ├── components/               — Reusable draw functions (callback-based, UI-framework-agnostic)
│   │   ├── net_viewer.h          — Neural net viewer/editor
│   │   ├── test_bench.h          — Sensor testing
│   │   ├── lineage_graph.h       — Ancestry tree visualization
│   │   ├── fitness_editor.h      — Scoring parameter editor
│   │   ├── pause_training.h      — Training tab content
│   │   ├── pause_evolution.h     — Evolution settings tab content
│   │   └── structural_heatmap.h  — Structural analysis heatmap
│   ├── renderers/
│   │   └── variant_net_render.h  — build_variant_net_config(): ShipDesign + NetType -> configured net render (Solo/Fighter/SquadLeader/NTM label schemes)
│   ├── app_state.h               — AppState, GenStats, dirty flags, go_to_screen() (legacy)
│   ├── config.h                  — GameConfig: scoring, population, ship, world params
│   ├── constants.h               — SCREEN_W, SCREEN_H
│   ├── collision.h               — Collision math: ray_circle_intersect, point_in_circle, bullet/triangle/circle collisions (rotated and non-rotated variants for arena)
│   ├── game.h                    — Tower, Token, Bullet (directional with max range), Triangle (rotation + arena thrust model), GameSession
│   ├── evolution.h               — Individual (StructuredGenome), EvolutionConfig, population ops
│   ├── renderer.h                — SDL2 split-screen renderer + deferred draw queue
│   ├── sensor_engine.h           — Scroller sensor source of truth: query_sensor, build_ship_input, decode_output
│   ├── arena_world.h             — ArenaWorld: unified physics layer (entities, tick, collisions, TickEvents), ArenaWorldConfig, SquadStats
│   ├── arena_config.h            — ArenaConfig: ArenaWorldConfig world + game-mode fields (time limit, fitness weights)
│   ├── arena_session.h           — ArenaSession: thin wrapper around ArenaWorld + scoring + end conditions
│   ├── arena_match.h             — ArenaMatch: multi-generation match runner for team evolution
│   ├── arena_sensor.h            — Arena sensor system: rotation-aware query_arena_sensor, build_arena_ship_input, ArenaQueryContext
│   ├── fighter_drill_session.h   — FighterDrillSession, FighterDrillConfig, DrillPhase enum
│   ├── attack_run_session.h      — AttackRunSession, AttackRunConfig, AttackRunPhase enum
│   ├── skirmish.h                — SkirmishConfig, SkirmishMatchResult, run_skirmish_match()
│   ├── skirmish_tournament.h     — SkirmishTournament: elimination bracket with per-tick stepping
│   ├── team_skirmish.h           — TeamSkirmishConfig, TeamPool, TeamSkirmishSession, co-evolution mode
│   ├── base.h                    — Base struct: team starbase with position, health, team_id
│   ├── camera.h                  — Camera struct: pan/zoom for arena viewport
│   ├── sector_grid.h             — SectorGrid: spatial index for NTM threat queries
│   ├── squad_leader.h            — NTM, OrderType enums, squad leader execution functions
│   ├── team_evolution.h          — TeamIndividual (3 nets: NTM + squad leader + fighter), team evolution ops
│   ├── ship_design.h             — ShipDesign, SensorDef (uint16_t id, is_full_sensor), EvolvableFlags, NodeStyle
│   ├── snapshot.h                — Snapshot, SnapshotHeader, NetType enum (Solo/Fighter/SquadLeader/NTM), GenomeInfo
│   ├── snapshot_io.h             — Binary save/load (NFS format v1-v7, CRC32)
│   ├── snapshot_utils.h          — snapshot_to_individual() conversion
│   ├── genome_manager.h          — Genome/variant filesystem + cross-genome lineage
│   ├── mrca_tracker.h            — Elite lineage tracking with pruning + degradation
│   ├── name_validation.h         — Filesystem-safe name validation
│   ├── ray.h                     — ray_range_multiplier (visualization legacy)
│   └── paths.h                   — data_dir(), asset_dir(), format_short_date()
├── src/
│   ├── engine/                   — Pure logic (ZERO SDL/ImGui deps)
│   │   ├── game.cpp              — Scroller game mechanics, collision, spawning, scoring
│   │   ├── evolution.cpp         — StructuredGenome evolution, topology mutations
│   │   ├── config.cpp            — GameConfig JSON save/load
│   │   ├── snapshot_io.cpp       — Binary I/O for snapshots (NFS v1-v7)
│   │   ├── genome_manager.cpp    — Filesystem ops + lineage.json + genomic_lineage.json
│   │   ├── mrca_tracker.cpp      — Elite lineage tracking
│   │   ├── sensor_engine.cpp     — Scroller sensor detection + input encoding + display helpers
│   │   ├── arena_world.cpp       — ArenaWorld: single authoritative physics (movement, collisions, bullet lifecycle)
│   │   ├── arena_tick.cpp        — tick_fighters_scripted() + tick_arena_with_leader(): centralized net execution
│   │   ├── arena_session.cpp     — ArenaSession: scoring wrapper around ArenaWorld
│   │   ├── arena_match.cpp       — Multi-generation arena match runner
│   │   ├── arena_sensor.cpp      — Rotation-aware arena sensors + fighter input builder
│   │   ├── fighter_drill_session.cpp — Drill world simulation, phase scoring
│   │   ├── attack_run_session.cpp — Attack Runs drill: 3 attack phases with starbase targets
│   │   ├── skirmish.cpp          — Skirmish match runner (blocking, kill-based scoring)
│   │   ├── skirmish_tournament.cpp — Elimination bracket tournament engine
│   │   ├── team_skirmish.cpp     — Team skirmish: per-ship nets, match runner, session orchestrator
│   │   ├── sector_grid.cpp       — Spatial indexing for NTM queries
│   │   ├── squad_leader.cpp      — NTM execution, squad leader net, order decoding
│   │   ├── team_evolution.cpp    — Team-level evolution (3-net TeamIndividual)
│   │   ├── ray.cpp               — Ray-circle intersection (visualization only)
│   │   └── paths.cpp             — File path utilities
│   ├── ui/                       — SDL/ImGui rendering and interaction
│   │   ├── main.cpp              — SDL init, UIManager setup, main loop
│   │   ├── renderer.cpp          — Game panel, fly mode orchestration, deferred draws
│   │   ├── asset_loader.cpp      — Texture loading (ships, asteroids, coins, stars, bg)
│   │   ├── framework/            — UI framework implementations
│   │   │   ├── ui_manager.cpp    — Screen/modal stack management
│   │   │   ├── ui_widget.cpp     — Styled widget implementations
│   │   │   └── modals/           — Generic modal implementations
│   │   │       ├── confirm_modal.cpp
│   │   │       ├── editor_modal.cpp
│   │   │       └── input_modal.cpp
│   │   ├── modals/               — App-specific modal implementations
│   │   │   ├── node_editor_modal.cpp
│   │   │   └── layer_editor_modal.cpp
│   │   ├── screens/              — Screen implementations (mirror include/ui/screens/)
│   │   │   ├── menu/             — Main menu + settings
│   │   │   │   ├── main_menu_screen.cpp
│   │   │   │   └── settings_screen.cpp
│   │   │   ├── hangar/           — Genome/variant management screens
│   │   │   │   ├── hangar_screen.cpp
│   │   │   │   ├── create_genome_screen.cpp
│   │   │   │   ├── variant_viewer_screen.cpp
│   │   │   │   └── lineage_tree_screen.cpp
│   │   │   ├── game/             — Training/flying screens
│   │   │   │   ├── fly_session_screen.cpp
│   │   │   │   ├── pause_config_screen.cpp
│   │   │   │   ├── fighter_drill_screen.cpp
│   │   │   ├── fighter_drill_pause_screen.cpp
│   │   │   ├── attack_run_screen.cpp
│   │   │   ├── skirmish_config_screen.cpp
│   │   │   ├── skirmish_screen.cpp
│   │   │   ├── skirmish_pause_screen.cpp
│   │   │   ├── team_skirmish_config_screen.cpp
│   │   │   ├── team_skirmish_screen.cpp
│   │   │   └── team_skirmish_pause_screen.cpp
│   │   │   ├── arena/            — Arena mode screens
│   │   │   │   ├── arena_config_screen.cpp
│   │   │   │   ├── arena_game_screen.cpp
│   │   │   │   └── arena_pause_screen.cpp
│   │   │   └── analysis/         — Analysis tools
│   │   │       └── analysis.cpp
│   │   ├── views/                — View implementations (reusable panels)
│   │   │   ├── net_viewer.cpp          — Neural net viewer (legacy component wrapper)
│   │   │   ├── net_viewer_view.cpp     — UIView wrapper for net viewer
│   │   │   ├── test_bench.cpp          — Sensor test bench (legacy component wrapper)
│   │   │   ├── test_bench_view.cpp     — UIView wrapper for test bench
│   │   │   ├── topology_preview_view.cpp — Net topology preview panel
│   │   │   ├── lineage_graph.cpp       — Lineage tree visualization
│   │   │   ├── fitness_editor.cpp      — Scoring parameter sliders
│   │   │   ├── pause_training.cpp      — Training tab content
│   │   │   ├── pause_evolution.cpp     — Evolution settings tab content
│   │   │   ├── structural_heatmap.cpp  — Structural analysis heatmap
│   │   │   ├── arena_config_view.cpp   — Arena configuration panel
│   │   │   ├── arena_game_view.cpp     — Arena world renderer (SDL)
│   │   │   └── arena_game_info_view.cpp — Arena HUD (team stats, scores)
│   │   └── renderers/            — SDL rendering helpers
│   │       ├── variant_net_render.cpp  — NeuroFlyer-specific net render wrapper
│   │       ├── starfield.cpp           — Parallax star background
│   │       └── occulus.cpp             — Ellipse sensor visualization
├── tests/                        — GoogleTests (25 files: scroller, arena, drill, attack runs, skirmish, evolution, snapshot, collision, sensors)
├── assets/                       — Sprites (ships, asteroids, coins, stars, hangar bg)
└── docs/
    ├── backlog.md                — Feature backlog (keep updated!)
    ├── feature-audit.md          — System-by-system status audit
    ├── arena-mode-backlog.md     — Arena-specific backlog items
    └── superpowers/              — Design specs and implementation plans
```

## Screen Flow

```
MainMenuScreen → push(FlySessionScreen) → push(PauseConfigScreen)
               │                           └── tabs: Training, Evolution, Analysis, Save Variants
               → push(SettingsScreen)
               → push(HangarScreen) → push(CreateGenomeScreen)
               │                     → push(VariantViewerScreen)
               │                        → embeds: NetViewerView, TestBenchView
               │                        → push(VariantNetEditorScreen) (zoomable net editor)
               │                        → push(LineageTreeScreen)
               │                        → push(FlySessionScreen) (training)
               │                        → push(FighterDrillScreen) (fighter drill training)
               │                        │  → Space: push(FighterDrillPauseScreen)
               │                        │     → tabs: Evolution, Save Variants
               │                        → push(AttackRunScreen) (attack runs drill)
               │                        │  → Space: push(FighterDrillPauseScreen)
               │                        │     → tabs: Evolution, Save Variants
               │                        → push(SkirmishConfigScreen) (squad skirmish config)
               │                           → push(SkirmishScreen) (squad skirmish tournament)
               │                              → Space: push(SkirmishPauseScreen)
               │                              │  → tabs: Evolution, Save Variants (squad+NTM pairs)
               │                              → Tab: cycle through individual fighters
               │                              → F: toggle Fighter/SquadLeader net view
               │                              → Target Viz: gold heading line + target circle + squad center markers
               → push(ArenaConfigScreen) → push(ArenaGameScreen)
               │                          → Space: push(ArenaPauseScreen)
               │                          │   → Save squad leader + NTM variants
               │                          → follow mode: net viewer (Fighter/SquadLeader toggle)
               → push(TeamSkirmishConfigScreen) → push(TeamSkirmishScreen)
                                                   → Space: push(TeamSkirmishPauseScreen)
                                                   │   → tabs: Evolution, Save Fighters (per team), Save Squad Leaders (per team)
                                                   → Tab: cycle through individual fighters
                                                   → F: toggle Fighter/SquadLeader net view
```

**Navigation:** `UIManager::push_screen()` / `pop_screen()` / `replace_screen()`. Screens subclass `UIScreen` and implement `on_draw(AppState&, Renderer&, UIManager&)`. Components take specific data + callbacks (not AppState), so they're reusable.

**Modals:** `UIManager::push_modal()` / `pop_modal()`. Rendered on top of all screens. Used for: confirm dialogs, text input, node editor, layer editor. Escape closes the top modal first.

**Legacy bridge:** `go_to_screen()` and `legacy_screen.h` still exist during migration — UIManager syncs legacy Screen enum changes. Being phased out in favor of direct push/pop.

## Neural Net

- **Inputs:** Configurable via sensor genes (rays or occulus ovals) + position + recurrent memory
- **Hidden:** Evolving topology — layers and nodes mutate
- **Outputs:** 5 action outputs (UP, DOWN, LEFT, RIGHT, SHOOT) + optional recurrent memory outputs. Squad leader nets use 5 outputs differently: 3 tactical orders + 2 spacing orders via argmax.
- **Genome:** `StructuredGenome` with weight genes + sensor genes + topology genes
- **Recurrent memory:** Hidden layer activations feed back as extra inputs next tick
- **NetType:** `NetType` enum (Solo/Fighter/SquadLeader/NTM) stored in snapshots, determines which input/output labels the net viewer displays

## Genome Management (Hangar)

Genomes are stored as directories in `data/genomes/{name}/`:
- `genome.bin` — root snapshot (NFS format v2, CRC32)
- `{variant name}.bin` — variant snapshots (same format)
- `lineage.json` — per-genome ancestry tree
- `data/genomes/genomic_lineage.json` — cross-genome lineage (genome-to-genome promotion links)

Each `.bin` file is self-describing: contains ShipDesign (sensors, memory), network topology, weights, parent link. Binary format versioned (v1 = original, v2 = evolvable flags, v3 = activation genes, v4 = parent name, v5 = run count, v6 = paired fighter name, v7 = NetType). Squad leader and NTM variants are saved to `data/genomes/{name}/squad/`.

The hangar lets users browse genomes, create new ones with custom sensor/topology configs, view/promote/delete variants, launch training runs, and view cross-genome lineage trees. Deleting a genome relinks child genomes to the grandparent.

**Variant saving from training:** The pause screen's "Save Variants" tab saves selected individuals with correct lineage. Elite individuals are saved via `save_elite_variants_with_mrca()` which computes the MRCA tree from the `MrcaTracker`, creates MRCA stub entries in lineage.json, and sets each variant's parent to the correct branch point. Non-elite individuals use `training_parent_name` directly. The `individual_hash()` function (in `evolution.h`) produces unique IDs for MRCA dedup.

## Sensor Engine

### Scroller Sensors (`sensor_engine.h`)

`sensor_engine.h` is the **single source of truth** for scroller sensor detection and input encoding:
- `query_sensor(SensorDef, ship_pos, towers, tokens)` — dispatches Raycast (ray-circle intersection) or Occulus (ellipse overlap)
- `build_ship_input(ShipDesign, ...)` — builds complete neural net input vector. Iterates sensors in ShipDesign order (stable IDs).
- `decode_output(output, memory_slots)` — extracts actions + memory from net output
- `compute_sensor_shape(SensorDef, ship_pos)` — derives ellipse geometry (shared by detection AND rendering)
- `build_input_labels/colors/display_order(ShipDesign)` — for net panel display
- `query_sensors_with_endpoints(ShipDesign, ...)` — sensor positions for visualization
- `build_squad_leader_input_labels()` — 14 strategic input labels for squad leader nets
- `build_squad_leader_output_labels()` — 5 tactical/spacing output labels for squad leader nets

**All scroller consumers** (fly mode, test bench, headless) call these functions. No inline detection math anywhere else.

Sensor parameters (angle, range, width) are per-variant via `ShipDesign` stored in each snapshot. They are evolvable via `EvolvableFlags` on the design.

`SensorDef` fields include `is_full_sensor` (bool) — when true, the sensor produces 5 input values in arena mode (distance, is_tower, is_token, is_friend, is_bullet) instead of a single distance value. This gives arena fighters entity-type awareness. Does not affect scroller mode sensors.

### Arena Sensors (`arena_sensor.h`)

Arena mode has a parallel sensor system for rotation-aware detection:
- `query_arena_sensor(SensorDef, ArenaQueryContext)` — dispatches Raycast or Occulus, rotated by ship facing direction. Detects towers, tokens, friendly ships, enemy ships, and bullets. Returns `ArenaSensorReading` with distance and `ArenaHitType`.
- `build_arena_ship_input(ShipDesign, ctx, squad_inputs..., memory)` — builds the complete fighter input vector: [sensor values] + [6 squad leader inputs] + [memory slots].
- `ArenaQueryContext` — bundles ship position, rotation, team, and all entity spans for a single sensor query.
- `is_full_sensor` flag — when true, each sensor produces 5 input values instead of 1. This gives the fighter net entity-type awareness.

Both the arena game screen and fighter drill screen use `build_arena_ship_input()` for their fighter nets.

## Scoring

### Scroller Scoring (defaults, tunable via pause screen)

| Event | Points |
|-------|--------|
| Distance traveled | +0.1/px |
| Tower destroyed | +50 |
| Token collected | +500 |
| Bullet fired | -30 |
| Hit a tower | DEATH |

Position multipliers scale score by screen position (center/edge, top/bottom).

### Arena Scoring (defaults, tunable)

| Factor | Default Weight |
|--------|---------------|
| Base damage dealt | 1.0 |
| Survival time | 0.5 |
| Ships alive at end | 0.2 |
| Tokens collected | 0.3 |

### Fighter Drill Scoring

Per-tick velocity-based scoring (prevents spawn-position bias):

| Phase | Scoring Formula |
|-------|----------------|
| Expand | dot(velocity, away_from_center) * expand_weight |
| Contract | dot(velocity, toward_center) * contract_weight |
| Attack | dot(velocity, toward_starbase) * travel_weight + bullet_hits * hit_bonus |

Always active: +token_bonus per token collected. Tower collision = death (with death_penalty).

### Attack Runs Scoring

3 attack phases per generation. Each phase spawns a starbase at a random position. Destroying the starbase advances to the next phase immediately.

| Event | Default Points |
|-------|---------------|
| Movement toward target | dot(velocity, toward_starbase) * 0.1 per tick |
| Bullet hit on starbase | +5000 |
| Token collected | +50 |
| Tower collision | -200 (death) |

### Squad Skirmish Scoring

Elimination tournament. Teams of squad nets compete in 1v1 arena matches.

| Event | Default Points |
|-------|---------------|
| Enemy fighter killed | +100 |
| Bullet hit on enemy base | +10 per hit |
| Enemy base destroyed | +kill_points × fighters_per_squad × num_squads |
| Own fighter lost | -20 |

## Evolution

- **Population:** 100 per generation (tunable; default: 10 for GameConfig scroller, 3 for EvolutionConfig)
- **Elitism:** top N survive unchanged (default: 10 for scroller via GameConfig, 3 for EvolutionConfig)
- **Selection:** tournament (size 5)
- **Crossover:** uniform, same-topology only
- **Weight mutation:** 10% of weights, Gaussian noise
- **Topology mutations:** add/remove nodes and layers
- **StructuredGenome:** weight genes + sensor genes (evolving ray/oval configurations)
- **Team evolution (arena):** `TeamIndividual` wraps 3 nets (NTM + squad leader + fighter). Team-level evolution mutates squad leader and NTM weights; fighter weights can be frozen or co-evolved. Uses `evolve_team_population()` / `evolve_squad_only()`.

## Arena Mode

A top-down 2D arena where teams of ships battle for territory. Each team has a hierarchical brain architecture: **NTM** (Near Threat Matrix) sub-nets evaluate nearby enemies, a **squad leader** net makes tactical decisions from macro state, and **fighter** nets handle sensorimotor control using rotation-aware sensors + squad leader orders.

### Architecture

- **ArenaWorld** (`arena_world.h` / `arena_world.cpp`) — single source of truth for arena physics. Owns ships, bullets, towers, tokens, bases. `tick()` returns `TickEvents` reporting what happened (kills, hits, pickups, deaths). All arena-based game modes compose ArenaWorld.
- **Arena Tick** (`arena_tick.h` / `arena_tick.cpp`) — centralized net execution. Two variants: `tick_fighters_scripted()` for drill modes (scripted squad leader inputs, one fighter net per ship) and `tick_arena_with_leader()` for arena/skirmish (learned NTM + squad leader + fighter pipeline, one shared net per team). Both build sensor input, run nets, decode output, apply actions, call `world.tick()`, and return `TickEvents`. Optional output parameters capture inputs for visualization.
- **ArenaWorldConfig** (`arena_world.h`) — physics-only config: world size, team/squad/fighter counts, obstacle counts, base/bullet/ship params, boundary wrapping.
- **ArenaConfig** (`arena_config.h`) — wraps `ArenaWorldConfig world` + game-mode fields: time limit, rounds per generation, fitness weights, sector grid params.
- **ArenaSession** (`arena_session.h` / `arena_session.cpp`) — thin wrapper around ArenaWorld. Delegates physics to `world_`, processes `TickEvents` for scoring (survival, kills), checks end conditions (time limit, teams alive, bases alive).
- **ArenaMatch** (`arena_match.h` / `arena_match.cpp`) — multi-generation match runner. Pits team genomes against each other, runs ArenaSession ticks, assigns fitness via configurable weights.
- **ArenaSensor** (`arena_sensor.h` / `arena_sensor.cpp`) — rotation-aware sensor system. See Sensor Engine section.
- **SectorGrid** (`sector_grid.h` / `sector_grid.cpp`) — spatial index dividing the arena into sectors. Used by NTM to efficiently find nearby enemies within a Manhattan distance diamond.
- **SquadLeader** (`squad_leader.h` / `squad_leader.cpp`) — NTM execution (shared-weight sub-net per nearby enemy, top-1 threat selection), squad leader net forward pass, order decoding (argmax over tactical/spacing output groups).
- **TeamEvolution** (`team_evolution.h` / `team_evolution.cpp`) — `TeamIndividual` wraps 3 nets. `create_team_population()`, `evolve_team_population()`, `evolve_squad_only()`.
- **Base** (`base.h`) — team starbase with position, health, team_id.
- **Camera** (`camera.h`) — pan/zoom viewport for arena rendering.

### UI Screens

- **ArenaConfigScreen** — configuration UI before match start (world size, team count, etc.)
- **ArenaGameScreen** — main arena training loop. Drives tick loop, renders via ArenaGameView, supports follow mode with live net viewer (Fighter/SquadLeader toggle).
- **ArenaPauseScreen** — save squad leader + companion NTM variants to `squad/` subdirectory. Syncs per-node activations from genome genes.

### Movement Model

Arena ships use rotation + thrust (not scroller's strafe model). `Triangle::apply_arena_actions()` handles turning and forward thrust. Bullets fire in facing direction with max range cutoff and directional travel.

## Fighter Drill Mode

A specialized training mode for fighter nets. Instead of a real evolved squad leader, the system injects **scripted squad leader inputs** through three timed phases (Expand, Contract, Attack). Fighters are scored on how well they follow commands, then evolved using individual-based evolution.

### Architecture

- **FighterDrillSession** (`fighter_drill_session.h` / `fighter_drill_session.cpp`) — pure engine class. Manages a small arena world (4000x4000) with ships, towers, tokens, one enemy starbase, and bullets. Tracks `DrillPhase` enum (Expand/Contract/Attack/Done) with configurable phase timing.
- **FighterDrillScreen** (`ui/screens/fighter_drill_screen.h` / `game/fighter_drill_screen.cpp`) — UIScreen driving the training loop. Computes scripted squad inputs per phase, runs arena sensors via `build_arena_ship_input()`, forwards fighter nets, decodes outputs, ticks the session, and renders via direct SDL.
- **FighterDrillPauseScreen** (`ui/screens/fighter_drill_pause_screen.h` / `game/fighter_drill_pause_screen.cpp`) — pause overlay with two tabs: Evolution (mutation rates) and Save Variants (multi-select fighters to save as named variants to `squad/` directory).

### Entry Point

From the Variant Viewer screen, select a fighter variant and click "Fighter Drill." The variant is converted to a fighter net (if not already) via `convert_variant_to_fighter()`, then mutated copies are seeded.

### Drill Phases

Three sequential phases, 20 seconds each (1200 ticks at 60fps):

| Phase | Scripted Squad Inputs | Scoring |
|-------|----------------------|---------|
| Expand | spacing=+1, aggression=0, no target | Movement away from squad center |
| Contract | spacing=-1, aggression=0, no target | Movement toward squad center |
| Attack | spacing=0, aggression=+1, target=starbase | Movement toward starbase + bullet hits |

All scoring is velocity-dot-product-based (per-tick), not position-based.

### Evolution

Individual-based (same as scroller). Uses `evolve_population()` directly — no TeamIndividual wrappers.

## Attack Runs Mode

A fighter drill focused purely on attacking. 3 phases per generation, each spawning a starbase at a random position. Fighters score by moving toward the target and hitting it. Destroying the starbase advances to the next phase immediately (no time carryover).

### Architecture

- **AttackRunSession** (`attack_run_session.h` / `attack_run_session.cpp`) — pure engine class. Same arena physics as FighterDrillSession but all 3 phases are attack phases. Phase transition on timer expiry OR starbase destruction.
- **AttackRunScreen** (`ui/screens/attack_run_screen.h` / `game/attack_run_screen.cpp`) — UIScreen driving the drill. All phases use attack-style scripted squad inputs (aggression=1.0, spacing=0.0, target heading/distance to current starbase).

### Entry Point

From the Variant Viewer screen, click "Attack Runs" button (next to "Fighter Drill").

## Squad Skirmish Mode

An elimination tournament drill for evolving squad nets (NTM + squad leader). N mutated squad net variants compete in 1v1 arena matches. Fighter nets are fixed. Variants accumulate fitness across all matches, then `evolve_squad_only()` breeds the next generation.

### Architecture

- **SkirmishConfig** (`skirmish.h`) — tournament and arena configuration: population size, seeds per match, world params, kill-based scoring weights.
- **run_skirmish_match()** (`skirmish.h` / `skirmish.cpp`) — blocking function that runs a complete 2-team arena match with NTM + squad leader + fighter tick loop and kill-based scoring. Fork of `run_arena_match()` with different scoring.
- **SkirmishTournament** (`skirmish_tournament.h` / `skirmish_tournament.cpp`) — orchestrates elimination bracket. `step()` advances one tick of the featured match while background matches run headlessly via `run_skirmish_match()`. Exposes `current_arena()` for live rendering, `variant_scores()` for leaderboard, and per-ship squad leader inputs for visualization.
- **SkirmishConfigScreen** (`ui/screens/skirmish_config_screen.h` / `game/skirmish_config_screen.cpp`) — configuration UI before tournament start. Sections: Tournament, Arena, Bases, Physics, Scoring.
- **SkirmishScreen** (`ui/screens/skirmish_screen.h` / `game/skirmish_screen.cpp`) — main tournament screen. Renders the featured match with team-colored ships (blue/red outlines) and squad-colored interiors. Tab cycles through individual fighters. F toggles Fighter/SquadLeader net view. Target Viz mode draws gold heading line from selected ship + gold target circle + squad center markers in squad colors.
- **SkirmishPauseScreen** (`ui/screens/skirmish_pause_screen.h` / `game/skirmish_pause_screen.cpp`) — pause with Evolution tab (mutation sliders) and Save Variants tab (saves squad leader + companion NTM pairs to `squad/` directory).

### Tournament Structure

Single-elimination bracket with multiple seeds per matchup:
1. Shuffle N variants. Pair into 1v1 matchups (bye if odd).
2. Each matchup plays `seeds_per_match` times with different random seeds.
3. Featured match (matchup 0) renders live; all others run headlessly.
4. After all seeds: winners advance (higher accumulated score), losers eliminated but keep their scores.
5. Finals (2-3 remaining): all pair combinations with double seeds.
6. After tournament: all variants have accumulated fitness → `evolve_squad_only()` → next generation.

### Entry Point

From the Variant Viewer screen's SquadNets tab, select a squad variant + pair a fighter → click "Squad Skirmish" → SkirmishConfigScreen → SkirmishScreen.

## Team Skirmish Mode

A co-evolution mode where multiple teams (2-8) evolve complete team brains independently against each other. Unlike Squad Skirmish (which freezes fighters), Team Skirmish evolves fighters, squad leaders, and NTMs within each team's own population pool.

### Architecture

- **TeamSkirmishConfig** (`team_skirmish.h`) — wraps `SkirmishConfig` + `CompetitionMode` (RoundRobin/FreeForAll) + per-team `TeamSeed` (squad + fighter snapshots + genome dirs).
- **TeamPool** (`team_skirmish.h`) — per-team state: squad population (`vector<TeamIndividual>`), fighter population (`vector<Individual>`), accumulated scores per individual.
- **TeamSkirmishSession** (`team_skirmish.h` / `team_skirmish.cpp`) — orchestrates one generation. Manages match scheduling, featured match (one tick at a time for rendering), background matches (headless). Each ship gets its own fighter net, each squad gets its own NTM + squad leader net.
- **tick_team_arena_match()** — per-tick net execution with per-squad/per-ship nets (unlike `tick_arena_match()` which shares one net per team).
- **run_team_skirmish_match()** — blocking headless match runner returning per-ship scores.
- **ShipAssignment** — maps each ship to `(team_id, squad_index, fighter_index)` for score routing.

### Competition Modes

- **Round Robin:** All unique team pairs play. N teams → N*(N-1)/2 matches. Same mutants play all matches; scores accumulate.
- **Free-for-All:** One match with all N teams in the arena simultaneously.

### Scoring

- Per-ship: kills, deaths, proportional base damage (split evenly across team).
- Squad leader fitness = sum of its fighters' scores (penalizes losing fighters).
- Same mutants across all matches in a generation; scores accumulate before evolution.

### Evolution

Each team evolves independently after all matches:
- `evolve_population()` on the fighter pool (all weights mutated).
- `evolve_squad_only()` on the squad pool (NTM + squad leader mutated, fighter field unused).

### Entry Point

Main menu → "Team Skirmish" → TeamSkirmishConfigScreen (pick teams, nets, params) → TeamSkirmishScreen.

## Controls

### Scroller Controls

| Key | Action |
|-----|--------|
| Tab | Cycle view: SWARM / BEST / WORST |
| Space | Pause + config screen |
| 1-4 | Speed (1x/5x/20x/100x) |
| H | Print scoring rules to console |
| Escape | Quit / Back |
| Hover | Mouse over neural net nodes to inspect weights |

### Arena / Fighter Drill / Attack Runs Controls

| Key | Action |
|-----|--------|
| Tab | Cycle camera: SWARM / BEST / WORST |
| Space | Pause (push pause screen) |
| 1-4 | Speed (1x/5x/20x/100x) |
| F | Toggle follow mode (track individual ship) |
| Escape | Exit to previous screen |
| Mouse drag | Pan camera |
| Shift+scroll | Zoom |

### Squad Skirmish Controls

| Key | Action |
|-----|--------|
| Tab | Cycle through individual fighters (SWARM → ship 0 → ship 1 → ... → SWARM) |
| F | Toggle Fighter / Squad Leader net view (in follow mode) |
| Space | Pause (push pause screen with Evolution + Save Variants tabs) |
| 1-4 | Speed (1x/5x/20x/100x) |
| Escape | Back to swarm (if following) or exit to previous screen |
| Arrow keys | Pan camera (switches to swarm) |
| Scroll | Zoom |
| Target Viz button | Toggle gold heading line + target circle + squad center markers |

### Team Skirmish Controls

| Key | Action |
|-----|--------|
| Tab | Cycle through individual fighters (SWARM → ship 0 → ship 1 → ... → SWARM) |
| F | Toggle Fighter / Squad Leader net view (in follow mode) |
| Space | Pause (push pause screen with Evolution + Save Fighters + Save Squad Leaders tabs) |
| 1-4 | Speed (1x/5x/20x/100x) |
| Escape | Back to swarm (if following) or exit to previous screen |
| Arrow keys | Pan camera (switches to swarm) |
| Scroll | Zoom |

## Building & Running

Prerequisites: library repos (`neuralnet`, `neuralnet-ui`, `evolve`) must be present at `../libs/`.

```bash
cd neuroflyer
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/neuroflyer
```

## Backlog

Feature backlog is tracked in `neuroflyer/docs/backlog.md`. When implementing features from the backlog, update the file to reflect completed items. When new feature ideas come up during development, add them to the backlog.

## UI Framework — Standard Practice

All new UI features MUST use the 4-layer UI framework. Do NOT create standalone draw functions with static locals or use the legacy `go_to_screen()` / `Screen` enum pattern.

### The 4 Layers

**1. UIScreen** (`include/neuroflyer/ui/ui_screen.h`)
- Major app contexts: MainMenu, Hangar, FlySession, etc.
- Subclass `UIScreen`, implement `on_enter()`, `on_exit()`, `on_draw()`, `name()`
- State lives as **member variables** (NOT static locals)
- Navigate via `ui.push_screen()`, `ui.pop_screen()`, `ui.replace_screen()`
- Header: `include/neuroflyer/ui/screens/{name}_screen.h`
- Source: `src/ui/screens/{category}/{name}_screen.cpp`

**2. UIView** (`include/neuroflyer/ui/ui_view.h`)
- Panels/regions within a screen with layout bounds (`set_bounds(x, y, w, h)`)
- Subclass `UIView`, implement `on_draw()`
- Used for reusable sub-panels: topology preview, net viewer, test bench
- Header: `include/neuroflyer/ui/views/{name}_view.h`

**3. UIModal** (`include/neuroflyer/ui/ui_modal.h`)
- Temporary overlays rendered on top of everything
- Subclass `UIModal`, implement `on_draw()`, `name()`, optionally `blocks_input()`
- Used for: confirm dialogs, text input, node editor
- Escape closes the top modal first (before affecting screens)
- Open via `ui.push_modal()`, close via `ui.pop_modal()`
- Header: `include/neuroflyer/ui/modals/{name}_modal.h`

**4. Widgets** (`include/neuroflyer/ui/ui_widget.h`)
- `ui::` namespace free functions for styled controls
- `ui::button()`, `ui::slider_float()`, `ui::input_int()`, `ui::checkbox()`, `ui::section_header()`
- All new UI controls should use these for visual consistency
- Add new widget functions here when a new control pattern is needed

### How to Add a New Feature

**New screen (e.g., "Leaderboard"):**
1. Header: `include/neuroflyer/ui/screens/leaderboard_screen.h` — subclass `UIScreen`
2. Source: `src/ui/screens/{category}/leaderboard_screen.cpp` — implement `on_draw()`
   - Categories: `menu/`, `hangar/`, `game/`, `analysis/` (match the existing grouping)
3. State as member variables on the class, NOT static locals
4. Navigate: `ui.push_screen(std::make_unique<LeaderboardScreen>())`

**New popup (e.g., "Rename Variant"):**
1. Header: `include/neuroflyer/ui/modals/rename_modal.h` — subclass `UIModal`
2. Source: `src/ui/modals/rename_modal.cpp` (app-specific) or `src/ui/framework/modals/rename_modal.cpp` (generic/reusable)
3. Open: `ui.push_modal(std::make_unique<RenameModal>(variant_name, on_confirm_callback))`
4. The modal calls its callback and `ui.pop_modal()` when done

**New panel within an existing screen (e.g., "Sensor Editor"):**
1. Header: `include/neuroflyer/ui/views/sensor_editor_view.h` — subclass `UIView`
2. Source: `src/ui/views/sensor_editor_view.cpp`
3. Screen sets bounds via `view.set_bounds(x, y, w, h)` and calls `view.on_draw()`

**New reusable component (e.g., "Fitness Chart"):**
1. Header: `include/neuroflyer/components/fitness_chart.h` with a state struct + draw function
2. Source: `src/ui/views/fitness_chart.cpp` (component sources live alongside views)
3. Components use **callbacks** (e.g., `std::function<void(...)>`) — NOT UIManager
4. The screen that embeds the component wires callbacks to UIManager actions

### Rules

- **No static locals for UI state.** All state on the class. Static locals cause bugs when re-entering screens.
- **No `go_to_screen()`** for new code. Use `ui.push_screen()` / `ui.pop_screen()`.
- **Components don't know about UIManager.** They take data + callbacks. Screens wire them to navigation.
- **Engine code (`src/engine/`) never imports SDL or ImGui.** UI code (`src/ui/`) wraps engine calls.
- **Use `ui::` widgets** for buttons, sliders, inputs — not raw ImGui calls for styled controls.
- **Modals for popups**, not inline dialog state on screens.

### Existing Implementations (Reference)

| Type | Example | Header | Source |
|------|---------|--------|--------|
| Screen | `HangarScreen` | `ui/screens/hangar_screen.h` | `ui/screens/hangar/hangar_screen.cpp` |
| Screen | `FlySessionScreen` | `ui/screens/fly_session_screen.h` | `ui/screens/game/fly_session_screen.cpp` |
| Screen | `SettingsScreen` | `ui/screens/settings_screen.h` | `ui/screens/menu/settings_screen.cpp` |
| Screen | `VariantNetEditorScreen` | `ui/screens/hangar/variant_net_editor_screen.h` | `ui/screens/hangar/variant_net_editor_screen.cpp` |
| Screen | `ArenaConfigScreen` | `ui/screens/arena_config_screen.h` | `ui/screens/arena/arena_config_screen.cpp` |
| Screen | `ArenaGameScreen` | `ui/screens/arena_game_screen.h` | `ui/screens/arena/arena_game_screen.cpp` |
| Screen | `ArenaPauseScreen` | `ui/screens/arena_pause_screen.h` | `ui/screens/arena/arena_pause_screen.cpp` |
| Screen | `FighterDrillScreen` | `ui/screens/fighter_drill_screen.h` | `ui/screens/game/fighter_drill_screen.cpp` |
| Screen | `FighterDrillPauseScreen` | `ui/screens/fighter_drill_pause_screen.h` | `ui/screens/game/fighter_drill_pause_screen.cpp` |
| Screen | `AttackRunScreen` | `ui/screens/attack_run_screen.h` | `ui/screens/game/attack_run_screen.cpp` |
| Screen | `SkirmishConfigScreen` | `ui/screens/skirmish_config_screen.h` | `ui/screens/game/skirmish_config_screen.cpp` |
| Screen | `SkirmishScreen` | `ui/screens/skirmish_screen.h` | `ui/screens/game/skirmish_screen.cpp` |
| Screen | `SkirmishPauseScreen` | `ui/screens/skirmish_pause_screen.h` | `ui/screens/game/skirmish_pause_screen.cpp` |
| Screen | `TeamSkirmishConfigScreen` | `ui/screens/team_skirmish_config_screen.h` | `ui/screens/game/team_skirmish_config_screen.cpp` |
| Screen | `TeamSkirmishScreen` | `ui/screens/team_skirmish_screen.h` | `ui/screens/game/team_skirmish_screen.cpp` |
| Screen | `TeamSkirmishPauseScreen` | `ui/screens/team_skirmish_pause_screen.h` | `ui/screens/game/team_skirmish_pause_screen.cpp` |
| View | `TopologyPreviewView` | `ui/views/topology_preview_view.h` | `ui/views/topology_preview_view.cpp` |
| View | `NetViewerView` | `ui/views/net_viewer_view.h` | `ui/views/net_viewer_view.cpp` |
| View | `TestBenchView` | `ui/views/test_bench_view.h` | `ui/views/test_bench_view.cpp` |
| View | `ArenaConfigView` | `ui/views/arena_config_view.h` | `ui/views/arena_config_view.cpp` |
| View | `ArenaGameView` | `ui/views/arena_game_view.h` | `ui/views/arena_game_view.cpp` |
| View | `ArenaGameInfoView` | `ui/views/arena_game_info_view.h` | `ui/views/arena_game_info_view.cpp` |
| Modal | `ConfirmModal` | `ui/modals/confirm_modal.h` | `ui/framework/modals/confirm_modal.cpp` |
| Modal | `NodeEditorModal` | `ui/modals/node_editor_modal.h` | `ui/modals/node_editor_modal.cpp` |
| Modal | `LayerEditorModal` | `ui/modals/layer_editor_modal.h` | `ui/modals/layer_editor_modal.cpp` |
| Component | `net_viewer` | `components/net_viewer.h` | `ui/views/net_viewer.cpp` |
| Component | `test_bench` | `components/test_bench.h` | `ui/views/test_bench.cpp` |
| Component | `lineage_graph` | `components/lineage_graph.h` | `ui/views/lineage_graph.cpp` |
| Renderer | `variant_net_render` | `renderers/variant_net_render.h` | `ui/renderers/variant_net_render.cpp` |

## Key Design Decisions

- **4-layer UI architecture** — UIManager with screen stack + modal stack. See "UI Framework" section above.
- **Engine/UI split** — `src/engine/` has zero SDL/ImGui deps, `src/ui/` has all rendering. Clean separation.
- **Composable components** — net viewer, test bench, lineage graph, fitness editor are not screen-specific. They use callbacks (e.g., `on_node_click`) instead of knowing about UIManager.
- **Frame-based fly session** — game loop converted from blocking nested loops to one-frame-per-call, enabling clean pause/resume via screen transitions
- **neuralnet-ui library** — generic neural net rendering lives in `libs/neuralnet-ui/`, NeuroFlyer-specific wrapper in `src/ui/renderers/variant_net_render.cpp`. Two-layer design: `render_neural_net()` (generic) + `build_variant_net_config()` (NeuroFlyer-specific, switches label/color scheme based on `NetType`). The renderer supports `text_scale` for scaled bitmap font at higher zoom levels.
- **Net viewer zoom** — `NetViewerViewState` has `zoom`, `scroll_x`, `zoom_enabled` fields. The variant net editor defaults to 2x zoom with Shift+scroll to adjust. The view renders into a zoom-scaled off-screen SDL canvas and blits a viewport-sized window from it. Mouse coordinates are correctly mapped through the zoom+scroll transform.
- **StructuredGenome** — evolved sensor configurations alongside weights and topology. Per-node activation functions.
- **Recurrent not stacked frames** — net develops its own memory
- **Sensor engine as single source of truth** — one set of functions for detection, input encoding, and output decoding. No inline math in screens. Visual sorting via display permutation (doesn't affect functional data order).
- **ShipDesign per variant, not global** — sensor config lives on each saved variant, not on GameConfig. Editing one variant's sensors doesn't affect others.
- **Shared library repos** — neuralnet and evolve are independent repos shared by EcoSim, NeuroFlyer, and AntSim
- **ArenaWorld as single physics source of truth** — `ArenaWorld` owns all entities and runs the authoritative tick loop (movement, collisions, bullets). `ArenaSession`, `FighterDrillSession`, and `AttackRunSession` all compose `ArenaWorld` rather than duplicating physics. Game modes only handle scoring, phases, and end conditions. `tick()` returns `TickEvents` so game modes interpret events without touching physics internals.
- **Centralized net execution in engine layer** — `tick_fighters_scripted()` and `tick_arena_with_leader()` in `arena_tick.h` are the single source of truth for running neural nets in arena-based modes. UI screens compute scripted squad leader inputs (drills) or pass learned nets (arena), then call the appropriate tick function. UI screens never call `net.forward()` directly (except FlySessionScreen which uses the scroller sensor system). SkirmishTournament and TeamSkirmishSession also use these shared functions.
- **Velocity-based drill scoring** — drill phases score using per-tick `dot(velocity, desired_direction)`, not absolute position. Prevents a fighter's spawn position from unfairly affecting fitness.
- **Rotated + non-rotated collision variants** — `collision.h` provides both `bullet_triangle_collision()` (scroller, ships face up) and `bullet_triangle_collision_rotated()` (arena, ships have facing direction). Both kept to avoid unnecessary trig in scroller mode.
- **Hierarchical team brain** — arena teams use 3 co-evolved nets: NTM (threat scoring per nearby enemy), squad leader (tactical orders from macro state), fighter (sensorimotor control from sensors + squad orders). The NTM uses shared weights duplicated per nearby enemy with top-1 threat selection via `SectorGrid`.
- **Team evolution** — `TeamIndividual` bundles 3 nets. `evolve_team_population()` mutates squad leader + NTM; `evolve_squad_only()` freezes fighter weights. Individual fighters use standard `evolve_population()` in drill mode.
- **Attack Runs as separate drill** — `AttackRunSession` is a fork of `FighterDrillSession` with all 3 phases as attack phases and early phase advance on starbase destruction. Kept separate rather than parameterizing the existing drill to avoid adding complexity to the fighter drill.
- **Skirmish tournament: elimination bracket with per-tick stepping** — `SkirmishTournament::step()` advances the featured match one tick while background matches run headlessly via `run_skirmish_match()`. This gives live visualization of one match while all others in the round complete in the background. All variants keep accumulated scores (not just winners) so `evolve_squad_only()` has a full fitness landscape.
- **Skirmish scoring: kill-based, not weighted fitness** — Unlike arena mode's weighted fitness (damage + survival + alive + tokens), skirmish uses discrete kill/death/base-hit counting. This gives cleaner competitive signal for tournament selection.
- **Target heading visualization derived from ship inputs** — The gold heading line in skirmish follow mode reconstructs world-space direction from `squad_target_heading * π + ship.rotation`, not from god-mode target position. This validates that the input encoding is correct by showing exactly what the neural net "sees."
- **Team skirmish: per-ship/per-squad net assignment** — `TeamSkirmishSession` gives each ship its own fighter net mutant and each squad its own NTM + squad leader mutant, unlike `SkirmishTournament` which shares one net per team. `ShipAssignment` maps each ship to its team pool indices. Scores accumulate per-individual across all matches in a generation, then each team evolves independently via `evolve_population()` (fighters) and `evolve_squad_only()` (squad brains).

## Standards & Practices

### Universal Principles

**No magic numbers or duplicated literals.** When the same value appears in multiple places, use named `constexpr` constants, config objects, or enums. A value that appears once in a clear context is fine — the problem is duplication and opacity. Follow the pattern in `game.cpp` (`EASY_PHASE`, `RAMP_INTERVAL`, `BASE_GAP_MIN`).

**DRY — Don't Repeat Yourself.** Every piece of knowledge should have a single, authoritative representation. When the same logic exists in multiple places, extract it. Critical nuance: DRY applies to *knowledge*, not to code that merely looks similar. Two blocks that look alike but represent different concepts should stay separate — merging them creates false coupling. Test: "if I change this, should it change everywhere?" If yes, centralize. If no, the duplication is coincidental.

**Separation of concerns.** Business logic, simulation logic, and game logic live in pure modules with no UI dependencies. UI code calls into the logic layer — not the other way around. Apply rigorously at module/layer boundaries; within a single small module, less critical.

**Meaningful naming over comments.** A well-named function communicates intent without needing a comment. `build_ship_input()` beats `process()`. Comments should explain *why*, not *what*. Module-level comments explaining purpose and invariants are worthwhile; inline comments restating the code are noise.

**Minimize scope.** Declare variables as close to their use as possible, in the narrowest scope possible. Keep functions focused on one conceptual operation. Smaller scope = fewer surprises.

**Composition over inheritance.** Use inheritance for genuine "is-a" relationships (`UIScreen`, `UIView`, `UIModal`). Use composition — callbacks, interfaces, member objects — for everything else. If you're more than 2-3 levels deep in an inheritance chain, reconsider.

**Defensive programming at boundaries.** Validate inputs at trust boundaries — file I/O, user input, external APIs. Inside your own code, use assertions to verify invariants (things that should always be true if the code is correct). Assertions catch bugs early; boundary validation catches bad input.

**Immutability by default.** Declare variables `const`, parameters `const&`, methods `const` unless they genuinely need to mutate. Mutable state should be the exception that needs justification, not the default.

**Don't live with broken windows.** One hack invites another. If you see something wrong while working on something else, fix it or flag it. Don't make things worse.

### C++20 Idioms

- **`constexpr` over `const`** for compile-time constants. Use `constexpr` for values known at compile time; reserve `const` for runtime-immutable values.
- **`[[nodiscard]]`** on non-void functions whose return value shouldn't be ignored — especially predicates, factory functions, and anything returning a result that indicates success/failure.
- **`enum class` over plain `enum`** — always. Scoped enums prevent implicit conversions and namespace pollution.
- **`std::span` or `const&`** for non-owning views of contiguous data at function boundaries. Avoid unnecessary copies.
- **`#pragma once`** for all header include guards.
- **Structured bindings** (`const auto& [name, parent, gen] = ...`) where they improve readability.
- **Range-based for loops** as the default iteration pattern.

### Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Files | `snake_case` | `evolution.cpp`, `hangar_screen.h` |
| Functions | `snake_case` | `build_ship_input()`, `query_sensor()` |
| Classes/Structs | `PascalCase` | `GameSession`, `ShipDesign`, `UIScreen` |
| Private members | `snake_case_` (trailing underscore) | `selected_genome_idx_`, `genomes_` |
| Public members | `snake_case` (no trailing underscore) | `alive`, `score` |
| Constants | `UPPER_SNAKE_CASE` | `SCREEN_W`, `ACTION_COUNT` |
| Enum types | `PascalCase` | `SensorType`, `ButtonStyle` |
| Enum values | `PascalCase` | `Raycast`, `Occulus`, `Tanh` |
| Namespaces | `snake_case` | `neuroflyer`, `neuroflyer::ui` |

### Engine/UI Boundary

`src/engine/` must **never** include SDL, ImGui, or any rendering header. This is the project's strongest architectural invariant. Engine code is pure logic — testable, portable, and free of graphical dependencies. If you need rendering data in engine code, pass it as plain data (floats, vectors, structs) through function parameters.

`src/ui/` wraps engine calls and handles all rendering. When engine code needs to communicate results that the UI will display, use return values or output parameters — not callbacks into UI code.

### UI Framework Compliance

All new UI features use the 4-layer architecture (Screen → View → Modal → Widget). Specifically:

- **No static locals for UI state.** All state lives as member variables on the screen/view/modal class. Static locals cause bugs when re-entering screens and make state invisible to the class interface.
- **No `go_to_screen()` in new code.** Use `ui.push_screen()` / `ui.pop_screen()` / `ui.replace_screen()`.
- **Components take data + callbacks, not UIManager.** Screens wire component callbacks to navigation actions. This keeps components reusable across screens.
- **Use `ui::` widgets** for buttons, sliders, inputs — not raw ImGui calls for styled controls.
- **Modals for popups**, not inline dialog state tracked with booleans on screens.

### Test Expectations

- **Framework:** Google Test with test fixtures (`::testing::Test`).
- **Location:** All tests in `tests/`. One test file per module or logical grouping.
- **Fixture pattern:** `SetUp()` creates temp directories or test data; `TearDown()` cleans up. Use RAII (filesystem temp paths) for automatic cleanup.
- **Assertions:** `ASSERT_*` for preconditions that make the rest of the test meaningless if they fail. `EXPECT_*` for checks where the test should continue to report all failures.
- **Engine testability:** All engine logic must be testable without SDL or ImGui. If a function can't be tested without a renderer, it belongs in `src/ui/`, not `src/engine/`.
- **Test what matters:** Focus on behavior (what the function does), not implementation details (how it does it). Don't test private internals — test through the public API.

### Memory Management

- **`std::unique_ptr`** for exclusive ownership (screens, modals, sessions).
- **No raw `new`/`delete`.** Use `std::make_unique` for heap allocation.
- **Stack allocation by default.** Only heap-allocate when ownership transfer or polymorphism requires it.
- **`const&`** for read-only access to existing objects. **`std::move`** for transferring ownership.
- **Containers own their elements.** `std::vector<Individual>` owns the individuals; pass `const&` or `std::span` for read-only views.

### Error Handling

- **Exceptions** for I/O failures, serialization errors, and external operation failures (`std::runtime_error`, `std::invalid_argument`). These are recoverable — catch them at UI boundaries and log/display the error.
- **Assertions** (`assert()`) for internal invariants — conditions that should always be true if the code is correct. These catch bugs during development.
- **Early returns** for invalid conditions: `if (!tower.alive) continue;` — clear, readable, avoids deep nesting.
- **No bare `catch(...)`** in new code. Always catch a specific exception type and log the error. Swallowing errors silently hides bugs.
- **Log errors with context:** Include what operation failed and why, not just "error occurred". Use `std::cerr` with the exception message.

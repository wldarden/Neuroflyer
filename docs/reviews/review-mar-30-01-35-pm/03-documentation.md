# Documentation Gaps Review

**Date:** 2026-03-30
**Scope:** Entire NeuroFlyer repo, focused on CLAUDE.md accuracy vs actual codebase
**Reviewer:** Automated code review

---

## Summary

CLAUDE.md was last substantially updated before the arena mode system was implemented. The arena subsystem now includes 7 new engine headers, 2 new screens, 4 new views, 1 new modal, 6 new engine source files, and several new concepts (squad leaders, NTM sub-nets, sector grid, team evolution, NetType enum, snapshot format v7). None of this is documented in CLAUDE.md. Several other files present in the codebase (base.h, camera.h, theme.h, game_view.h, net_designer_view.h) are also missing from the file tree.

**Findings:** 16 documentation gaps identified (4 HIGH, 7 MEDIUM, 5 LOW).

---

## [HIGH] Arena mode architecture entirely missing from CLAUDE.md

**ID:** `DOC-001`
**Location:** `CLAUDE.md` (Architecture section, after "Sensor Engine" section)
**Description:** The arena mode is a major game mode with its own config, session, match runner, sensor system, squad leader system, sector grid, team evolution pipeline, and 3 neural net types per team. None of this is mentioned anywhere in CLAUDE.md.
**Impact:** A contributor reading CLAUDE.md would have no idea arena mode exists. They would not know about ArenaConfig, ArenaSession, ArenaMatch, team-based evolution, the squad leader/NTM system, or the sector grid. This is the single largest documentation gap.

**Draft text** (add as a new section after "## Evolution"):

```markdown
## Arena Mode

A second game mode where ships exist in a large 2D arena and battle each other in teams. Uses the same neural net engine, evolution pipeline, genome management, and UI framework as the scroller mode.

Key differences from scroller:
- Large rectangular world (default 81920 x 51200 px, configurable)
- No auto-scroll -- ships rotate and thrust freely
- All ships share one world instance (not separate GameSessions)
- N teams of K squads of M fighters (default: 2 teams, 1 squad, 8 fighters)
- Each team has a **base** (destructible structure with HP) to defend
- Round ends on time limit or last base standing
- Friendly fire enabled
- Wrap-around boundaries (toroidal world)

### Arena Architecture

- **ArenaConfig** (`arena_config.h`) -- world size, team/squad counts, timing, NTM/squad leader topology, fitness weights
- **ArenaSession** (`arena_session.h`) -- one shared world: ships, towers, tokens, bullets, bases. Tick-based simulation with collision resolution
- **ArenaMatch** (`arena_match.h`) -- `run_arena_match()` orchestrates a complete round: builds nets from TeamIndividuals, runs arena ticks, computes per-team scores
- **ArenaSensor** (`arena_sensor.h`) -- arena-specific sensor queries that detect towers, tokens, friendly ships, enemy ships, and bullets. `build_arena_ship_input()` is the arena equivalent of `build_ship_input()`
- **Base** (`base.h`) -- destructible team structure with HP, team assignment, and bullet damage

### Movement Model (Arena)

Same 5 neural net outputs, reinterpreted:

| Output | Scroller | Arena |
|--------|----------|-------|
| Left | Move left | Rotate counter-clockwise |
| Right | Move right | Rotate clockwise |
| Up | Move up | Thrust forward (facing direction) |
| Down | Move down | Thrust reverse |
| Shoot | Fire upward | Fire in facing direction |

### Hierarchical Brain Architecture

Each team genome encodes **three neural networks** (Phase 1.5):

1. **NTM Sub-Net** (Near Threat Matrix) -- shared-weight net, duplicated per nearby enemy entity. Evaluates threats, top-1 selected by score. Topology: 7 inputs, [4] hidden, 1 output (threat_score).
2. **Squad Leader Net** -- one per squad. Receives squad stats + NTM result, outputs tactical orders (AttackStarbase/AttackShip/DefendHome) and spacing orders (Expand/Contract) via argmax 1-hot groups. Topology: 14 inputs, [8] hidden, 5 outputs.
3. **Fighter Net** -- shared weights, one instance per ship. Local sensors + 6 structured squad leader inputs (target heading/dist, center heading/dist, aggression, spacing) + recurrent memory.

### Squad Leader System (`squad_leader.h`)

- `gather_near_threats()` -- uses SectorGrid to find enemies within Manhattan distance diamond
- `run_ntm_threat_selection()` -- runs NTM sub-net per threat, selects top-1 by score
- `run_squad_leader()` -- runs the leader net with squad stats + NTM result, outputs SquadLeaderOrder
- `compute_squad_leader_fighter_inputs()` -- converts orders to 6 structured fighter inputs

### Sector Grid (`sector_grid.h`)

2D spatial index for efficient "nearby entity" queries. World divided into fixed-size sectors (default 2000x2000 px). `entities_in_diamond(center, radius)` returns all entity IDs within Manhattan distance. Used by NTM threat gathering.

### Team Evolution (`team_evolution.h`)

- `TeamIndividual` -- bundles NTM + squad leader + fighter Individuals with combined fitness
- `create_team_population()` / `evolve_team_population()` -- full team evolution
- `evolve_squad_only()` -- evolves only squad leader + NTM nets while freezing fighter weights (for squad training mode)
- `convert_variant_to_fighter()` (in `evolution.h`) -- converts scroller variant to arena fighter by remapping sensor inputs

### Camera (`camera.h`)

Free-roaming camera for the arena world with zoom, pan, and follow mode. `world_to_screen()` / `screen_to_world()` coordinate transforms. Used by ArenaGameView.

### Arena Fitness

| Factor | Weight (default) |
|--------|-----------------|
| Base damage dealt | 1.0 |
| Survival time | 0.5 |
| Ships alive at end | 0.2 |
| Tokens collected | 0.3 |
```

---

## [HIGH] Snapshot format version is wrong in CLAUDE.md

**ID:** `DOC-002`
**Location:** `CLAUDE.md` lines 70, 82-83, 175 (multiple references to "v1-v4")
**Description:** CLAUDE.md says the snapshot format is "NFS format v1-v4" in the file tree annotations and "Binary format versioned (v1 = original, v2 = evolvable flags)" in the Genome Management section. The actual current version is **v7** (as defined by `CURRENT_VERSION = 7` in `snapshot_io.cpp`). Versions 5-7 added: v5 = `run_count`, v6 = `paired_fighter_name`, v7 = `NetType`.
**Impact:** A developer extending the snapshot format would start from the wrong version number and potentially miss the v5-v7 fields when reading/writing snapshots.

**Draft text** -- replace the snapshot_io annotations in the file tree:

```
│   ├── snapshot_io.h             — Binary save/load (NFS format v1-v7, CRC32)
```

```
│   │   ├── snapshot_io.cpp       — Binary I/O for snapshots (NFS v1-v7)
```

Replace in "## Genome Management (Hangar)":

```markdown
Each `.bin` file is self-describing: contains ShipDesign (sensors, memory), network topology, weights, parent link, and metadata. Binary format versioned (NFS v1-v7):
- v1: original format
- v2: evolvable flags (EvolvableFlags on ShipDesign)
- v3: per-node activation functions
- v4: sensor IDs (uint16_t per SensorDef)
- v5: run_count (training run counter)
- v6: paired_fighter_name (squad-fighter pairing metadata)
- v7: NetType enum (Solo/Fighter/SquadLeader)
```

---

## [HIGH] File tree in CLAUDE.md is missing many files

**ID:** `DOC-003`
**Location:** `CLAUDE.md` lines 21-137 (the `neuroflyer/` file tree)
**Description:** The file tree is missing all arena-mode files, several newer headers, and some files that were removed. Specifically missing from `include/neuroflyer/`:
- `arena_config.h` -- ArenaConfig struct
- `arena_session.h` -- ArenaSession class
- `arena_match.h` -- run_arena_match() function
- `arena_sensor.h` -- arena sensor queries + fighter input builder
- `squad_leader.h` -- NTM, squad leader net, fighter inputs
- `sector_grid.h` -- SectorGrid spatial index
- `team_evolution.h` -- TeamIndividual, team population ops
- `base.h` -- Base entity (destructible team structure)
- `camera.h` -- Camera struct for arena
- `config.h` is present but does not mention `GameMode` enum

Missing from `include/neuroflyer/ui/screens/`:
- `arena_game_screen.h`
- `arena_config_screen.h`

Missing from `include/neuroflyer/ui/views/`:
- `arena_game_view.h`
- `arena_game_info_view.h`
- `arena_config_view.h`
- `net_designer_view.h`
- `game_view.h`

Missing from `include/neuroflyer/ui/modals/`:
- `fighter_pairing_modal.h`

Missing from `src/engine/`:
- `sector_grid.cpp`
- `arena_session.cpp`
- `arena_match.cpp`
- `arena_sensor.cpp`
- `squad_leader.cpp`
- `team_evolution.cpp`

Missing from `src/ui/screens/`:
- `arena/arena_config_screen.cpp`
- `arena/arena_game_screen.cpp`

Missing from `src/ui/views/`:
- `arena_game_view.cpp`
- `arena_game_info_view.cpp`
- `arena_config_view.cpp`
- `net_designer_view.cpp`
- `game_view.cpp`

Missing from `src/ui/modals/`:
- `fighter_pairing_modal.cpp`

Files listed in the tree that no longer exist:
- `src/ui/views/net_viewer.cpp` (removed; functionality now in `net_viewer_view.cpp`)
- `src/ui/renderers/starfield.cpp` (removed; functionality moved into `game_view.cpp`)
- `src/ui/renderers/occulus.cpp` (removed; functionality moved into `game_view.cpp`)

Additionally, `include/neuroflyer/ui/theme.h` exists but is not listed.

**Impact:** The file tree is the primary navigation aid for contributors. Missing files means developers cannot find arena code, and listed-but-absent files cause confusion.

**Draft text** -- the following blocks should be added to the file tree. For the `include/neuroflyer/` section, add after `paths.h`:

```
│   ├── arena_config.h            — ArenaConfig: world/team/squad/NTM/fitness params
│   ├── arena_session.h           — ArenaSession: shared arena world simulation
│   ├── arena_match.h             — run_arena_match(): complete match orchestration
│   ├── arena_sensor.h            — Arena sensor queries, build_arena_ship_input()
│   ├── squad_leader.h            — NTM sub-nets, squad leader net, SquadLeaderOrder
│   ├── sector_grid.h             — SectorGrid: 2D spatial index for NTM queries
│   ├── team_evolution.h          — TeamIndividual, team population evolution
│   ├── base.h                    — Base entity (destructible team structure)
│   ├── camera.h                  — Camera: zoom/pan/follow for arena viewport
```

For `include/neuroflyer/ui/screens/`, add:

```
│   │   │   ├── arena_game_screen.h
│   │   │   ├── arena_config_screen.h
```

For `include/neuroflyer/ui/views/`, add:

```
│   │   │   ├── arena_game_view.h
│   │   │   ├── arena_game_info_view.h
│   │   │   ├── arena_config_view.h
│   │   │   ├── net_designer_view.h
│   │   │   └── game_view.h
```

For `include/neuroflyer/ui/modals/`, add:

```
│   │       ├── fighter_pairing_modal.h
```

Add `include/neuroflyer/ui/theme.h`:

```
│   │   ├── theme.h               — neuroflyer::theme namespace: sensor/node/UI colors
```

For `src/engine/`, add:

```
│   │   ├── sector_grid.cpp       — SectorGrid implementation
│   │   ├── arena_session.cpp     — ArenaSession tick, collision, spawning
│   │   ├── arena_match.cpp       — Match orchestration + scoring
│   │   ├── arena_sensor.cpp      — Arena sensor detection + input encoding
│   │   ├── squad_leader.cpp      — NTM evaluation, squad leader logic
│   │   └── team_evolution.cpp    — Team creation, evolution, squad-only mode
```

For `src/ui/screens/`, add new category:

```
│   │   │   ├── arena/            — Arena mode screens
│   │   │   │   ├── arena_config_screen.cpp
│   │   │   │   └── arena_game_screen.cpp
```

For `src/ui/views/`, add:

```
│   │   │   ├── arena_game_view.cpp       — Arena world rendering (ships, bases, towers)
│   │   │   ├── arena_game_info_view.cpp  — Arena HUD: generation, kills, scores
│   │   │   ├── arena_config_view.cpp     — Arena config editor controls
│   │   │   ├── net_designer_view.cpp     — Network topology designer controls
│   │   │   ├── game_view.cpp             — Scroller game panel (assets, starfield, rendering)
```

For `src/ui/modals/`, add:

```
│   │   │   ├── fighter_pairing_modal.cpp — Fighter variant selection for squad training
```

Remove from the tree (files no longer exist):

```
- src/ui/views/net_viewer.cpp          (removed)
- src/ui/renderers/starfield.cpp       (removed)
- src/ui/renderers/occulus.cpp         (removed)
```

---

## [HIGH] NetType enum not documented

**ID:** `DOC-004`
**Location:** `CLAUDE.md` (Neural Net section, Snapshot section)
**Description:** The `NetType` enum (`snapshot.h`) distinguishes three types of neural network stored in snapshots: `Solo` (scroller variant), `Fighter` (arena fighter), and `SquadLeader` (squad leader net). This enum was added in snapshot format v7 and drives input/output label selection in the net viewer. CLAUDE.md mentions only the original scroller neural net with "5 outputs: UP, DOWN, LEFT, RIGHT, SHOOT" and does not acknowledge that different net types exist.
**Impact:** Contributors would not know that snapshots carry a net_type field, or that the net viewer switches labels based on it. Anyone adding a new net type (e.g., Commander) would not know to update the enum.

**Draft text** -- add to the "## Neural Net" section:

```markdown
### Net Types

The `NetType` enum (`snapshot.h`) identifies the type of neural network stored in a snapshot:

| NetType | Description | Typical Input Layout |
|---------|-------------|---------------------|
| `Solo` | Original scroller variant | Sensors + position + memory |
| `Fighter` | Arena fighter | Sensors + 6 squad leader inputs + memory |
| `SquadLeader` | Squad leader net | 14 strategic inputs (squad stats + NTM result) |

NetType is persisted in snapshot format v7+ and drives which input/output labels the net viewer displays. The variant viewer screen uses NetType tabs to organize variants by kind within a genome directory.
```

---

## [MEDIUM] Screen flow diagram does not include arena screens

**ID:** `DOC-005`
**Location:** `CLAUDE.md` lines 139-151 (Screen Flow section)
**Description:** The screen flow diagram shows only scroller-mode navigation (MainMenu -> FlySession -> PauseConfig, MainMenu -> Hangar, etc.). The arena flow is missing: MainMenu -> ArenaConfigScreen -> ArenaGameScreen. The ArenaConfigScreen lets users configure arena parameters and launch, and ArenaGameScreen runs the arena simulation.
**Impact:** Contributors navigating the screen stack cannot trace how arena screens are reached.

**Draft text** -- add to the Screen Flow diagram:

```
               → push(ArenaConfigScreen) → push(ArenaGameScreen)
                                           └── follow mode: net viewer (Fighter/SquadLeader toggle)
                                           └── Space: push(ArenaPauseScreen) (planned)
```

---

## [MEDIUM] Existing Implementations reference table is incomplete

**ID:** `DOC-006`
**Location:** `CLAUDE.md` lines 312-329 (Existing Implementations table)
**Description:** The reference table of screens, views, modals, and components does not include any arena-mode entries. Developers use this table to find examples of each UI layer type.
**Impact:** New arena contributors cannot find reference implementations for arena screens/views.

**Draft text** -- add rows to the table:

```markdown
| Screen | `ArenaGameScreen` | `ui/screens/arena_game_screen.h` | `ui/screens/arena/arena_game_screen.cpp` |
| Screen | `ArenaConfigScreen` | `ui/screens/arena_config_screen.h` | `ui/screens/arena/arena_config_screen.cpp` |
| View | `ArenaGameView` | `ui/views/arena_game_view.h` | `ui/views/arena_game_view.cpp` |
| View | `ArenaGameInfoView` | `ui/views/arena_game_info_view.h` | `ui/views/arena_game_info_view.cpp` |
| View | `ArenaConfigView` | `ui/views/arena_config_view.h` | `ui/views/arena_config_view.cpp` |
| View | `NetDesignerView` | `ui/views/net_designer_view.h` | `ui/views/net_designer_view.cpp` |
| View | `GameView` | `ui/views/game_view.h` | `ui/views/game_view.cpp` |
| Modal | `FighterPairingModal` | `ui/modals/fighter_pairing_modal.h` | `ui/modals/fighter_pairing_modal.cpp` |
```

---

## [MEDIUM] GameMode enum not documented

**ID:** `DOC-007`
**Location:** `CLAUDE.md` (config.h annotation on line 61)
**Description:** `config.h` now defines `enum class GameMode { Scroller, Arena }` but CLAUDE.md's annotation for config.h says only "GameConfig: scoring, population, ship, world params". The GameMode enum is the top-level mode selector for the application.
**Impact:** Contributors would not know the mode enum exists or where to find it.

**Draft text** -- update the config.h annotation in the file tree:

```
│   ├── config.h                  — GameMode enum, GameConfig: scoring, population, ship, world params
```

---

## [MEDIUM] Genome filesystem layout change not documented

**ID:** `DOC-008`
**Location:** `CLAUDE.md` (Genome Management section, lines 167-179)
**Description:** CLAUDE.md describes the genome directory layout as flat: `genome.bin`, `{variant}.bin`, `lineage.json`. The team genome UI spec (2026-03-28) introduced per-net-type subdirectories: `squad/` for squad net snapshots with their own `lineage.json`, and `commander/` planned for the future. This layout change is not reflected in CLAUDE.md.
**Impact:** A developer working on genome management would not know about the `squad/` subdirectory or that different net types are stored separately.

**Draft text** -- replace the directory listing in "## Genome Management (Hangar)":

```markdown
Genomes are stored as directories in `data/genomes/{name}/`:
- `genome.bin` -- root fighter snapshot (NFS format v7, CRC32)
- `{variant name}.bin` -- fighter variant snapshots (same format)
- `lineage.json` -- fighter ancestry tree
- `squad/` -- squad leader net subdirectory
  - `{squad variant}.bin` -- squad leader snapshots (NetType::SquadLeader)
  - `lineage.json` -- squad-specific lineage tree
- `commander/` -- (planned) commander net subdirectory
- `~autosave.bin` -- auto-save sentinel (atomic writes)
- `data/genomes/genomic_lineage.json` -- cross-genome lineage (genome-to-genome promotion links)
```

---

## [MEDIUM] component net_viewer.h listed in file tree but does not exist

**ID:** `DOC-009`
**Location:** `CLAUDE.md` line 51 (components section of file tree)
**Description:** The file tree lists `include/neuroflyer/components/net_viewer.h` as "Neural net viewer/editor". This file does not exist on disk. The net viewer functionality lives in `include/neuroflyer/ui/views/net_viewer_view.h` (the UIView wrapper) and in the `neuralnet-ui` library's rendering functions. Similarly, `src/ui/views/net_viewer.cpp` is listed in the tree but does not exist.
**Impact:** Contributors would look for a non-existent file.

**Draft text** -- remove from the components section of the file tree:

```
(remove) │   │   ├── net_viewer.h          — Neural net viewer/editor
```

And remove from the Existing Implementations table:

```
(remove) | Component | `net_viewer` | `components/net_viewer.h` | `ui/views/net_viewer.cpp` |
```

---

## [MEDIUM] feature-audit.md references stale snapshot versions

**ID:** `DOC-010`
**Location:** `docs/feature-audit.md` line 24
**Description:** The feature audit table says "Save/Load (NFS format) | Current | snapshot_io.cpp | v1/v2/v3, CRC32, backward compat". The actual format is now v1-v7 with v4 adding sensor IDs, v5 adding run_count, v6 adding paired_fighter_name, and v7 adding NetType.
**Impact:** Feature audit readers get a wrong picture of format maturity.

**Draft text** -- update the row:

```
| Save/Load (NFS format) | Current | `snapshot_io.cpp` | v1-v7, CRC32, backward compat. v5+ adds run_count, v6+ adds paired_fighter_name, v7+ adds NetType. |
```

---

## [MEDIUM] feature-audit.md missing arena systems entirely

**ID:** `DOC-011`
**Location:** `docs/feature-audit.md`
**Description:** The feature audit has no rows for any arena system: ArenaSession, ArenaMatch, ArenaSensor, SquadLeader, SectorGrid, TeamEvolution, or the arena UI screens/views. The audit was last updated 2026-03-26, before arena mode was implemented.
**Impact:** The audit no longer represents the full system inventory.

**Draft text** -- add a new section "## Arena Systems" to feature-audit.md:

```markdown
## Arena Systems

| System | Status | Location | Notes |
|--------|--------|----------|-------|
| Arena session (shared world simulation) | Current | `arena_session.cpp` | Tick-based, shared entity world, teams, squads |
| Arena match orchestration | Current | `arena_match.cpp` | Builds nets from TeamIndividuals, runs rounds, scores |
| Arena sensors | Current | `arena_sensor.cpp` | Detects towers, tokens, ships (friendly/enemy), bullets |
| Squad leader + NTM | Current | `squad_leader.cpp` | Sector grid queries, NTM threat selection, tactical orders |
| Sector grid | Current | `sector_grid.cpp` | 2D spatial index, Manhattan diamond queries |
| Team evolution | Current | `team_evolution.cpp` | 3-net TeamIndividual, squad-only mode, full team mode |
| Arena game screen | Current | `arena_game_screen.cpp` | Main arena simulation screen with follow-mode net viewer |
| Arena config screen | Current | `arena_config_screen.cpp` | Arena parameter configuration + launch |
| Base entity | Current | `base.h` | Destructible team structure, HP, bullet damage |
| Camera system | Current | `camera.h` | Zoom/pan/follow for large arena world |
| Fighter pairing modal | Current | `fighter_pairing_modal.cpp` | Selects fighter variant for squad training |
| Variant-to-fighter conversion | Current | `evolution.cpp` | `convert_variant_to_fighter()` remaps scroller inputs to arena |
```

---

## [LOW] CLAUDE.md description says "100 neural nets" but population is configurable

**ID:** `DOC-012`
**Location:** `CLAUDE.md` line 3
**Description:** The opening description says "100 neural nets learn to dodge towers" but the population size is configurable (default 100 for scroller, different for arena). Arena default is `num_teams * num_squads * fighters_per_squad` which is 16 at default settings.
**Impact:** Minor inaccuracy in the project summary. Low priority.

**Draft text** -- update the opening line:

```markdown
A neural net playground -- vertical-scrolling arcade game where neural nets learn to dodge towers and collect tokens via neuroevolution, with real-time brain visualization, a genome management hangar, and a team-based arena battle mode.
```

---

## [LOW] Key Design Decisions section missing arena decisions

**ID:** `DOC-013`
**Location:** `CLAUDE.md` lines 331-343 (Key Design Decisions)
**Description:** The Key Design Decisions section documents only scroller-era decisions. Major arena design decisions are not listed: shared entity types between modes, hierarchical 3-net brain architecture, sector grid for spatial indexing, squad-only training mode, 1-hot output groups for discrete squad leader orders.
**Impact:** Contributors would not understand the rationale behind arena architecture choices.

**Draft text** -- add to Key Design Decisions:

```markdown
- **Shared entity layer, separate orchestrators** -- `Triangle`, `Tower`, `Token`, `Bullet` are shared types. `GameSession` (scroller) and `ArenaSession` (arena) compose them differently. No class hierarchy between modes.
- **Hierarchical 3-net brain** -- NTM (threat scoring) + Squad Leader (tactical orders) + Fighter (local control). Each net type evolves together in `TeamIndividual`. The NTM pattern (shared weights, duplicated per threat, top-1 selection) handles variable entity counts through a fixed-size input interface.
- **1-hot output groups** -- Squad leader uses argmax within output groups (tactical: 3 nodes, spacing: 2 nodes) to force discrete decisions rather than continuous blends.
- **Squad-only training mode** -- `evolve_squad_only()` freezes fighter weights and evolves only squad leader + NTM nets. Lets users develop coordination strategy on top of a known-good fighter variant.
- **Sector grid for NTM** -- Manhattan distance diamond on a fixed-size sector grid for O(1) "nearby entity" queries instead of O(N^2) pairwise distance checks.
- **ArenaGameView as SDL renderer** -- Arena game panel uses direct SDL rendering (not ImGui) for performance with many entities, matching the scroller's GameView pattern.
```

---

## [LOW] Controls table only covers scroller mode

**ID:** `DOC-014`
**Location:** `CLAUDE.md` lines 218-226 (Controls section)
**Description:** The controls table documents only scroller keybindings. Arena mode has different/additional controls: Tab cycles through ships (follow mode), arrow keys switch to free camera, scroll wheel zooms, 1-4 sets speed.
**Impact:** Users of arena mode cannot find keybindings in the documentation.

**Draft text** -- add an "Arena Controls" sub-section:

```markdown
### Arena Controls

| Key | Action |
|-----|--------|
| Tab | Cycle through ships (follow mode) |
| Arrow Keys | Free camera pan (exits follow mode) |
| Scroll Wheel | Zoom in/out |
| 1-4 | Speed (1x/5x/20x/100x ticks per frame) |
| Space | Pause (planned: push ArenaPauseScreen) |
| Escape | Exit arena |
| F/S buttons | Toggle net viewer between Fighter and Squad Leader net |
```

---

## [LOW] renderers/ section of file tree lists removed files

**ID:** `DOC-015`
**Location:** `CLAUDE.md` lines 128-130
**Description:** The file tree under `src/ui/renderers/` lists `starfield.cpp` and `occulus.cpp`. These files no longer exist on disk -- the `src/ui/renderers/` directory contains only `variant_net_render.cpp`. The starfield rendering was absorbed into `GameView` (in `game_view.cpp`), and occulus rendering is handled by `GameView::render_occulus()`.
**Impact:** Minor confusion when navigating the codebase. The file tree says files exist that do not.

**Draft text** -- replace the renderers section:

```
│   │   └── renderers/            — SDL rendering helpers
│   │       └── variant_net_render.cpp  — NeuroFlyer-specific net render wrapper
```

---

## [LOW] Backlog items reference spec files but specs are not cross-referenced from CLAUDE.md

**ID:** `DOC-016`
**Location:** `CLAUDE.md` (docs/ section of file tree, line 133-136)
**Description:** The file tree shows `docs/superpowers/` with just a comment "Design specs and implementation plans" but does not list the spec or plan files. There are now 16 spec files and 14 plan files covering major features. A contributor looking for design rationale would not know what specs exist without listing the directory.
**Impact:** Low -- specs are discoverable by browsing, but a brief listing in CLAUDE.md would improve discoverability.

**Draft text** -- expand the docs section of the file tree:

```
└── docs/
    ├── backlog.md                — Feature backlog (keep updated!)
    ├── feature-audit.md          — System-by-system status audit
    └── superpowers/
        ├── specs/                — Design specifications (16 specs)
        │   ├── 2026-03-28-arena-mode-design.md
        │   ├── 2026-03-28-arena-perception-design.md
        │   ├── 2026-03-28-team-genome-ui-design.md
        │   ├── 2026-03-30-squad-leader-design.md
        │   ├── 2026-03-30-arena-net-viewer-design.md
        │   ├── 2026-03-30-arena-pause-screen-design.md
        │   └── ... (10 more covering save system, sensors, UI framework, etc.)
        └── plans/                — Implementation plans (14 plans)
```

---

## Appendix: Completeness Checklist

| Question | Answer |
|----------|--------|
| Does CLAUDE.md mention arena mode? | NO -- `DOC-001` |
| Does CLAUDE.md list ArenaGameScreen / ArenaConfigScreen? | NO -- `DOC-003`, `DOC-005`, `DOC-006` |
| Does CLAUDE.md list ArenaGameView / ArenaGameInfoView / ArenaConfigView? | NO -- `DOC-003`, `DOC-006` |
| Does CLAUDE.md list NetDesignerView / GameView? | NO -- `DOC-003`, `DOC-006` |
| Does CLAUDE.md list FighterPairingModal? | NO -- `DOC-003`, `DOC-006` |
| Does CLAUDE.md describe squad leader system / NTM? | NO -- `DOC-001` |
| Does CLAUDE.md describe sector grid? | NO -- `DOC-001` |
| Does CLAUDE.md describe NetType enum? | NO -- `DOC-004` |
| Does CLAUDE.md list correct snapshot version? | NO (says v1-v4, actual is v1-v7) -- `DOC-002` |
| Does CLAUDE.md describe team evolution? | NO -- `DOC-001` |
| Does CLAUDE.md list base.h / camera.h / theme.h? | NO -- `DOC-003` |
| Does CLAUDE.md list GameMode enum? | NO -- `DOC-007` |
| Does CLAUDE.md describe genome subdirectory layout (squad/)? | NO -- `DOC-008` |
| Does feature-audit.md cover arena systems? | NO -- `DOC-011` |
| Does feature-audit.md have correct snapshot version? | NO -- `DOC-010` |
| Does CLAUDE.md list files that no longer exist? | YES (3 files) -- `DOC-009`, `DOC-015` |

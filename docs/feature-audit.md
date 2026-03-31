# NeuroFlyer Feature Audit

**Date:** 2026-03-26

## Status Legend
- **Current** — Fully migrated, no legacy code, working correctly
- **Deprecated** — Replaced by a newer system, code remains, should be removed
- **Partial** — Old and new code coexist, needs cleanup
- **Backlog** — Planned but not implemented

---

## Core Systems

| System | Status | Location | Notes |
|--------|--------|----------|-------|
| Screen navigation (7 screens) | Current | `main.cpp`, `screens/` | Frame-based fly session, thin dispatcher |
| Sensor engine (detection + input encoding) | Current | `sensor_engine.cpp` | Single source of truth. Raycast + Occulus. |
| Output decoding | Current | `sensor_engine.cpp` | `decode_output()` — only path |
| Collision math | Current | `collision.h` | `ray_circle_intersect`, `point_in_circle`, bullet/triangle/circle (rotated + non-rotated) |
| Game session (tick, spawning, scoring) | Current | `game.cpp` | Position multipliers, difficulty ramping |
| Evolution (StructuredGenome) | Current | `evolution.cpp` | Weight + sensor + topology genes, linkage groups |
| Neural net rendering | Current | `renderer.cpp` | Deferred draws, display order permutation |
| Save/Load (NFS format) | Current | `snapshot_io.cpp` | v1-v7, CRC32, backward compat |
| Genome management + lineage | Current | `genome_manager.cpp` | Per-genome + cross-genome lineage, atomic saves |
| MRCA tracking | Current | `mrca_tracker.cpp` | Elite lineage, pruning, degradation |
| Auto-save + crash recovery | Current | `genome_manager.cpp` | `~autosave.bin` sentinel, atomic writes |
| Config (GameConfig) | Current | `config.cpp` | Game rules + app settings only. Zero sensor config. |
| Constants | Current | `constants.h`, `sensor_engine.h` | No duplicates. PI from `<numbers>`. |
| Ship sprites | Current | `renderer.cpp` | 10 types, 4 frames each |
| Arena session | Current | `arena_session.cpp` | Top-down arena world: rotated ships, directional bullets, bases, squad stats |
| Arena match | Current | `arena_match.cpp` | Multi-generation match runner for team evolution |
| Arena sensors | Current | `arena_sensor.cpp` | Rotation-aware sensors, `build_arena_ship_input()`, `is_full_sensor` entity channels |
| Fighter drill session | Current | `fighter_drill_session.cpp` | Phase-based training (Expand/Contract/Attack) with scripted squad inputs |
| Sector grid | Current | `sector_grid.cpp` | Spatial index for NTM nearby-enemy queries |
| Squad leader | Current | `squad_leader.cpp` | NTM execution, squad leader net, order decoding |
| Team evolution | Current | `team_evolution.cpp` | TeamIndividual (3 nets), `evolve_team_population()`, `evolve_squad_only()` |
| Base | Current | `base.h` | Team starbase with health and team_id |
| Camera | Current | `camera.h` | Pan/zoom viewport for arena rendering |
| NetType enum | Current | `snapshot.h` | Solo/Fighter/SquadLeader/NTM — drives net viewer labels |

## Components

| Component | Status | Location | Notes |
|-----------|--------|----------|-------|
| Test bench | Current | `test_bench.cpp` (1,201 lines) | Reads ShipDesign from variant, click-to-select sensors, mirror mode, resolution analysis |
| Net viewer | Current | `net_viewer.cpp` | Memory slots derived from topology (read-only) |
| Lineage graph | Current | `lineage_graph.cpp` | Per-genome + cross-genome ancestor/child nodes |
| Fitness editor | Current | `fitness_editor.cpp` | Simple slider panel |
| Game UI (modal, highlight_list) | Current | `game-ui/` | Reusable ImGui widgets |
| Arena config screen | Current | `arena_config_screen.cpp` | Arena configuration UI |
| Arena game screen | Current | `arena_game_screen.cpp` | Arena training loop + follow mode net viewer |
| Arena pause screen | Current | `arena_pause_screen.cpp` | Squad leader + NTM variant saving |
| Arena game view | Current | `arena_game_view.cpp` | Arena world SDL renderer |
| Arena game info view | Current | `arena_game_info_view.cpp` | Arena HUD (team stats, scores) |
| Arena config view | Current | `arena_config_view.cpp` | Arena config panel |
| Fighter drill screen | Current | `fighter_drill_screen.cpp` | Fighter drill training loop UI |
| Fighter drill pause screen | Current | `fighter_drill_pause_screen.cpp` | Evolution config + variant saving for drills |

## Deprecated / Should Remove

| Item | Location | Replaced By | Action Needed |
|------|----------|-------------|---------------|
| **`ray.cpp` — cast_rays / cast_rays_with_endpoints** | `ray.cpp` (105 lines) | `sensor_engine.cpp` → `query_sensors_with_endpoints` | Remove. Fly session line ~287 still calls `cast_rays_with_endpoints` for ray visualization alongside the sensor endpoint system. Consolidate. |
| **`renderer.init_occulus_fields` / `set_occulus_config`** | Already deleted | `compute_sensor_shape` in sensor_engine | Done ✓ |
| **GameConfig sensor fields** | Already deleted | ShipDesign per variant | Done ✓ |

## Partially Migrated

| Item | Location | What Remains | Action Needed |
|------|----------|-------------|---------------|
| **Ray visualization in fly mode** | `fly_session.cpp:287` | Still calls `cast_rays_with_endpoints` for drawing rays alongside `query_sensors_with_endpoints`. Two visualization paths coexist. | Use only `query_sensors_with_endpoints` for all visualization. Remove `cast_rays_with_endpoints` call. |
| **Legacy ShipDesign fallback** | `sensor_engine.cpp:255` | `create_legacy_ship_design()` fills 13-ray default when `design.sensors` is empty | Keep for backward compat with old genomes. Document clearly. Will naturally die as old genomes are deleted. |
| **`net_viewer.cpp` "Create Random Net"** | `net_viewer.cpp:102-111` | Uses hardcoded `SENSOR_SIZE = 31` and local `edit_memory_slots` to build a random network. Doesn't use ShipDesign. | Should create from a ShipDesign (use the loaded variant's design, or a default). |

## Known Bugs

| Bug | Location | Severity | Fix |
|-----|----------|----------|-----|
| **MRCA tracker assertion on small populations** | `mrca_tracker.cpp:48-51` | Medium — crashes if `population.size() < elitism_count` | Clamp `elite_count_` or relax assertion in `record_generation()` |

## Backlog (Not Started)

| # | Feature | Description |
|---|---------|-------------|
| 1 | MRCA history on pause screen | Show elite lineage tree during training pause |
| 2 | Lineage tree colorization | Color by evolutionary distance, sensor composition, node count, layer count |
| 3 | Evolution settings UI | Expose mutation rates, crossover toggle, tournament size in a settings page |
| 4 | Manual variant recombination | Select 2 variants → sexual reproduction, or 1 → asexual. Preview child before saving. |
| 5 | Graphics/audio settings | Resolution, fullscreen, vsync, audio — main menu Settings page |
| 6 | Topology bottleneck prevention | Min layer size on insertion, weight clamping, targeted layer removal (see `docs/topology-bottleneck-analysis.md`) |
| 7 | Sensor noise + refresh rate | `SensorDef.noise` and `SensorDef.refresh_rate` evolvable parameters |
| 8 | Headless parallelization | Multi-threaded headless mode (individuals don't interact → embarrassingly parallel) |

## Code Size Summary

*Last updated: 2026-03-31. Counts are approximate.*

| Category | Files | Lines (approx) |
|----------|-------|-------|
| Core engine src/ | 17 | ~7,000 |
| Screens | 12 | ~5,500 |
| Views/Components | 14 | ~3,500 |
| Game UI lib | 2 | 338 |
| Tests | 21 | ~4,000 |
| Headers | 45+ | ~3,500 |
| **Total** | **110+** | **~24,000** |

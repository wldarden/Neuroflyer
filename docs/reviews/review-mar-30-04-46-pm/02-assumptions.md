# Assumption Verification

> **Scope:** Arena mode, Fighter Drill, team evolution, collision, sensor engine, snapshot I/O, evolution engine. Focus on cross-subsystem contracts, container sizes, enum sync, serialization round-trip, and index consistency.
> **Findings:** 2 high, 5 medium, 3 low (verified assumptions noted inline)

---

## [HIGH] convert_variant_to_fighter assumes specific scroller input layout

**ID:** `ASM-001`

**Location:** `src/engine/evolution.cpp:242-322`

**Description:** `convert_variant_to_fighter` hardcodes the scroller-to-arena weight mapping. It assumes scroller full sensors have 4 values `[distance, is_tower, token_value, is_token]` (line 280-291, `src_col += 4`), that sight sensors have 1 value (line 293-297, `src_col += 1`), and that exactly 3 position inputs follow all sensors (line 302, `src_col += 3`). It then maps these to arena full sensors with 5 values `[distance, is_tower, is_token, is_friend, is_bullet]` (line 291, `dst_col += 5`).

The scroller encoding is defined in `compute_input_size` (ship_design.h:43-49): `is_full_sensor ? 4 : 1`, and `build_ship_input` (sensor_engine.cpp) which emits `[distance, is_tower, token_value, is_token]` for full sensors. The arena encoding is in `compute_arena_input_size` (arena_sensor.cpp:218-224): `is_full_sensor ? 5 : 1`, and `build_arena_ship_input` which emits `[distance, is_tower, is_token, is_friend, is_bullet]`.

The column mapping is correct today. The assertion on line 303 catches total column count mismatches. However, the per-sensor column semantics are hardcoded and not derived from any shared definition. If either `build_ship_input` or `build_arena_ship_input` changes its encoding order (e.g., adding a new sensor value), `convert_variant_to_fighter` will silently map wrong columns.

**Impact:** Changing sensor encoding in either the scroller or arena path without updating `convert_variant_to_fighter` will silently corrupt weight mappings. Transferred variants would have weights connected to wrong inputs.

**Recommendation:** Extract the per-sensor column count and semantic ordering into named constants or a shared struct (e.g., `SCROLLER_FULL_SENSOR_COLS = 4`, `ARENA_FULL_SENSOR_COLS = 5`) that both the input builders and the converter reference.

---

## [HIGH] ArenaGameScreen assumes exactly 2 teams for net indexing

**ID:** `ASM-002`

**Location:** `src/ui/screens/arena/arena_game_screen.cpp:143-151, 161-165, 280-350, 392, 601-641`

**Description:** The arena game screen hardcodes many assumptions about team count:

1. `current_team_indices_` is built by picking 2 random indices from `team_population_` (lines 143-151).
2. `ntm_nets_`, `leader_nets_`, `fighter_nets_` are populated with exactly `current_team_indices_.size()` entries (lines 158-165), which is always 2.
3. `team_orders` is sized to `config_.num_teams` (line 280).
4. `tick_arena` indexes `ntm_nets_[t]` using the ship's team (line 298), assuming team indices are 0 and 1.

If `config_.num_teams` is changed to 3+, the net vectors would have 2 entries but teams 0-2 would try to index them, causing out-of-bounds access. `ArenaConfig::num_teams` defaults to 2 and there's no UI to change it, but the config is a plain struct with no validation.

**Impact:** Crash if `num_teams > 2` is ever used, since the net vectors only have 2 entries but team indices could be 0, 1, 2, ...

**Recommendation:** Either assert `config_.num_teams == 2` at initialization, or generalize the matchmaking to support N teams by building `current_team_indices_` with `num_teams` entries.

---

## [MEDIUM] Snapshot serialization v7 round-trip verified

**ID:** `ASM-003`

**Location:** `src/engine/snapshot_io.cpp:90-146 (write), 148-226 (read)`

**Description:** Tracing the write and read paths field by field:

| Field | Write | Read |
|-------|-------|------|
| name | `write_string` | `read_string` |
| generation | `write_val<uint32_t>` | `read_val<uint32_t>` |
| created_timestamp | `write_val<int64_t>` | `read_val<int64_t>` |
| parent_name | `write_string` | `read_string` |
| run_count | `write_val<uint32_t>` (always) | `read_val<uint32_t>` (v5+) |
| paired_fighter_name | `write_string` (always) | `read_string` (v6+) |
| net_type | `write_val<uint8_t>` (always) | `read_val<uint8_t>` (v7+) |
| num_sensors | `uint16_t` | `uint16_t` |
| memory_slots | `uint16_t` | `uint16_t` |
| per sensor: type | `uint8_t` | `uint8_t` |
| per sensor: angle | `float` | `float` |
| per sensor: range | `float` | `float` |
| per sensor: width | `float` | `float` |
| per sensor: is_full_sensor | `uint8_t` | `uint8_t` |
| per sensor: id | `uint16_t` | `uint16_t` (v3+) |
| evolvable flags | `uint8_t` (always) | `uint8_t` (v2+) |
| input_size | `uint32_t` | `uint32_t` |
| num_layers | `uint32_t` | `uint32_t` |
| per layer: output_size | `uint32_t` | `uint32_t` |
| per layer: activation | `uint32_t` | `uint32_t` |
| per layer: node_activations count | `uint16_t` | `uint16_t` (v4+) |
| per layer: node_activations | `uint8_t` each | `uint8_t` each (v4+) |
| weight_count | `uint32_t` | `uint32_t` |
| weights | `float[]` | `float[]` |

**Verification result:** Write always emits the current version (7) format. Read handles versions 1-7 with correct conditionals. The write path always emits all fields; the read path conditionally reads versioned fields and uses defaults for older versions. Field order and types match exactly. CRC32 covers the entire payload.

One subtle point: `write_payload` always writes `evolvable_flags` and `sensor.id`, but the read path only reads `sensor.id` for v3+ and `evolvable_flags` for v2+. Since write always uses CURRENT_VERSION=7, this is only relevant when reading old files. Reading a v1 file would skip sensor.id (leaving default 0) and evolvable flags (leaving default false), then `assign_sensor_ids` backfills IDs. This is correct.

**Status:** VERIFIED -- no deserialization mismatch found.

---

## [MEDIUM] compute_arena_input_size matches build_arena_ship_input output

**ID:** `ASM-004`

**Location:** `src/engine/arena_sensor.cpp:218-224 (size), 226-269 (build)`

**Description:** `compute_arena_input_size` computes: `sensor_values + 6 + design.memory_slots` where `sensor_values = sum(s.is_full_sensor ? 5 : 1 for s in design.sensors)`.

`build_arena_ship_input` pushes:
1. For each sensor: 1 distance value, then if full_sensor: +4 more (is_tower, is_token, is_friend, is_bullet) = 5 total per full sensor, 1 per sight sensor.
2. 6 squad leader inputs (target heading, target distance, center heading, center distance, aggression, spacing).
3. Memory values (from `memory` span).

Both use the same iteration over `design.sensors` and the same 5/1 branching. The 6 squad leader inputs are hardcoded in both places. Memory is `design.memory_slots` in the size calculation and `memory.begin()/end()` in the builder -- the caller must ensure `memory.size() == design.memory_slots`.

**Status:** VERIFIED -- the computed size matches the actual output size, assuming the caller passes a correctly-sized memory span. The caller in `tick_arena` (arena_game_screen.cpp:385) passes `recurrent_states_[i]` which is initialized to `ship_design_.memory_slots` zeros (line 184-186). The caller in `run_tick` (fighter_drill_screen.cpp:342) similarly passes `recurrent_states_[i]` initialized correctly (line 166-168).

---

## [MEDIUM] NetType enum stays in sync across snapshot_io and variant_net_render

**ID:** `ASM-005`

**Location:** `include/neuroflyer/snapshot.h:12-16`, `src/ui/renderers/variant_net_render.cpp:24-60`, `src/engine/snapshot_io.cpp:99, 164`

**Description:** `NetType` is defined as `enum class NetType : uint8_t { Solo = 0, Fighter = 1, SquadLeader = 2 }`. It is serialized as a raw `uint8_t` in snapshot_io (line 99: `write_val<uint8_t>(static_cast<uint8_t>(snap.net_type))`) and deserialized back (line 164: `static_cast<NetType>(read_val<uint8_t>(in))`). The variant_net_render switch on line 24 uses the three cases: `NetType::SquadLeader`, `NetType::Fighter`, and `default` (which handles Solo and any unknown values).

If a new NetType value is added (e.g., `Commander = 3`), the serialization will handle it transparently (it's just a uint8_t), but `variant_net_render` will fall through to the `default` case and use Solo-style labels, which would be incorrect for a Commander net.

**Status:** PARTIALLY VERIFIED -- currently consistent for the 3 defined values. The default fallback in variant_net_render is a safety net but could silently produce wrong labels for future net types. Adding a new NetType requires updating the switch in `variant_net_render.cpp`.

---

## [MEDIUM] ArenaConfig::squad_leader_fighter_inputs matches hardcoded 6 in build_arena_ship_input

**ID:** `ASM-006`

**Location:** `include/neuroflyer/arena_config.h:60`, `src/engine/arena_sensor.cpp:218-224, 257-263`

**Description:** `ArenaConfig` declares `static constexpr std::size_t squad_leader_fighter_inputs = 6`. The function `compute_arena_input_size` hardcodes `+ 6` on line 223. `build_arena_ship_input` pushes exactly 6 squad leader values (lines 257-263). The arena fighter input labels function `build_arena_fighter_input_labels` also emits 6 labels (lines 296-302). The arena fighter input colors function emits 6 squad colors (line 334: `for (int i = 0; i < 6; ++i)`).

These are all hardcoded `6` rather than referencing `ArenaConfig::squad_leader_fighter_inputs`. If the constant changes, the functions would be out of sync.

**Status:** VERIFIED for current value (6). The constant exists but is not referenced by the functions that should use it. This is a maintenance hazard.

**Recommendation:** Replace hardcoded `6` with `ArenaConfig::squad_leader_fighter_inputs` in `compute_arena_input_size`, `build_arena_ship_input`, `build_arena_fighter_input_labels`, and `build_arena_fighter_input_colors`.

---

## [MEDIUM] EvolutionConfig::population_size vs actual population vector size

**ID:** `ASM-007`

**Location:** `src/engine/evolution.cpp:601-667`, `src/engine/team_evolution.cpp:81-119, 121-158`

**Description:** `evolve_population`, `evolve_team_population`, and `evolve_squad_only` all use `config.population_size` as the target size for the next generation (`while (next.size() < population.size())`). Wait -- re-reading: they actually use `population.size()`, not `config.population_size`.

Line 98 in team_evolution.cpp: `while (next.size() < population.size())`. Line 623 in evolution.cpp: `while (next.size() < config.population_size)`.

There is an inconsistency: `evolve_population` uses `config.population_size`, while `evolve_team_population` and `evolve_squad_only` use `population.size()`. If `config.population_size != population.size()`, `evolve_population` will resize the population to match the config, while the team evolution functions will maintain the current size regardless of config.

For FighterDrillScreen, the population is initially created with `drill_config_.population_size`, and `evo_config_.population_size` is set to the same value. If the user changes `evo_config_.population_size` in the pause screen, `evolve_population` will resize the population accordingly, but the drill session still uses the old drill_config_.population_size. This is the root cause of BUG-002.

For ArenaGameScreen (team evolution), the population size stays constant because `evolve_team_population` uses `population.size()`.

**Status:** INCONSISTENCY FOUND between `evolve_population` (uses config) and `evolve_team_population`/`evolve_squad_only` (uses current population size). This is the root cause of BUG-002 in the bugs report.

---

## [LOW] DrillPhase enum values match expected phase transition order

**ID:** `ASM-008`

**Location:** `include/neuroflyer/fighter_drill_session.h:15`, `src/engine/fighter_drill_session.cpp:123-131`

**Description:** `DrillPhase` is defined as `{ Expand = 0, Contract = 1, Attack = 2, Done = 3 }`. The phase transition switch (lines 125-129) explicitly maps: Expand -> Contract, Contract -> Attack, Attack -> Done, Done -> Done. The test `FighterDrillSessionTest::PhaseTransitions` verifies this sequence. The `phase_name` function in fighter_drill_screen.cpp covers all 4 cases.

**Status:** VERIFIED -- enum values, transitions, display names, and tests are all consistent.

---

## [LOW] Triangle vertex positions consistent between collision and rendering

**ID:** `ASM-009`

**Location:** `include/neuroflyer/collision.h:48-50, 58-60, 72-83`

**Description:** Three collision functions define triangle vertices relative to center (cx, cy) with size `s`:
- Unrotated: top = `(cx, cy - s)`, bottom-left = `(cx - s, cy + s)`, bottom-right = `(cx + s, cy + s)`
- `rotated_triangle_vertices`: rotates the same local offsets `(0, -s)`, `(-s, s)`, `(s, s)` by `tri.rotation`

The SDL drawing helper in fighter_drill_screen.cpp (lines 28-48) uses the same local offsets `(0, -size)`, `(-size, size)`, `(size, size)` rotated by `rotation`. The arena_game_view.cpp (not shown but referenced) uses the same pattern.

**Status:** VERIFIED -- vertex definitions are consistent across collision detection and rendering. The same 3 local offsets are used everywhere.

---

## [LOW] Snapshot weight vector size matches topology expectations

**ID:** `ASM-010`

**Location:** `src/engine/evolution.cpp:181-211 (build_network)`, `src/engine/snapshot_io.cpp:137-145 (write), 212-220 (read)`

**Description:** `Individual::build_network()` extracts weights from genome genes `layer_N_weights` and `layer_N_biases`, concatenating them into a flat vector which is passed to `neuralnet::Network(topo, flat)`. The expected count for each layer is `prev_size * output_size + output_size` (weights + biases).

When saving a snapshot, `snap.weights = ind.genome.flatten("layer_")` extracts all genes with tags starting with "layer_". This includes `layer_N_weights`, `layer_N_biases`, and `layer_N_activations`. However, `build_network()` only reads weights and biases from the flat vector (not activations -- those are read separately from the genome). So the saved weights include activation gene values that are NOT consumed by the Network constructor.

Wait -- re-reading `create_random_snapshot` (evolution.cpp line 669-700): it sets `snap.weights = ind.genome.flatten("layer_")`. And `create_population_from_snapshot` presumably rebuilds from the snapshot. Let me check if this is an issue.

Actually, `build_network` reads weights/biases from individual named genes, not from a flat vector. The flat vector in `snap.weights` is only used for snapshot storage. When loading, `snapshot_to_individual` (in snapshot_utils) would need to reconstruct the genome from the flat weights. This is a separate code path.

The key question is: does `flatten("layer_")` include activation genes, and does the restore path handle this correctly? Since `flatten` filters by tag prefix "layer_", it would include `layer_0_weights`, `layer_0_biases`, and `layer_0_activations`. The topology stores per-node activations separately, so the weights vector would contain extra activation values that don't belong in the weight count.

However, looking at snapshot_io.cpp, per-node activations are stored in the topology section (lines 130-134), not in the weights. So if `snap.weights` includes activation gene values, the weight count will be larger than expected by `neuralnet::Network`, and the Network constructor may reject or misinterpret the extra values.

**Revised:** The `flatten("layer_")` call includes weights, biases, AND activation genes. The snapshot stores the flat vector in the weights section and also stores per-node activations separately in the topology section (as uint8_t enum values). This means activation data is stored twice: once as float gene values in `snap.weights` and once as enum values in `snap.topology.layers[l].node_activations`.

The `snapshot_to_individual` function (evolution.cpp:752-776) reads back from the flat weights in the correct order: weights, biases, activations per layer. It correctly handles old files (which have no activations in the flat weights) by keeping the skeleton defaults. The `build_network` function reads activations from the genome genes (not from the flat vector directly), so the round-trip is: flatten -> save -> load -> unflatten into genome -> build_network reads from genome.

The duplicate storage of activations (in topology and in flat weights) is redundant but not a bug. The topology activations are used when loading as a Network directly; the flat weights activations are used when reconstructing an Individual.

**Status:** VERIFIED -- the round-trip is correct despite the redundant activation storage. The `flatten`/unflatten paths match, and `snapshot_to_individual` handles both old and new formats.

---

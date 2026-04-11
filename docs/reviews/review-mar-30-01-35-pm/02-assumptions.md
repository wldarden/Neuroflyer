# Assumption Verification Report -- 2026-03-30

Reviewer: Claude Opus 4.6 (automated overnight review)
Scope: Full NeuroFlyer repo, focused on arena mode (last 30 commits)

---

## [CRITICAL] Fighter input labels/colors count mismatches actual input size

**ID:** `ASM-001`
**Location:** `src/ui/renderers/variant_net_render.cpp:37-43` vs `src/engine/arena_sensor.cpp:117-123`
**Description:** The renderer assumes Fighter nets use the same input layout as Solo nets by calling `build_input_labels(ship_design)`, which produces labels for: sensors (1 or 4 per sensor) + 3 position inputs (POS X, POS Y, SPEED) + memory. But `compute_arena_input_size()` computes: sensors (1 or 5 per sensor) + 6 squad leader inputs + memory. Two mismatches:
1. Full sensors produce 4 values in scroller but 5 values in arena.
2. The "system" block is 3 inputs (position) in scroller but 6 inputs (squad leader) in arena.
For a design with 5 full sensors and 8 sight sensors, scroller has 8 + 5*4 + 3 + 4 = 35 inputs, but arena has 8 + 5*5 + 6 + 4 = 43 inputs. The label vector will be 35 long but the network expects 43 inputs -- an 8-element deficit.
**Verdict:** BROKEN. The assumption that Solo and Fighter share input structure is false.

---

## [VERIFIED] NTM input size (7) matches actual input vector in run_ntm_threat_selection

**ID:** `ASM-002`
**Location:** `include/neuroflyer/team_evolution.h:14` vs `src/engine/squad_leader.cpp:79-87`
**Description:** `NtmNetConfig::input_size = 7`. The `run_ntm_threat_selection` function builds:
```
ntm_input = {dir_sin, dir_cos, range, health, alive_fraction, is_ship, is_starbase}
```
That is 7 elements.
**Verdict:** MATCH. The NTM input assumption holds.

---

## [VERIFIED] Squad leader input size (14) matches actual input vector in run_squad_leader

**ID:** `ASM-003`
**Location:** `include/neuroflyer/team_evolution.h:20` vs `src/engine/squad_leader.cpp:120-135`
**Description:** `SquadLeaderNetConfig::input_size = 14`. The `run_squad_leader` function builds:
```
input = {squad_health, home_sin, home_cos, home_dist, home_hp, squad_spacing,
         cmd_sin, cmd_cos, cmd_dist, ntm_active, ntm_sin, ntm_cos, ntm_dist, ntm_score}
```
That is 14 elements.
**Verdict:** MATCH. The squad leader input assumption holds.

---

## [VERIFIED] Squad leader output size (5) matches argmax decoding in run_squad_leader

**ID:** `ASM-004`
**Location:** `include/neuroflyer/team_evolution.h:22` vs `src/engine/squad_leader.cpp:139-152`
**Description:** `SquadLeaderNetConfig::output_size = 5`. The `run_squad_leader` function reads:
- output[0], output[1] -- spacing argmax (Expand vs Contract)
- output[2], output[3], output[4] -- tactical argmax (AttackStarbase, AttackShip, DefendHome)
That is 5 outputs.
**Verdict:** MATCH. The squad leader output assumption holds.

---

## [VERIFIED] Fighter input size from compute_arena_input_size matches build_arena_ship_input

**ID:** `ASM-005`
**Location:** `src/engine/arena_sensor.cpp:117-123` vs `src/engine/arena_sensor.cpp:125-168`
**Description:** `compute_arena_input_size` returns `sensor_values + 6 + memory_slots`, where `sensor_values` sums `5` per full sensor and `1` per sight sensor. `build_arena_ship_input` pushes:
- Per sensor: 1 distance + (if full: 4 more = 5 total)
- 6 squad leader inputs
- memory_slots values
The counts are identical.
**Verdict:** MATCH. The fighter input size assumption holds.

---

## [VERIFIED] Entity IDs in sector grid: ships first [0..N), bases offset by ship_count [N..N+B)

**ID:** `ASM-006`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:236-244` and `src/engine/arena_match.cpp:57-67` vs `src/engine/squad_leader.cpp:24-55`
**Description:** Both the arena game screen and the match runner insert ships with IDs [0..total_ships) and bases with IDs [total_ships..total_ships+bases.size()). The `gather_near_threats` function interprets IDs as:
- `id < ship_count` => ship
- `id >= ship_count` => base at index `id - ship_count`
This encoding is consistent across all three call sites.
**Verdict:** MATCH. Entity ID encoding is consistent.

---

## [VERIFIED] Snapshot v7 read/write symmetry for net_type field

**ID:** `ASM-007`
**Location:** `src/engine/snapshot_io.cpp:99` (write) vs `src/engine/snapshot_io.cpp:163-165` (read)
**Description:** Write path: `write_val<uint8_t>(static_cast<uint8_t>(snap.net_type))`. Read path (v7+): `snap.net_type = static_cast<NetType>(read_val<uint8_t>(in))`. Both use uint8_t and cast symmetrically. For v1-v6, `net_type` remains at its default value `NetType::Solo`, which is correct backward-compat behavior.
**Verdict:** MATCH. Serialization is symmetric.

---

## [VERIFIED] Snapshot v6 read/write symmetry for paired_fighter_name field

**ID:** `ASM-008`
**Location:** `src/engine/snapshot_io.cpp:97` (write) vs `src/engine/snapshot_io.cpp:159-161` (read)
**Description:** Write path: `write_string(out, snap.paired_fighter_name)`. Read path (v6+): `snap.paired_fighter_name = read_string(in)`. Both use the same string serialization format (uint16 length prefix + char data). For v1-v5, the field remains empty string (default), which is correct.
**Verdict:** MATCH. Serialization is symmetric.

---

## [VERIFIED] Snapshot header parsing matches full payload field order

**ID:** `ASM-009`
**Location:** `src/engine/snapshot_io.cpp:228-246` vs `src/engine/snapshot_io.cpp:148-165`
**Description:** `parse_header_fields` reads: name, generation, timestamp, parent_name, run_count (v5+), paired_fighter_name (v6+), net_type (v7+). `parse_payload` reads the same fields in the same order before continuing with ship design, topology, and weights. The header parser stops after the common header fields, which is correct since CRC validation is skipped for header-only reads.
**Verdict:** MATCH. Header and full payload parsing are consistent.

---

## [WARNING] ship_teams_ built once per match but ships can die mid-match

**ID:** `ASM-010`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:140-144`
**Description:** `ship_teams_` is built once in `start_new_match()` and never updated. This is fine because team assignment never changes during a match -- a dead ship still belongs to its team. However, the code at line 585 (`if (ship_teams_[i] == team)`) iterates all ships including dead ones, which is correct for counting total ships on a team but could be confusing.
**Verdict:** SAFE. Team assignments are immutable per match. The assumption holds.

---

## [WARNING] Arena net viewer uses scroller's build_input_colors for Fighter type

**ID:** `ASM-011`
**Location:** `src/ui/renderers/variant_net_render.cpp:38`
**Description:** `build_input_colors(config.ship_design)` returns colors for scroller layout: sensors (green/purple) + 3 system inputs (blue) + memory (red). But fighter inputs are: sensors (green/purple) + 6 squad leader inputs (should be a new color) + memory (red). The color array will be 3 entries short, and the squad leader inputs will not have their own color -- they'll incorrectly receive system-blue coloring for the first 3 and have no color for the remaining 3.
**Verdict:** BROKEN (same root cause as ASM-001). The color assumption is wrong for Fighter nets.

---

## [VERIFIED] TacticalOrder enum values match switch statements

**ID:** `ASM-012`
**Location:** `include/neuroflyer/squad_leader.h:18-22` vs `src/engine/squad_leader.cpp:158-177` and `src/engine/squad_leader.cpp:217-225`
**Description:** `TacticalOrder` has three values: AttackStarbase (0), AttackShip (1), DefendHome (2). Both switch statements in `run_squad_leader` (output target selection) and `compute_squad_leader_fighter_inputs` (aggression mapping) cover all three cases without fallthrough.
**Verdict:** MATCH. All enum values are handled.

---

## [VERIFIED] SpacingOrder enum values match switch/comparison

**ID:** `ASM-013`
**Location:** `include/neuroflyer/squad_leader.h:24-27` vs `src/engine/squad_leader.cpp:140-141, 227`
**Description:** `SpacingOrder` has two values: Expand (0), Contract (1). The argmax comparison (line 140-141) produces one or the other. The fighter input mapping (line 227) checks `== Expand` and produces +1 or -1.
**Verdict:** MATCH. Both values are handled.

---

## [VERIFIED] ArenaHitType enum consistency in arena_sensor

**ID:** `ASM-014`
**Location:** `include/neuroflyer/arena_sensor.h:13-20` vs `src/engine/arena_sensor.cpp:137-168`
**Description:** `ArenaHitType` has 6 values: Nothing, Tower, Token, FriendlyShip, EnemyShip, Bullet. All are used in `query_arena_sensor` and tested in `build_arena_ship_input`. The full_sensor encoding checks Tower, Token, FriendlyShip, EnemyShip, and Bullet. Nothing is the default when distance == 1.0f.
**Verdict:** MATCH. All enum values are produced and consumed correctly.

---

## [VERIFIED] compute_output_size matches decode_output expectations

**ID:** `ASM-015`
**Location:** `include/neuroflyer/ship_design.h:51-53` vs `src/engine/sensor_engine.cpp:242-253`
**Description:** `compute_output_size` returns `ACTION_COUNT + memory_slots` = `5 + memory_slots`. `decode_output` reads 5 action values (indices 0-4) and `memory_slots` memory values (indices 5 onwards). Both use `ACTION_COUNT` (5) consistently.
**Verdict:** MATCH. Output size assumption holds.

---

## [VERIFIED] arena_match.cpp fitness computation matches arena_game_screen.cpp

**ID:** `ASM-016`
**Location:** `src/engine/arena_match.cpp:189-237` vs `src/ui/screens/arena/arena_game_screen.cpp:563-603`
**Description:** Both compute the same four fitness components (base_damage, own_survival, alive_frac, token_frac) with the same weight names from ArenaConfig. The headless match runner in arena_match.cpp and the interactive version in arena_game_screen.cpp produce consistent fitness scores.
**Verdict:** MATCH. Fitness computation is consistent between headless and interactive paths.

---

## [WARNING] current_team_indices_ assumes num_teams == 2

**ID:** `ASM-017`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:123`
**Description:** `current_team_indices_ = {0, 1}` hardcodes exactly 2 entries. If `config_.num_teams` is changed to 3 or more, the `ntm_nets_`, `leader_nets_`, and `fighter_nets_` vectors will only have 2 entries, but `tick_arena()` iterates `config_.num_teams` teams (line 252), causing out-of-bounds access on `ntm_nets_[t]` for t >= 2.
**Verdict:** BROKEN for num_teams > 2. The assumption that num_teams == 2 is implicit and undocumented. Increasing num_teams will crash.

---

## [VERIFIED] Arena session team_of indices match sector grid entity encoding

**ID:** `ASM-018`
**Location:** `src/engine/arena_session.cpp:336-341` vs `src/engine/squad_leader.cpp:30`
**Description:** `team_of(ship_idx)` returns `team_assignments_[ship_idx]` for valid indices. In `gather_near_threats`, `ship_teams[id]` is used to check if `ship_teams[id] == squad_team`. The `ship_teams` span passed to the function is the same `ship_teams_` vector built from `arena_->team_of(i)`.
**Verdict:** MATCH. Team lookup is consistent.

---

## [VERIFIED] NtmNetConfig in header matches ArenaConfig defaults

**ID:** `ASM-019`
**Location:** `include/neuroflyer/team_evolution.h:13-16` vs `include/neuroflyer/arena_config.h:49-51`
**Description:** `NtmNetConfig` defaults: input=7, hidden={4}, output=1. `ArenaConfig` defaults: ntm_input=7, ntm_hidden={4}, ntm_output=1. However, the arena game screen uses `NtmNetConfig` default member values (line 47 of header) and never reads from `ArenaConfig.ntm_*` fields. This means changing ArenaConfig has no effect on the NTM net size -- the NtmNetConfig on the screen class uses its own defaults.
**Verdict:** MISMATCH (minor). The two config sources are redundant but disconnected. ArenaConfig stores NTM topology settings that are never read. The NtmNetConfig member on the screen class is the one actually used. Not a crash bug, but a maintainability concern.

---

## [VERIFIED] SquadLeaderNetConfig matches ArenaConfig defaults

**ID:** `ASM-020`
**Location:** `include/neuroflyer/team_evolution.h:19-23` vs `include/neuroflyer/arena_config.h:54-56`
**Description:** Same pattern as ASM-019. `SquadLeaderNetConfig` defaults: input=14, hidden={8}, output=5. `ArenaConfig` defaults: input=14, hidden={8}, output=5. Values match, but the config on the screen class is not read from ArenaConfig.
**Verdict:** MATCH (values agree). Same redundancy concern as ASM-019.

---

## [WARNING] run_arena_match bases index assumes bases_[t] is team t's base

**ID:** `ASM-021`
**Location:** `src/engine/arena_match.cpp:91`
**Description:** `float own_base_x = arena.bases()[t].x` assumes that `bases_[t]` belongs to team `t`. This works because `ArenaSession::spawn_bases()` creates bases in team order: base 0 = team 0, base 1 = team 1, etc. The assumption is valid but implicit.
**Verdict:** SAFE (by construction). Bases are spawned in team order. But if base spawning order ever changes, this breaks silently.

---

## [VERIFIED] squad_leader_fighter_inputs count (6) matches build_arena_ship_input

**ID:** `ASM-022`
**Location:** `include/neuroflyer/arena_config.h:59` vs `src/engine/arena_sensor.cpp:156-162` vs `include/neuroflyer/squad_leader.h:97-103`
**Description:** `ArenaConfig::squad_leader_fighter_inputs = 6`. `SquadLeaderFighterInputs` has 6 float fields. `build_arena_ship_input` pushes exactly 6 values after sensors and before memory. `compute_arena_input_size` adds exactly 6.
**Verdict:** MATCH. The 6-input assumption is consistent everywhere.

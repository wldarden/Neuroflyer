# Stale Code

> **Scope:** Traced all recent features (arena mode, fighter drill, NetType, rotated collisions, ArenaConfig fields, DrillPhase, ArenaHitType) through their integration points to find spots that were not updated when new features were added.
> **Findings:** 2 critical, 3 high, 2 medium, 2 low

---

## [CRITICAL] FighterDrillPauseScreen does not sync per-node activations when saving variants

**ID:** `STALE-001`

**Location:** `src/ui/screens/game/fighter_drill_pause_screen.cpp:249-273`

**Description:** When saving fighter drill variants, the code builds a `Snapshot` from the individual's topology and flattened weights (`ind.genome.flatten("layer_")`), but it does **not** sync the evolved per-node activation functions from the genome's `_activations` genes into `snap.topology.layers[l].node_activations`.

Both sibling save paths DO perform this sync:
- `src/ui/screens/game/pause_config_screen.cpp:342-356` (scroller save)
- `src/ui/screens/arena/arena_pause_screen.cpp:212-225` (arena squad save)

The fighter drill save at `fighter_drill_pause_screen.cpp:249-273` skips this step entirely. If activation functions have evolved during training (when `evolvable.activation_function` is true), the saved snapshot will contain default activations instead of the evolved ones.

**Impact:** Data loss -- saved fighter drill variants silently lose their evolved per-node activation functions. When reloaded, the network behaves differently from what was trained.

**Fix plan:**
1. Add the per-node activation sync loop (identical to the one in `pause_config_screen.cpp:342-356`) between `snap.weights = ind.genome.flatten(...)` and the `save_snapshot()` call.
2. Copy from the pattern in `arena_pause_screen.cpp:212-225`.

---

## [CRITICAL] NTM snapshots saved with `NetType::Solo` instead of a dedicated NTM type

**ID:** `STALE-002`

**Location:** `src/ui/screens/arena/arena_pause_screen.cpp:244`

**Description:** When saving NTM companion snapshots from the arena pause screen, the code sets `ntm_snap.net_type = NetType::Solo`. The NTM net is NOT a solo scroller variant -- it has a completely different input/output structure (7 inputs -> 1 output for threat scoring). Labeling it as `Solo` means:

1. The variant viewer will display it with solo scroller labels (pos_x, pos_y, speed, sensor labels) instead of NTM-specific labels (threat features).
2. The net viewer will render it with the wrong input/output label set.
3. There is no way to distinguish NTM snapshots from solo snapshots in the hangar.

The `NetType` enum (`snapshot.h:12-16`) has three values: `Solo`, `Fighter`, `SquadLeader`. There is no `NTM` value. When the NTM save path was added, it used `Solo` as a fallback rather than adding a new enum value.

**Impact:** Incorrect labeling in the UI. NTM snapshots appear as solo variants with wrong labels. Users cannot identify them as NTM nets.

**Fix plan:**
1. Add `NTM = 3` to the `NetType` enum in `snapshot.h`.
2. Update `arena_pause_screen.cpp:244` to use `NetType::NTM`.
3. Add NTM label/color handling in `variant_net_render.cpp` switch on `config.net_type` (currently only handles `SquadLeader`, `Fighter`, default/`Solo`).
4. Update `snapshot_io.cpp` -- the serialization already handles arbitrary `uint8_t` values, so no format change needed, but add a display case for the new value.

---

## [HIGH] `ArenaConfig` has six NTM/SquadLeader topology fields that duplicate `NtmNetConfig`/`SquadLeaderNetConfig`

**ID:** `STALE-003`

**Location:** `include/neuroflyer/arena_config.h:50-57`

**Description:** `ArenaConfig` declares six topology fields:
```
ntm_input_size = 7, ntm_hidden_sizes = {4}, ntm_output_size = 1
squad_leader_input_size = 14, squad_leader_hidden_sizes = {8}, squad_leader_output_size = 5
```

These are never read by any code. The actual NTM and squad leader topologies are configured via `NtmNetConfig` and `SquadLeaderNetConfig` in `team_evolution.h`, which have identical default values but are separate structs. The `ArenaConfig` fields were added to the config struct during the arena design phase but the actual implementation uses the `team_evolution.h` structs directly.

This means:
- If someone edits these fields in the arena config UI (if one were added), it would have zero effect.
- The duplication creates confusion about which is the authoritative source of truth.

**Impact:** Misleading API. Developers waste time trying to configure NTM/squad leader topology through `ArenaConfig` when the actual config is in `team_evolution.h`.

**Fix plan:**
1. Either remove the six fields from `ArenaConfig` entirely, or
2. Wire them up so `ArenaGameScreen::initialize()` constructs `NtmNetConfig`/`SquadLeaderNetConfig` from the `ArenaConfig` fields. Choose one source of truth.

---

## [HIGH] `FighterDrillConfig` has no scoring penalty for dead ships

**ID:** `STALE-004`

**Location:** `src/engine/fighter_drill_session.cpp:246-292`, `include/neuroflyer/fighter_drill_session.h:17-49`

**Description:** In `FighterDrillSession::compute_phase_scores()`, dead ships are simply skipped (`if (!ships_[i].alive) continue;`). A ship that dies on tick 1 retains whatever score it accumulated before death and is never penalized. This creates a selection pressure problem: a ship that flies recklessly into towers, accumulates a high score early, then dies can outperform a cautious ship that survives the full drill.

Compare with the scroller game's scoring which uses `DEATH` (ship.alive check terminates the session for that ship). Compare with the arena, which includes `fitness_weight_survival` and `fitness_weight_ships_alive` terms.

The `FighterDrillConfig` has scoring weights (`expand_weight`, `contract_weight`, `attack_travel_weight`, `attack_hit_bonus`, `token_bonus`) but no `survival_weight` or `death_penalty`.

**Impact:** Evolution rewards reckless behavior. Ships that die early but accumulate burst score can dominate the population.

**Fix plan:**
1. Add `float death_penalty = -200.0f` to `FighterDrillConfig`.
2. In `FighterDrillSession::resolve_ship_tower_collisions()`, subtract the penalty when `ships_[i].alive = false` is set.
3. Alternatively, add a survival bonus that accumulates per-tick for alive ships, similar to `ArenaConfig::fitness_weight_survival`.

---

## [HIGH] `FighterDrillPauseScreen` saves variants to the genome root directory, not the squad directory

**ID:** `STALE-005`

**Location:** `src/ui/screens/game/fighter_drill_pause_screen.cpp:266`

**Description:** The fighter drill pause screen saves snapshots using:
```cpp
std::string path = genome_dir_ + "/" + snap.name + ".bin";
save_snapshot(snap, path);
```

This saves fighter variants as regular `.bin` files in the genome root directory. However, the arena pause screen saves squad-related variants using `save_squad_variant(genome_dir_, snap)` (line 228, 263), which places them in the `squad/` subdirectory.

Fighter drill variants are arena fighters (tagged `NetType::Fighter`), but they are saved alongside solo scroller variants in the genome root. This mixes variant types in the same directory, making it difficult to distinguish fighter variants from solo variants in the hangar file listing.

**Impact:** Organizational confusion in the genome directory. Fighter drill variants intermixed with solo variants. The hangar list shows them all together without clear type separation.

**Fix plan:**
1. Use `save_squad_variant(genome_dir_, snap)` instead of `save_snapshot(snap, path)` to place fighter drill variants in the `squad/` subdirectory, consistent with the arena save path.
2. Or create a separate `fighters/` subdirectory if distinguishing fighters from squad leaders is desired.

---

## [MEDIUM] `variant_net_render.cpp` switch on `net_type` has no explicit `case NetType::Solo`

**ID:** `STALE-006`

**Location:** `src/ui/renderers/variant_net_render.cpp:24-61`

**Description:** The switch statement on `config.net_type` handles `NetType::SquadLeader` (line 25) and `NetType::Fighter` (line 39), with `Solo` handled by `default:` (line 50). If a new `NetType` value is added (e.g., `NTM` as suggested in STALE-002), it will silently fall through to the `Solo` case rather than producing a compiler warning about an unhandled enum value. This is a stale pattern from before `Fighter` was added -- the original code only had `default:` for the one case.

Similarly at lines 68 and 95, `config.net_type == NetType::SquadLeader` is checked with an implicit else for both Solo and Fighter. If Fighter output labels differ from Solo in the future, this will silently use the wrong labels.

**Impact:** New `NetType` values will silently get Solo labels instead of generating a compile-time warning.

**Fix plan:**
1. Replace `default:` with explicit `case NetType::Solo:`.
2. This ensures the compiler warns about unhandled enum values when new types are added.

---

## [MEDIUM] `FighterDrillSession` does not resolve bullet-ship collisions

**ID:** `STALE-007`

**Location:** `src/engine/fighter_drill_session.cpp:100-106`

**Description:** The fighter drill tick loop at lines 100-106 calls:
```
resolve_ship_tower_collisions();
resolve_ship_token_collisions();
resolve_bullet_starbase_collisions();
resolve_bullet_tower_collisions();
```

But there is no `resolve_bullet_ship_collisions()`. Ships can shoot bullets that hit the starbase and towers, but bullets pass through other ships without effect. Compared with `ArenaSession::tick()` (line 132) which DOES call `resolve_bullet_ship_collisions()`, the drill session is missing this step.

In the current drill design, all ships are on the same team, so friendly fire might be intentionally disabled. However, there is no documented reason for the omission, and the ships can still collide with towers (which kills them). The absence of ship-bullet collision means ships in the Attack phase can fire through the squad toward the starbase without any friendly fire risk, which may create unrealistic training conditions.

**Impact:** Training artifact -- fighters trained in drill mode learn they can fire freely without friendly fire consequences, which may not transfer to arena mode where friendly fire is possible.

**Fix plan:**
1. If intentional, add a comment explaining why bullet-ship collisions are skipped.
2. If not intentional, add a `resolve_bullet_ship_collisions()` method to `FighterDrillSession` and call it in the tick loop.

---

## [LOW] `effective_ship_design()` does not preserve `is_full_sensor` or `type` from the original design

**ID:** `STALE-008`

**Location:** `src/engine/evolution.cpp:214-231`

**Description:** `Individual::effective_ship_design()` reconstructs a `ShipDesign` from the genome's sensor genes, but it hardcodes `sensor.type = SensorType::Occulus` and `sensor.is_full_sensor = false` for every sensor (lines 226-227), with a comment "caller should set these from template." This means the method returns a ShipDesign that does not faithfully represent the individual's actual sensor configuration.

The method is called from:
- `evolution.cpp:447` (inside `restore_layer_genes`) -- used to rebuild genome skeleton after topology mutations
- `variant_net_editor_screen.cpp:40` -- used to set ship_design_ for the net editor

In the `restore_layer_genes` case, the sensor types don't affect the weight structure (only sensor count matters), so the bug is latent. In the net editor case, the wrong `is_full_sensor` value causes incorrect input count calculations, though this is overridden later by loading from the snapshot's ship_design.

**Impact:** Latent bug. If `effective_ship_design()` is used in a context where `is_full_sensor` or `type` matters (e.g., computing input size for a new feature), it will return wrong values.

**Fix plan:**
1. Store `SensorType` and `is_full_sensor` as genome genes (alongside angle/range/width), OR
2. Accept a template `ShipDesign` parameter and copy those fields from it, OR
3. Add a prominent warning comment that callers must overlay type/is_full_sensor from the original design.

---

## [LOW] `ArenaConfig::squad_leader_fighter_inputs` is a `static constexpr` but is defined alongside runtime-configurable fields

**ID:** `STALE-009`

**Location:** `include/neuroflyer/arena_config.h:60`

**Description:** `squad_leader_fighter_inputs = 6` is a `static constexpr` member, meaning it cannot be changed at runtime or configured per-session. It is placed among runtime-configurable fields (like `num_squads`, `fighters_per_squad`) in `ArenaConfig`, which suggests it was intended to be configurable. However, its value is also hardcoded in several places:
- `arena_sensor.cpp:223` -- `return sensor_values + 6 + design.memory_slots;` (hardcoded `6`)
- `arena_sensor.cpp:258-263` -- squad leader inputs pushed as exactly 6 values
- `arena_sensor.cpp:334` -- `for (int i = 0; i < 6; ++i)`
- `variant_net_render.cpp` -- various references to the fixed 6-input layout

The `static constexpr` field exists but the code uses magic number `6` instead of referencing `ArenaConfig::squad_leader_fighter_inputs`.

**Impact:** If the number of squad leader fighter inputs changes, the `ArenaConfig` field can be updated but all the hardcoded `6`s throughout the codebase will remain stale.

**Fix plan:**
1. Replace all hardcoded `6`s in `arena_sensor.cpp` and `variant_net_render.cpp` with `ArenaConfig::squad_leader_fighter_inputs`.
2. Or move the constant to a more appropriate location (e.g., `arena_sensor.h`) if it should not be in ArenaConfig.

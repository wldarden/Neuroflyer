# Stale Code Review -- 2026-03-30

Scope: full NeuroFlyer repo. Focus on recently added arena mode (NTM, squad leaders, NetType, ArenaHitType::Bullet, sector grid).

---

## [CRITICAL] `query_arena_sensor()` only handles Raycast -- Occulus sensors silently broken in arena mode

**ID:** `STALE-001`
**Location:** `src/engine/arena_sensor.cpp:33-96`
**Description:** `query_arena_sensor()` treats every sensor as a raycast. It computes `abs_angle = ctx.ship_rotation + sensor.angle` and uses `ray_circle_hit()` for detection. There is no check for `sensor.type` and no Occulus (ellipse overlap) code path. By contrast, the scroller's `sensor_engine.cpp` has a `switch(sensor.type)` that dispatches to either `query_raycast()` or `query_occulus()`. Any ShipDesign with Occulus sensors will produce incorrect detection results in arena mode -- raycasts will be fired instead of ellipse overlap checks.
**Impact:** Variants with Occulus sensors will behave incorrectly in arena matches. Since the default legacy design only uses Raycast, this is latent for now, but any genome created with the sensor editor that includes Occulus sensors will break silently when used in arena training.
**Fix plan:**
1. Add a `switch(sensor.type)` to `query_arena_sensor()`.
2. Port the ellipse overlap logic from `sensor_engine.cpp::query_occulus()` into a new `query_arena_occulus()` that also checks ships, bullets, and friend/enemy classification.
3. Add test coverage in `tests/arena_sensor_test.cpp` for Occulus sensors.

---

## [CRITICAL] `variant_net_render.cpp` does not produce arena-specific labels for `NetType::Fighter`

**ID:** `STALE-002`
**Location:** `src/ui/renderers/variant_net_render.cpp:35-44`
**Description:** When `config.net_type == NetType::Fighter`, the code falls through to the `else` branch which calls `build_input_labels(config.ship_design)`. This produces **scroller labels** (including "POS X", "POS Y", "SPEED" and scroller-specific sensor sub-labels like "SNS ... $" for token_value). Arena fighters have a completely different input layout: 5 values per full sensor (distance, is_tower, is_token, is_friend, is_bullet), 6 squad leader inputs (target_heading, target_distance, center_heading, center_distance, aggression, spacing), and no position inputs. The net viewer in the arena game screen (`arena_game_screen.cpp:487`) sets `net_type = NetType::Fighter`, so the live net viewer during arena play shows **wrong labels** for every input node.
**Impact:** The arena follow-mode net viewer is mislabeled for all fighter inputs. Users cannot correctly interpret which inputs are driving which behavior. The `build_input_colors()` and `build_display_order()` also produce scroller-layout data.
**Fix plan:**
1. Add `build_arena_fighter_input_labels(const ShipDesign&)` to `sensor_engine.h` (or `arena_sensor.h`).
2. In `variant_net_render.cpp`, add a `NetType::Fighter` case that calls the arena-specific label builder.
3. Similarly add arena-specific colors and display order.

---

## [HIGH] Arena game screen has no pause/config screen -- no way to save arena-trained variants

**ID:** `STALE-003`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:217-219`
**Description:** The Space key in arena mode toggles a `paused_` boolean, which simply stops calling `tick_arena()`. Unlike the scroller's `FlySessionScreen` which pushes a `PauseConfigScreen` (with tabs for Training, Evolution, Analysis, Save Variants), the arena game screen has NO config overlay. There is no way to:
- Save elite arena team variants during or after training
- Adjust evolution parameters mid-run
- View structural analysis
- Run headless generations
**Impact:** Arena training runs are fire-and-forget. Users cannot extract trained squad/fighter nets. For squad training mode (which was specifically designed to train squad nets against a frozen fighter), the inability to save results makes the feature incomplete end-to-end.
**Fix plan:**
1. Create an `ArenaPauseScreen` (or extend `PauseConfigScreen` to accept arena context).
2. Wire Space key to push that screen.
3. Implement at minimum a "Save Variants" tab that can serialize `TeamIndividual` squad/fighter nets as snapshots.

---

## [HIGH] `convert_variant_to_fighter()` does not update `topology.input_size` in the `result` correctly for all sensor types

**ID:** `STALE-004`
**Location:** `src/engine/evolution.cpp:248-253`
**Description:** The function computes `arena_sensor_vals` using `s.is_full_sensor ? 5 : 1`. This is correct for both Raycast and Occulus sensors IF `is_full_sensor` accurately determines the value count. However, the scroller format emits 4 values for a full sensor (distance, is_tower, token_value, is_token) while the arena emits 5 (distance, is_tower, is_token, is_friend, is_bullet). The function handles this correctly in the weight mapping (copies 2 weights from scroller's 4, maps to arena's 5 with zeros for new columns).

However, there is a subtle problem: the `src_col` advancement assumes `sensor.is_full_sensor` always means scroller layout (4 values). If a sensor is `is_full_sensor == false` but has type `Occulus`, the scroller and arena both emit 1 value for it, which is correct. But the function never verifies that `old_input == expected scroller input size`, meaning a mismatched topology would silently produce corrupt weights with no error.
**Impact:** Currently works for the common case (Raycast sensors only), but fragile. If someone creates a genome with a mix of sight and Occulus sensors and trains in scroller mode, then converts to arena, the weight mapping could misalign.
**Fix plan:**
1. Add a debug assertion: `assert(src_col + 3 + design.memory_slots == old_input)` after the sensor loop to catch mismatches.
2. Consider handling Occulus sensors explicitly if they have different value counts in scroller vs arena.

---

## [HIGH] Duplicated arena tick logic between `ArenaGameScreen::tick_arena()` and `run_arena_match()`

**ID:** `STALE-005`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:229-367`, `src/engine/arena_match.cpp:12-240`
**Description:** The NTM + squad leader + fighter tick loop is copy-pasted in two places. Both build a `SectorGrid`, run `gather_near_threats`, `run_ntm_threat_selection`, compute squad leader inputs with identical math, call `run_squad_leader`, then `compute_squad_leader_fighter_inputs`, `build_arena_ship_input`, and decode output. If any of these steps are updated (e.g., new squad leader inputs, changed heading convention), only one copy may be updated.
**Impact:** The `arena_match.cpp` version is already drifting -- it lacks the `last_leader_input_` / `last_fighter_input_` capture logic (expected, since that's a rendering concern), but any functional change to the tick would need to be applied in both places.
**Fix plan:** Extract the common per-tick logic into a shared function (e.g., `tick_arena_frame()`) that both the screen and the headless runner call. The screen version passes callbacks for capturing visualization data.

---

## [MEDIUM] `build_input_labels()` / `build_input_colors()` / `build_display_order()` produce incorrect data for arena fighter nets

**ID:** `STALE-006`
**Location:** `src/engine/sensor_engine.cpp:284-408`
**Description:** These three functions produce labels, colors, and ordering for the **scroller** input layout:
- Per full sensor: 4 values (distance, is_tower, token_value, is_token)
- System inputs at end: POS X, POS Y, SPEED
- Memory inputs at very end

Arena fighters have a different layout:
- Per full sensor: 5 values (distance, is_tower, is_token, is_friend, is_bullet)
- Squad leader inputs (6): target_heading, target_distance, center_heading, center_distance, aggression, spacing
- No position/speed inputs
- Memory at end

These functions are the ones called by `variant_net_render.cpp` for `NetType::Solo` and (incorrectly) `NetType::Fighter`. There are no equivalent `build_arena_*` functions.
**Impact:** See STALE-002. The live net viewer in arena mode shows wrong labels.
**Fix plan:** Create arena-aware versions of these three functions, or parameterize the existing ones by `NetType`.

---

## [MEDIUM] `VariantViewerScreen::on_enter()` does NOT reset `active_tab_` or refresh squad variants

**ID:** `STALE-007`
**Location:** `src/ui/screens/hangar/variant_viewer_screen.cpp:51-61`
**Description:** `on_enter()` resets `sub_view_` and pending flags, but does NOT:
1. Reset `active_tab_` (so if you left the screen on the Squad Nets tab, you return to it even for a different genome).
2. Explicitly trigger squad variant refresh (though `state.variants_dirty = true` is NOT set in `on_enter()`; the variants are only refreshed in `draw_variant_list()` when `state.variants_dirty` is true).

The squad variant refresh IS handled when `state.active_genome` changes (line 960-963), and `state.variants_dirty` is set to true during genome-change detection (line 955). So for a **new genome**, the refresh works. But if you return to the **same genome** after training (which sets `state.variants_dirty = true`), both fighter and squad variants are refreshed in `draw_variant_list` (lines 416-427), so this also works.

The main issue is `active_tab_` persistence: returning to the variant viewer after an arena training session keeps the tab you left on. This is mildly confusing but arguably intentional.
**Impact:** Minor UX issue -- the tab state persists across screen pushes/pops.
**Fix plan:** Consider resetting `active_tab_ = NetTypeTab::Fighters` in `on_enter()` if a new genome is detected, or leave as-is if tab persistence is desired.

---

## [MEDIUM] `PauseConfigScreen::draw_save_variants_tab()` does not set `net_type` on saved snapshots

**ID:** `STALE-008`
**Location:** `src/ui/screens/game/pause_config_screen.cpp:327-341`
**Description:** When saving variants from the pause screen's Save Variants tab, the code builds `Snapshot` objects but never sets `snap.net_type`. It defaults to `NetType::Solo`, which is correct for scroller training. However, if this code is ever reused or adapted for arena training (where saved nets should be `NetType::Fighter`), the default will be wrong. This is noted as a preemptive stale risk since the arena pause screen (STALE-003) does not yet exist.
**Impact:** Currently correct since the pause screen only exists for scroller mode. Becomes a bug if an arena pause screen copies this pattern.
**Fix plan:** When implementing the arena save-variants UI, ensure `snap.net_type` is set based on the training mode.

---

## [MEDIUM] `ArenaConfig` NTM/squad topology fields are never read by `ArenaGameScreen`

**ID:** `STALE-009`
**Location:** `include/neuroflyer/arena_config.h:48-56`, `src/ui/screens/arena/arena_game_screen.cpp:59-60`
**Description:** `ArenaConfig` has `ntm_input_size`, `ntm_hidden_sizes`, `ntm_output_size`, `squad_leader_input_size`, `squad_leader_hidden_sizes`, and `squad_leader_output_size`. The `ArenaConfigScreen` view (`arena_config_view.cpp`) may allow editing these values. But `ArenaGameScreen::initialize()` creates `ntm_config_` and `leader_config_` as member variables with their own default values and **never reads** the corresponding fields from `config_`. Any edits the user makes on the config screen to NTM/squad leader topology are silently ignored.
**Impact:** User-visible bug: changing NTM or squad leader topology in the arena config screen has no effect. The arena always uses the hardcoded defaults in `NtmNetConfig` and `SquadLeaderNetConfig`.
**Fix plan:** In `ArenaGameScreen::initialize()`, populate `ntm_config_` and `leader_config_` from `config_`:
```
ntm_config_.input_size = config_.ntm_input_size;
ntm_config_.hidden_sizes = config_.ntm_hidden_sizes;
// etc.
```

---

## [MEDIUM] `ArenaGameScreen` does not handle the "all ships on one team dead, base undefended" case gracefully

**ID:** `STALE-010`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:300-315`, `src/engine/arena_session.cpp:451-469`
**Description:** `ArenaSession::check_end_conditions()` correctly ends the match when `teams_alive() <= 1` (line 468). However, during the tick loop in `ArenaGameScreen`, the NTM + squad leader computations (lines 252-316) access `arena_->bases()[t]` for all teams, and compute centroids from ships. When all ships on a team are dead:
1. `compute_squad_stats()` returns zero centroid (all ships dead, `alive_count = 0`).
2. The NTM still runs with a zero-centroid, which is meaningless.
3. Squad leader still runs and produces orders that are applied to no fighters (the fighter loop skips dead ships).

This is not a crash bug since dead ships are skipped in the fighter loop (line 320: `if (!arena_->ships()[i].alive) continue`), and the match ends next tick via `check_end_conditions()`. But in the base_attack_mode scenario (team 1 starts with all fighters dead), the entire NTM + squad leader pipeline runs for team 1 every tick with zero ships, doing wasted computation.
**Impact:** Performance waste in base_attack_mode. Not a correctness bug since the match eventually ends.
**Fix plan:** Add `if (stats.alive_fraction < 1e-6f) continue;` at the top of the per-team NTM loop to skip dead teams.

---

## [LOW] `NetType::Fighter` label display order includes scroller "system" block but arena has no system inputs

**ID:** `STALE-011`
**Location:** `src/engine/sensor_engine.cpp:373-376`
**Description:** `build_display_order()` hardcodes `SYSTEM_COUNT = 3` for POS X, POS Y, SPEED system inputs. Arena fighters do not have these inputs. When this order is applied to a fighter net in the arena net viewer, the display permutation will map indices past the end of the actual input vector, causing visual corruption or out-of-bounds reads in the net renderer.
**Impact:** Visual corruption in the arena net viewer when display order is applied. The net viewer might crash or show garbage for the last few input nodes.
**Fix plan:** See STALE-002/STALE-006. Create arena-specific display order that accounts for the 6 squad leader inputs instead of 3 system inputs.

---

## [LOW] Squad net lineage not tracked in `lineage.json` -- squad variants have flat ancestry

**ID:** `STALE-012`
**Location:** `src/engine/genome_manager.cpp` (save_squad_variant), `src/ui/screens/hangar/variant_viewer_screen.cpp:298`
**Description:** When creating squad net variants (line 298: `snap.parent_name = ""`), the parent is always empty. When saving squad variants after training, there is no `save_elite_variants_with_mrca()` equivalent for squad nets. The squad directory has its own `lineage.json` (created by `rebuild_lineage()` called on squad_dir in `save_squad_variant`), but since parent_name is always empty, the lineage tree for squad nets is always flat (all variants are roots).
**Impact:** No ancestry tracking for squad net evolution. Users cannot trace which squad net evolved from which. The lineage tree view only shows fighter variants (it reads `vs_.genome_dir`, not the squad subdirectory).
**Fix plan:**
1. Set `snap.parent_name` when saving squad nets after training (to the seed squad net or MRCA).
2. Consider extending the lineage tree view to optionally show squad net ancestry.

---

## [LOW] `ArenaHitType::Bullet` is correctly handled everywhere it appears

**ID:** `STALE-013`
**Location:** `src/engine/arena_sensor.cpp:86-92,152`, `include/neuroflyer/arena_sensor.h:19`
**Description:** After review, `ArenaHitType::Bullet` is handled in all relevant locations:
- `query_arena_sensor()` checks bullets and assigns `ArenaHitType::Bullet` (line 92).
- `build_arena_ship_input()` checks for `ArenaHitType::Bullet` in the is_bullet channel (line 152).
- `tests/arena_sensor_test.cpp` has a specific bullet detection test.
This item is NOT stale.
**Impact:** None. Correctly integrated.
**Fix plan:** No action needed. Listed here to confirm the audit was performed.

---

## [LOW] `convert_variant_to_fighter()` drops the `is_token` scroller weight instead of mapping it

**ID:** `STALE-014`
**Location:** `src/engine/evolution.cpp:282-287`
**Description:** The scroller full sensor layout is `[distance, is_tower, token_value, is_token]`. The arena full sensor layout is `[distance, is_tower, is_token, is_friend, is_bullet]`. The conversion copies:
- scroller[0] (distance) -> arena[0] (distance)
- scroller[1] (is_tower) -> arena[1] (is_tower)
- scroller[2] (token_value) and scroller[3] (is_token) are DROPPED (advance `src_col += 4`)
- arena[2] (is_token), arena[3] (is_friend), arena[4] (is_bullet) are left as zero

The scroller's `is_token` weight (which learned to detect tokens) could reasonably be mapped to the arena's `is_token` slot (arena[2]), preserving some of the trained detection behavior. Currently this knowledge is discarded.
**Impact:** Converted variants lose their learned token-detection behavior. Mild information loss during conversion.
**Fix plan:** Map `old_weights[src_col + 3]` (scroller is_token) to `new_weights[dst_col + 2]` (arena is_token) before zeroing the rest. This is a judgment call -- the semantic match is imperfect (scroller is_token was binary, arena is_token is also binary), but preserving it seems better than discarding.

# Bug Hunt Report -- 2026-03-30

Reviewer: Claude Opus 4.6 (automated overnight review)
Scope: Full NeuroFlyer repo, focused on arena mode (last 30 commits)

---

## [CRITICAL] Only team genomes 0 and 1 ever accumulate fitness

**ID:** `BUG-001`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:123, 602-603`
**Description:** `current_team_indices_` is hardcoded to `{0, 1}` in `start_new_match()`, and fitness is only accumulated into `team_population_[current_team_indices_[ti]]`. With a population of 20 team genomes, only indices 0 and 1 ever receive fitness scores. The other 18 team genomes always have `fitness == 0.0f` when evolution runs. Since evolution sorts by fitness and selects via tournament, the elites will always be 0 and 1, and every other genome is treated as equally bad (fitness 0). This collapses selection pressure to effectively random among the 18 untested genomes.
**Impact:** Arena evolution is broken. Only 2 out of 20 team genomes are ever evaluated. Natural selection cannot work properly -- the population cannot improve beyond what random mutation of the two tested genomes produces.
**Fix plan:**
1. Implement round-robin or random matchmaking: each generation should run `rounds_per_generation` matches with different team pairings drawn from the population, so every genome gets tested.
2. Track cumulative fitness per genome across all rounds in the generation.
3. Consider normalizing fitness by number of rounds played if matchmaking is uneven.

---

## [HIGH] Fighter net input labels are wrong in net viewer

**ID:** `BUG-002`
**Location:** `src/ui/renderers/variant_net_render.cpp:35-43`
**Description:** When `net_type == NetType::Fighter`, the renderer falls through to the `else` branch (shared with `NetType::Solo`) and calls `build_input_labels(config.ship_design)`. This function generates scroller-mode labels: sensors + "POS X", "POS Y", "SPEED" + memory. But fighter nets have a different input layout: sensors + 6 squad leader inputs (target heading, target distance, center heading, center distance, aggression, spacing) + memory. The fighter has 6 squad leader inputs vs 3 position inputs, so the label count will also be wrong (off by 3), causing labels to be misaligned with actual input nodes.
**Impact:** The net viewer displays incorrect labels for every fighter network node in the arena screen. The last 3+ input nodes will have wrong or missing labels. Developers inspecting fighter brains during training will see misleading information.
**Fix plan:**
1. Add a `build_fighter_input_labels(const ShipDesign&)` function in `sensor_engine.cpp` that produces: sensor labels + "TGT HDG", "TGT DST", "CTR HDG", "CTR DST", "AGG", "SPC" + memory labels.
2. Add a corresponding `build_fighter_input_colors()` and `build_fighter_display_order()`.
3. In `variant_net_render.cpp`, add a `NetType::Fighter` branch that uses the new functions.

---

## [HIGH] Triangle collision detection ignores rotation

**ID:** `BUG-003`
**Location:** `include/neuroflyer/collision.h:43-61`
**Description:** Both `bullet_triangle_collision()` and `triangle_circle_collision()` use fixed vertex offsets (top: `(x, y-s)`, bottom-left: `(x-s, y+s)`, bottom-right: `(x+s, y+s)`) that assume the ship faces upward (rotation = 0). Arena ships have arbitrary rotations, but the collision vertices are never rotated. A ship facing right still has its collision triangle pointing up.
**Impact:** Collision detection for ship-tower, ship-bullet, and bullet-ship interactions is inaccurate in the arena. Ships can fly through towers they appear to collide with, and get killed by bullets that appear to miss. The error grows with rotation angle -- a ship rotated 180 degrees has its collision triangle exactly inverted.
**Fix plan:**
1. Add a rotation-aware collision helper that rotates the 3 vertex offsets by `tri.rotation` before computing world-space vertex positions.
2. Replace the fixed-offset calculations in `bullet_triangle_collision()` and `triangle_circle_collision()` with the rotation-aware version.
3. Note: the scroller mode (vertical flight) keeps rotation near 0, so this bug only manifests in arena mode where ships rotate freely.

---

## [HIGH] Division by zero risk in compute_dir_range when world is zero-sized

**ID:** `BUG-004`
**Location:** `src/engine/arena_sensor.cpp:106`
**Description:** `world_diag = sqrt(world_w^2 + world_h^2)`. If both `world_w` and `world_h` are 0, `world_diag` is 0, and `distance / world_diag` on line 113 produces NaN/Inf. This propagates into NTM inputs, squad leader inputs, and all downstream neural net evaluations.
**Impact:** If an ArenaConfig is constructed with zero world dimensions, the entire neural net pipeline receives NaN inputs and produces garbage outputs. While unlikely in normal use, it can happen through misconfiguration or unit tests.
**Fix plan:** Add a guard: `if (world_diag < 1e-6f) world_diag = 1.0f;` before the division.

---

## [MEDIUM] Stale net_viewer_state_ pointers after evolution recompiles networks

**ID:** `BUG-005`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:484-494`
**Description:** `net_viewer_state_.network` is set to `&fighter_nets_[t]` or `&leader_nets_[t]` in `render_arena()`. These pointers point into vectors that are rebuilt every time `start_new_match()` is called (lines 130-137). Between `start_new_match()` clearing the vectors and the next `render_arena()` call, the pointers are stale. While `start_new_match()` does null-out the pointers (line 126-127) before rebuilding, there is a window during `do_arena_evolution()` -> `start_new_match()` where `team_population_` is replaced (via `evolve_team_population`/`evolve_squad_only`) and the `individual` pointer (line 484) becomes dangling because it pointed into the old population vector.
**Impact:** If `render_arena()` caches the pointer between frames and evolution runs mid-frame (during the fast-forward tick loop on lines 557-613), the `net_viewer_state_.individual` pointer points to freed memory. In practice, the net viewer's `draw_net_viewer_view()` checks `if (!state.network) return;` on the null pointer, but `state.individual` is not cleared -- it remains pointing at the old population.
**Fix plan:**
1. In `do_arena_evolution()`, clear `net_viewer_state_.individual = nullptr` alongside the network pointer clear.
2. Or move the net_viewer_state_ setup to always happen fresh each frame, after all tick processing is complete.

---

## [MEDIUM] arena_sensor ray_circle_hit: division by zero when ray length is zero

**ID:** `BUG-006`
**Location:** `src/engine/arena_sensor.cpp:24`
**Description:** In `ray_circle_hit()`, `a = dx*dx + dy*dy`. If `sensor.range` is 0 (degenerate sensor), then `dx = dy = 0`, so `a = 0`, and the division `(-b - sqrt_disc) / (2.0f * a)` produces Inf/NaN. Also, line 28: `t * sqrt(a)` and line 28: `dist / range` where `range` is 0.
**Impact:** Sensors with range 0 would crash or produce NaN, corrupting neural net inputs. Unlikely in normal use but possible if a sensor range gene mutates to its minimum boundary.
**Fix plan:** Add early return `if (a < 1e-12f) return 1.0f;` at the start of `ray_circle_hit()`, and/or clamp sensor range to a positive minimum in the evolvable gene bounds (min_val for sensor_range is already 30.0f, so this is low risk).

---

## [MEDIUM] Scroller full_sensor is_token weight lost in convert_variant_to_fighter

**ID:** `BUG-007`
**Location:** `src/engine/evolution.cpp:278-287`
**Description:** During weight conversion from scroller to arena format, the scroller's full_sensor layout is [distance, is_tower, token_value, is_token] (4 cols), while the arena's is [distance, is_tower, is_token, is_friend, is_bullet] (5 cols). The conversion copies only `distance` (col 0->0) and `is_tower` (col 1->1), then advances `src_col += 4`. The scroller's `is_token` weight (trained at src_col+3) could be mapped to arena's `is_token` (dst_col+2), preserving some learned token-detection behavior. Instead, it is discarded and the arena's is_token weight starts at 0.
**Impact:** Fighters converted from well-trained scroller variants lose their learned token-detection weights, reducing the benefit of transfer learning. The fighter must re-learn token detection from scratch.
**Fix plan:** Add a line to copy `old_weights[out * old_input + src_col + 3]` to `new_weights[out * arena_input + dst_col + 2]` (mapping scroller is_token to arena is_token).

---

## [LOW] base_attack_mode only kills team 1 fighters once at match start, not on subsequent rounds

**ID:** `BUG-008`
**Location:** `src/ui/screens/arena/arena_game_screen.cpp:147-153`
**Description:** The `base_attack_mode_` flag kills all team 1 ships in `start_new_match()`, which is called for every round. This is consistent. However, `base_attack_mode_` is not cleared between rounds, meaning every round in a generation will be base-attack mode. This may be intentional, but if the intent is to have mixed rounds (some base-attack, some normal), it would need a per-round flag.
**Impact:** Low -- behavior is consistent within a generation. This is more of a design consideration than a bug.
**Fix plan:** Document the intended behavior. If mixed-mode rounds are desired, add a per-round flag.

---

## [LOW] SectorGrid sector_of truncates negative coordinates to sector 0

**ID:** `BUG-009`
**Location:** `src/engine/sector_grid.cpp:16-20`
**Description:** `sector_of()` computes `col = int(x / sector_size_)` then clamps to [0, cols-1]. For negative x values (which can occur if a ship temporarily leaves the world before boundary wrapping), `int(-0.5f / 2000.0f)` gives 0, but `int(-2001.0f / 2000.0f)` gives -1, which is clamped to 0. This means all entities with negative coordinates end up in the corner sector (0,0), slightly inflating the neighbor count there.
**Impact:** Very minor -- boundary wrapping in `apply_boundary_rules()` should prevent negative coordinates under normal conditions. Ships are wrapped to [0, world_width] before the sector grid is rebuilt. Only relevant if an entity is inserted before wrapping occurs.
**Fix plan:** No action needed given the current tick order (wrapping happens before grid build). Consider an assert or comment documenting the assumption.

---

## [LOW] build_display_order uses 4 columns per full_sensor, should be 5 for arena

**ID:** `BUG-010`
**Location:** `src/engine/sensor_engine.cpp:368`
**Description:** `build_display_order()` counts `n = s.is_full_sensor ? 4 : 1` per sensor. This matches the scroller layout (4 values per full sensor: distance, is_tower, token_value, is_token) but not the arena layout (5 values per full sensor: distance, is_tower, is_token, is_friend, is_bullet). If `build_display_order()` is ever called for a fighter net's ShipDesign, the display permutation will be wrong.
**Impact:** Currently `build_display_order()` is only called in the `Solo/Fighter` renderer path of `variant_net_render.cpp`. For fighters, the display order will be incorrect -- it will undercount input nodes, causing the permutation to be shorter than the actual input count. Some input nodes will be un-permuted.
**Fix plan:** Either (a) create a separate `build_arena_display_order()` or (b) add a `bool arena_mode` parameter that uses 5 instead of 4 for full sensors.

---

## [LOW] Potential integer overflow in SectorGrid with extremely large worlds

**ID:** `BUG-011`
**Location:** `src/engine/sector_grid.cpp:10-12`
**Description:** `rows_ = ceil(world_height / sector_size)` and `cols_ = ceil(world_width / sector_size)`. The cells vector is sized to `rows_ * cols_`. With default ArenaConfig (world 81920x51200, sector_size 2000), this gives ~41x26 = 1066 cells -- very manageable. But with pathological configs, `rows_ * cols_` could overflow `int` if both exceed ~46340 (sqrt of INT_MAX). Since `rows_` and `cols_` are `int`, the multiplication could wrap.
**Impact:** Extremely unlikely with any realistic configuration. Only relevant with adversarial config values.
**Fix plan:** Consider using `size_t` for `rows_` and `cols_`, or add a check that `rows_ * cols_` does not exceed a reasonable limit.

# Consolidation Opportunities

> **Scope:** Recent arena mode and fighter drill additions, plus cross-cutting patterns in the full codebase. Focus on `fighter_drill_screen.cpp`, `fighter_drill_session.cpp`, `arena_session.cpp`, `arena_game_screen.cpp`, `arena_game_view.cpp`, `arena_sensor.cpp`, `sensor_engine.cpp`, `collision.h`.
> **Findings:** 3 high, 5 medium, 4 low

---

## [HIGH] SDL drawing primitives duplicated across two files

**ID:** `CONS-001`

**Location:**
- `src/ui/screens/game/fighter_drill_screen.cpp:28-90`
- `src/ui/views/arena_game_view.cpp:36-106`

**Description:** Three drawing functions are duplicated verbatim:

`draw_rotated_triangle` (fighter_drill_screen.cpp:28-49 vs arena_game_view.cpp:36-63):
Both compute cos/sin rotation, apply a rotate lambda to 3 triangle vertices, and draw 3 lines with `SDL_RenderDrawLineF`. Character-for-character identical.

`draw_filled_circle` (fighter_drill_screen.cpp:51-63 vs arena_game_view.cpp:66-77):
Both use the scanline approach: iterate dy from -radius to +radius, compute dx from sqrt, draw horizontal line. Identical.

`draw_circle_outline` (fighter_drill_screen.cpp:65-90 vs arena_game_view.cpp:80-106):
Both implement the midpoint circle algorithm with 8-point symmetry. Identical.

**Impact:** 130 lines of pure duplication. A visual improvement to one copy won't appear in the other.

**Fix plan:**
1. Create `include/neuroflyer/ui/sdl_draw.h` with declarations for these three functions.
2. Create `src/ui/renderers/sdl_draw.cpp` with the implementations.
3. Remove the anonymous namespace copies from both files and include the shared header.
4. Add `src/ui/renderers/sdl_draw.cpp` to `CMakeLists.txt`.

---

## [HIGH] Session tick mechanics duplicated between ArenaSession and FighterDrillSession

**ID:** `CONS-002`

**Location:**
- `src/engine/arena_session.cpp:176-325`
- `src/engine/fighter_drill_session.cpp:134-244`

**Description:** Five nearly-identical methods exist in both session classes:

| Method | ArenaSession lines | FighterDrillSession lines | Identical? |
|--------|-------------------|--------------------------|-----------|
| `apply_boundary_rules` | 176-196 | 134-152 | Yes |
| `spawn_bullets_from_ships` | 198-219 | 154-174 | Yes |
| `resolve_ship_tower_collisions` | 297-308 | 183-194 | Yes |
| `resolve_ship_token_collisions` | 310-325 | 196-214 | Near-identical (same proximity math) |
| `resolve_bullet_tower_collisions` | 264-276 | 232-244 | Yes |

Additionally, `spawn_obstacles` shares the same tower/token random placement logic with the same hardcoded radius range:
- `arena_session.cpp:89-110`: `radius_dist(15.0f, 35.0f)`, random x/y, push Tower/Token
- `fighter_drill_session.cpp:36-56`: `radius_dist(15.0f, 35.0f)`, random x/y, push Tower/Token

Total: ~200 lines of near-identical code.

**Impact:** Bug fixes must be applied to both files. The `update_bullets` method has already diverged: `ArenaSession` destroys bullets at world boundaries (line 227-229), while `FighterDrillSession` does not. It is unclear whether this divergence is intentional.

**Fix plan:**
1. Create free functions in a new `src/engine/session_mechanics.h/.cpp`:
   - `apply_boundary_wrapping(ships, world_w, world_h, wrap_ew, wrap_ns)`
   - `spawn_bullets(ships, bullets, bullet_max_range)`
   - `resolve_ship_tower_collisions(ships, towers)`
   - `resolve_ship_token_collisions(ships, tokens, scores, tokens_collected, token_bonus)`
   - `resolve_bullet_tower_collisions(bullets, towers)`
   - `spawn_random_obstacles(towers, tokens, tower_count, token_count, world_w, world_h, rng)`
2. Both sessions call these shared functions in their `tick()` methods.

---

## [HIGH] Occulus ellipse overlap math duplicated between sensor_engine and arena_sensor

**ID:** `CONS-003`

**Location:**
- `src/engine/sensor_engine.cpp:79-137` (`query_occulus`)
- `src/engine/arena_sensor.cpp:98-181` (`query_arena_occulus`)

**Description:** Both functions implement the same ellipse overlap algorithm:

1. Call `compute_sensor_shape()` to get center, major/minor radii, rotation
2. Compute cos/sin of rotation
3. Define a `check_overlap` lambda that:
   - Transforms point to ellipse-local coordinates
   - Computes `(lmaj^2/eff_maj^2 + lmin^2/eff_min^2)`
   - If <= 1.0, computes normalized distance from ship to object edge
   - Uses `SHIP_GAP + major_radius * 2.0` as max_reach
4. Iterate entities, calling `check_overlap` and tracking closest

The arena version extends this to also check ships, bullets, and classify hits by type (friendly/enemy/bullet), but the core overlap math is identical.

The `SHIP_GAP` constant (15.0f) appears in:
- `sensor_engine.cpp:24` (as `SHIP_GAP`)
- `sensor_engine.cpp:111` (as `SHIP_GAP_INNER`)
- `arena_sensor.cpp:120` (as `SHIP_GAP`)

**Impact:** 80+ duplicated lines. If the ellipse formula changes (e.g., for performance optimization), both files need identical updates.

**Fix plan:**
1. Extract the core overlap check into a standalone function:
   ```cpp
   // In sensor_engine.h or a new sensor_math.h
   float ellipse_overlap_distance(
       float obj_x, float obj_y, float obj_radius,
       float ship_x, float ship_y,
       const SensorShape& shape);
   // Returns [0,1] if inside, -1 if outside
   ```
2. Define `SHIP_GAP` once in `sensor_engine.h` as a named constant.
3. Both `query_occulus` and `query_arena_occulus` call the shared function.

---

## [MEDIUM] Input handling (speed controls, zoom, camera pan) duplicated between screens

**ID:** `CONS-004`

**Location:**
- `src/ui/screens/game/fighter_drill_screen.cpp:196-250`
- `src/ui/screens/arena/arena_game_screen.cpp:192-257`

**Description:** Both `handle_input` methods contain identical blocks for:

**Speed controls** (4 identical lines):
```cpp
if (keys[SDL_SCANCODE_1]) ticks_per_frame_ = 1;
if (keys[SDL_SCANCODE_2]) ticks_per_frame_ = 5;
if (keys[SDL_SCANCODE_3]) ticks_per_frame_ = 20;
if (keys[SDL_SCANCODE_4]) ticks_per_frame_ = 100;
```

**Camera zoom** (identical in both):
```cpp
float wheel = ImGui::GetIO().MouseWheel;
if (wheel != 0.0f) { camera_.adjust_zoom(wheel * 0.05f); }
if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) camera_.adjust_zoom(0.02f);
if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) camera_.adjust_zoom(-0.02f);
```

**Camera pan** (near-identical in both):
```cpp
float pan = Camera::PAN_SPEED / camera_.zoom;
if (keys[SDL_SCANCODE_LEFT])  { camera_.x -= pan; ... }
if (keys[SDL_SCANCODE_RIGHT]) { camera_.x += pan; ... }
if (keys[SDL_SCANCODE_UP])    { camera_.y -= pan; ... }
if (keys[SDL_SCANCODE_DOWN])  { camera_.y += pan; ... }
```

**Impact:** ~30 duplicated lines per screen. Adding a new speed level (e.g., key 5 = 500x) requires changes in both files.

**Fix plan:**
1. Add helper methods to the `Camera` struct:
   - `Camera::handle_pan_input(const Uint8* keys)` -- applies arrow key panning, returns true if moved
   - `Camera::handle_zoom_input(const Uint8* keys, float wheel)` -- applies zoom from scroll/keyboard
2. Add a free function `apply_speed_controls(const Uint8* keys, int& ticks_per_frame)`.
3. Both screens call these helpers instead of inlining the logic.

---

## [MEDIUM] Magic numbers: tower obstacle radius range 15-35

**ID:** `CONS-005`

**Location:**
- `src/engine/arena_session.cpp:92`: `radius_dist(15.0f, 35.0f)`
- `src/engine/fighter_drill_session.cpp:39`: `radius_dist(15.0f, 35.0f)`
- `src/engine/game.cpp:125`: `radius_dist(15.0f, 35.0f)`

**Description:** The tower obstacle radius range `[15.0, 35.0]` appears as magic numbers in three separate spawn functions across three session types. The values are identical but have no named constant.

**Impact:** Changing the tower size range requires finding and updating three locations. There is no documentation of what these values represent or why they were chosen.

**Fix plan:**
1. Define named constants in a shared location (e.g., `include/neuroflyer/game.h` near `Tower`):
   ```cpp
   inline constexpr float TOWER_MIN_RADIUS = 15.0f;
   inline constexpr float TOWER_MAX_RADIUS = 35.0f;
   ```
2. Alternatively, add `tower_min_radius` and `tower_max_radius` fields to the config structs.
3. Replace the three hardcoded `radius_dist(15.0f, 35.0f)` calls.

---

## [MEDIUM] Magic numbers: BULLET_RADIUS = 2.0f in arena_sensor.cpp

**ID:** `CONS-006`

**Location:**
- `src/engine/arena_sensor.cpp:83`: `constexpr float BULLET_RADIUS = 2.0f;`
- `src/engine/arena_sensor.cpp:172`: `constexpr float BULLET_RADIUS = 2.0f;`

**Description:** The bullet collision radius for sensor detection is defined twice in the same file (once in `query_arena_raycast`, once in `query_arena_occulus`), both as local `constexpr float BULLET_RADIUS = 2.0f`. This value is also semantically related to `Bullet::SPEED` and the visual bullet size in the renderers (`std::max(2.0f, 3.0f * camera.zoom)`).

**Impact:** Changing the bullet sensor radius requires finding both definitions. The value should ideally be defined once near the `Bullet` struct.

**Fix plan:**
1. Add `static constexpr float SENSOR_RADIUS = 2.0f;` to the `Bullet` struct in `game.h`.
2. Replace both local `BULLET_RADIUS` definitions with `Bullet::SENSOR_RADIUS`.
3. Consider also using this constant in the rendering code for visual consistency.

---

## [MEDIUM] Render color constants duplicated as inline RGBA tuples

**ID:** `CONS-007`

**Location:**
- Tower color `(120, 120, 130, 200)`: `arena_game_view.cpp:238` and `fighter_drill_screen.cpp:518`
- Token color `(255, 200, 50, 220)`: `arena_game_view.cpp:264` and `fighter_drill_screen.cpp:529`
- Bullet color `(255, 255, 80, 230)`: `arena_game_view.cpp:329` and `fighter_drill_screen.cpp:571`
- Background color `(10, 10, 20, 255)`: `arena_game_view.cpp:159` and `fighter_drill_screen.cpp:507`
- Divider color `(60, 60, 80, 255)`: `arena_game_screen.cpp:473` and `fighter_drill_screen.cpp:626`

**Description:** Five render colors appear as raw RGBA tuples in both the arena game view and the fighter drill screen. The project already has a `theme.h` for colors, but these rendering colors are not in it.

**Impact:** Visual inconsistency risk if one file's colors are updated without the other. The theme system exists but is not used for game object colors.

**Fix plan:**
1. Add game object render colors to `include/neuroflyer/ui/theme.h`:
   ```cpp
   inline constexpr Color tower_render = {120, 120, 130, 200};
   inline constexpr Color token_render = {255, 200, 50, 220};
   inline constexpr Color bullet_render = {255, 255, 80, 230};
   inline constexpr Color world_background = {10, 10, 20, 255};
   inline constexpr Color panel_divider = {60, 60, 80, 255};
   ```
2. Replace the inline RGBA tuples with theme constants.

---

## [MEDIUM] ImGui window flags boilerplate repeated 18+ times

**ID:** `CONS-008`

**Location:** Every screen file in `src/ui/screens/` (18+ occurrences found by grep)

**Description:** The pattern `ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse` (sometimes with `NoScrollbar`) appears in virtually every screen. Example locations:
- `fighter_drill_screen.cpp:661,705`
- `arena_game_screen.cpp:494,551`
- `hangar_screen.cpp:112,265,459`
- `fly_session_screen.cpp:419,435`
- (13 more occurrences)

**Impact:** Verbose boilerplate that obscures the actual window setup code. Adding a new flag (e.g., `NoBackground`) to all fixed-position windows requires editing 18+ locations.

**Fix plan:**
1. Define a named constant in `ui_widget.h`:
   ```cpp
   inline constexpr ImGuiWindowFlags kFixedPanelFlags =
       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
   ```
2. Replace all occurrences with `kFixedPanelFlags` (or `kFixedPanelFlags | ImGuiWindowFlags_NoScrollbar` where needed).

---

## [LOW] ArenaQueryContext built manually in 3 locations

**ID:** `CONS-009`

**Location:**
- `src/ui/screens/game/fighter_drill_screen.cpp:322-335`
- `src/ui/screens/arena/arena_game_screen.cpp:366-378`
- `src/engine/arena_match.cpp:140-152`

**Description:** Each site manually sets 12 fields on `ArenaQueryContext`. The assignment sequence is identical in structure -- only the source of ship data differs (direct `ships[i]` vs `arena_->ships()[i]` vs `arena.ships()[i]`). Each site is 13 lines of field-by-field assignment.

**Impact:** 39 lines of boilerplate across 3 files. Adding a new field to `ArenaQueryContext` requires updating all 3 sites.

**Fix plan:**
1. Add a factory function to `ArenaQueryContext`:
   ```cpp
   static ArenaQueryContext for_ship(
       std::size_t idx, int team,
       std::span<const Triangle> ships,
       std::span<const int> ship_teams,
       std::span<const Tower> towers,
       std::span<const Token> tokens,
       std::span<const Bullet> bullets,
       float world_w, float world_h);
   ```
2. Replace the 3 manual construction sites with a single factory call.

---

## [LOW] Phase progress bar computation duplicated in fighter drill HUD

**ID:** `CONS-010`

**Location:**
- `src/ui/screens/game/fighter_drill_screen.cpp:671-676` (follow mode HUD)
- `src/ui/screens/game/fighter_drill_screen.cpp:715-720` (swarm mode HUD)

**Description:** The phase progress bar computation is identical in both rendering paths of `render_hud()`:
```cpp
float phase_progress = 1.0f;
if (session_->phase() != DrillPhase::Done) {
    uint32_t remaining = session_->phase_ticks_remaining();
    phase_progress = 1.0f - static_cast<float>(remaining) /
        static_cast<float>(drill_config_.phase_duration_ticks);
}
ImGui::ProgressBar(phase_progress, ImVec2(-1, 14));
```

**Impact:** Minor duplication (6 lines repeated once). If the progress calculation changes, both branches need updating.

**Fix plan:**
1. Extract to a local helper at the top of `render_hud()`:
   ```cpp
   auto phase_progress = [&]() -> float { ... };
   ```
2. Call `ImGui::ProgressBar(phase_progress(), ...)` in both branches.

---

## [LOW] Alive-ship counting duplicated within fighter_drill_screen.cpp

**ID:** `CONS-011`

**Location:**
- `src/ui/screens/game/fighter_drill_screen.cpp:680-681` (follow HUD)
- `src/ui/screens/game/fighter_drill_screen.cpp:727-728` (swarm HUD)

**Description:** Both HUD paths count alive ships with the same one-liner:
```cpp
int alive = 0;
for (const auto& s : session_->ships()) if (s.alive) ++alive;
```

**Impact:** Trivial duplication (2 lines). The `FighterDrillSession` class could expose an `alive_count()` method (like `ArenaSession` does at line 432) to eliminate this.

**Fix plan:**
1. Add `[[nodiscard]] std::size_t alive_count() const noexcept;` to `FighterDrillSession`.
2. Replace both inline loops with `session_->alive_count()`.

---

## [LOW] ray_circle_hit in arena_sensor.cpp re-implements collision.h logic

**ID:** `CONS-012`

**Location:**
- `src/engine/arena_sensor.cpp:16-33` (`ray_circle_hit`)
- `include/neuroflyer/collision.h:12-31` (`ray_circle_intersect`)

**Description:** `ray_circle_hit()` is a local function in `arena_sensor.cpp` that performs ray-circle intersection, returning a normalized distance [0,1]. `ray_circle_intersect()` in `collision.h` performs the same math but returns the raw parameter `t`. The arena version adds normalization: `return std::min(dist / range, 1.0f)`.

The comment on line 18 explicitly acknowledges this: "Use f = O - C (same convention as collision.h ray_circle_intersect)".

**Impact:** The core math is duplicated. If a numeric stability fix is applied to `ray_circle_intersect`, it won't automatically apply to `ray_circle_hit`.

**Fix plan:**
1. Add a normalized variant to `collision.h`:
   ```cpp
   [[nodiscard]] inline float ray_circle_intersect_normalized(
       float ox, float oy, float dx, float dy, float range,
       float cx, float cy, float r) {
       float t = ray_circle_intersect(ox, oy, dx, dy, cx, cy, r);
       if (t < 0.0f) return 1.0f;
       float dist = t * std::sqrt(dx * dx + dy * dy);
       return std::min(dist / range, 1.0f);
   }
   ```
2. Replace `ray_circle_hit` in `arena_sensor.cpp` with a call to this new function.

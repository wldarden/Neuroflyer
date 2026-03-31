# Sensor/Collision Engine Cleanup

> Cleanup sprint Group A: 4 tightly-coupled fixes in the sensor/collision engine layer.
> Addresses ARCH-001, CONS-003/ARCH-005, CONS-012, ARCH-009/CONS-009.

## Fix 1: ARCH-001 — Move `build_input_colors()` out of engine

### Problem

`sensor_engine.cpp` imports `theme.h` (a UI header) to access color constants for `build_input_colors()`. This violates the engine/UI boundary. The function is purely presentation logic — every caller is in `src/ui/`.

Similarly, `arena_sensor.cpp` has `build_arena_fighter_input_colors()` with hardcoded color constants (not even using `theme.h`). Same issue: presentation logic in the engine.

### Design

**Move both color functions to `src/ui/renderers/variant_net_render.cpp`.**

This file is the natural home — it already:
- Imports `sensor_engine.h` and `arena_sensor.h`
- Calls both `build_input_colors()` and `build_arena_fighter_input_colors()`
- Converts `NodeStyle` to `neuralnet_ui::NodeColor`

Changes:
1. Remove `build_input_colors()` from `sensor_engine.h` (declaration) and `sensor_engine.cpp` (definition).
2. Remove `build_arena_fighter_input_colors()` from `arena_sensor.h` (declaration) and `arena_sensor.cpp` (definition).
3. Implement both as file-local functions in `variant_net_render.cpp`, importing colors from `theme.h`.
4. Remove `#include <neuroflyer/ui/theme.h>` from `sensor_engine.cpp`.
5. Update the other two callers (`topology_preview_view.cpp`, `hangar_screen.cpp`, `create_genome_screen.cpp`) — they currently call `build_input_colors()` via `sensor_engine.h`. They'll need to either:
   - Call a new UI-layer function declared in `variant_net_render.h`, OR
   - Inline the color logic locally (since they already do the NodeStyle→NodeColor conversion)

**Recommended:** Declare `build_input_colors()` and `build_arena_fighter_input_colors()` in `variant_net_render.h` so all UI callers can reach them. The functions move from engine headers to a UI header — same signature, new location.

### Files touched

| File | Action |
|------|--------|
| `include/neuroflyer/sensor_engine.h` | Remove `build_input_colors()` declaration |
| `include/neuroflyer/arena_sensor.h` | Remove `build_arena_fighter_input_colors()` declaration |
| `src/engine/sensor_engine.cpp` | Remove `build_input_colors()` impl, remove `#include <neuroflyer/ui/theme.h>` |
| `src/engine/arena_sensor.cpp` | Remove `build_arena_fighter_input_colors()` impl |
| `include/neuroflyer/renderers/variant_net_render.h` | Add declarations for both functions |
| `src/ui/renderers/variant_net_render.cpp` | Add both implementations, add `#include <neuroflyer/ui/theme.h>` |
| `src/ui/views/topology_preview_view.cpp` | Import from `variant_net_render.h` instead of `sensor_engine.h` for colors |
| `src/ui/screens/hangar/hangar_screen.cpp` | Same import change |
| `src/ui/screens/hangar/create_genome_screen.cpp` | Same import change |

## Fix 2: CONS-003 / ARCH-005 — Deduplicate occulus ellipse overlap math

### Problem

`query_occulus()` (sensor_engine.cpp) and `query_arena_occulus()` (arena_sensor.cpp) both contain an identical `check_overlap` lambda: ~20 lines of rotated ellipse overlap math + normalized distance computation.

### Design

**Extract a shared free function in `sensor_engine.h/.cpp`.**

`sensor_engine.h` already defines `SensorShape` and `SHIP_SENSOR_GAP`, both of which the ellipse math depends on. Both files already import `sensor_engine.h`.

```cpp
/// Test whether a circular object overlaps a sensor ellipse.
/// Returns normalized distance [0,1] if inside the inflated ellipse, or -1.0 if outside.
/// ship_x/ship_y needed for distance normalization relative to ship position.
[[nodiscard]] float ellipse_overlap_distance(
    const SensorShape& shape,
    float ship_x, float ship_y,
    float obj_x, float obj_y, float obj_radius);
```

Implementation in `sensor_engine.cpp` — replaces the existing lambda in `query_occulus()`. The `query_arena_occulus()` lambda is replaced by a call to the same function.

### Files touched

| File | Action |
|------|--------|
| `include/neuroflyer/sensor_engine.h` | Add `ellipse_overlap_distance()` declaration |
| `src/engine/sensor_engine.cpp` | Extract lambda into `ellipse_overlap_distance()`, call it from `query_occulus()` |
| `src/engine/arena_sensor.cpp` | Replace `check_overlap` lambda in `query_arena_occulus()` with call to `ellipse_overlap_distance()` |

## Fix 3: CONS-012 — Eliminate `ray_circle_hit` reimplementation

### Problem

`arena_sensor.cpp` defines a local `ray_circle_hit()` that re-implements the quadratic ray-circle intersection from `collision.h::ray_circle_intersect()`. The only difference: `ray_circle_hit` normalizes the result to `[0,1]` using ray length, while `ray_circle_intersect` returns the raw parameter `t`.

### Design

**Rewrite `ray_circle_hit` as a thin wrapper around `ray_circle_intersect()`.**

Add `#include <neuroflyer/collision.h>` to `arena_sensor.cpp`. Replace the 18-line quadratic solver with:

```cpp
float ray_circle_hit(float ox, float oy, float dx, float dy, float range,
                     float cx, float cy, float cr) {
    if (range < 1e-6f) return 1.0f;
    float t = ray_circle_intersect(ox, oy, dx, dy, cx, cy, cr);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    float dist = t * std::sqrt(dx * dx + dy * dy);
    return std::min(dist / range, 1.0f);
}
```

Keeps the local helper with arena-specific normalization semantics, eliminates the duplicated quadratic solver.

### Files touched

| File | Action |
|------|--------|
| `src/engine/arena_sensor.cpp` | Add `#include <neuroflyer/collision.h>`, rewrite `ray_circle_hit()` body |

## Fix 4: ARCH-009 / CONS-009 — ArenaQueryContext factory method

### Problem

`ArenaQueryContext` is manually constructed with 12 field assignments in 3 locations (arena_match.cpp, arena_game_screen.cpp, fighter_drill_screen.cpp). Identical boilerplate — any new field requires updating all 3.

### Design

**Add a static factory method on the struct.**

```cpp
struct ArenaQueryContext {
    // ... existing fields ...

    /// Build a context for querying sensors from a specific ship's perspective.
    [[nodiscard]] static ArenaQueryContext for_ship(
        const Triangle& ship, std::size_t index, int team,
        float world_w, float world_h,
        std::span<const Tower> towers,
        std::span<const Token> tokens,
        std::span<const Triangle> ships,
        std::span<const int> ship_teams,
        std::span<const Bullet> bullets);
};
```

Implementation in `arena_sensor.cpp` (next to the existing sensor query functions). Each call site becomes a one-liner.

### Files touched

| File | Action |
|------|--------|
| `include/neuroflyer/arena_sensor.h` | Add `for_ship()` static method declaration |
| `src/engine/arena_sensor.cpp` | Add `for_ship()` implementation |
| `src/engine/arena_match.cpp` | Replace 13-line construction with `ArenaQueryContext::for_ship(...)` |
| `src/ui/screens/arena/arena_game_screen.cpp` | Same replacement |
| `src/ui/screens/game/fighter_drill_screen.cpp` | Same replacement |

## Verification

- Build must succeed with no `theme.h` import in any `src/engine/` file.
- Existing tests must pass (sensor tests, arena sensor tests, snapshot tests).
- `grep -r "theme.h" src/engine/` must return zero results after Fix 1.

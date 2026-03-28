# Sensor Engine — Design Spec

**Date:** 2026-03-24
**Status:** Draft
**Scope:** `neuroflyer/` — single source of truth for sensor detection and input vector construction

## Problem

Sensor detection logic is duplicated across 3 locations:
1. **`fly_session.cpp`** — hardcoded raycast with magic indices `{0,1,3,5,7,9,11,12}` and `{2,4,6,8,10}`. Always raycasts, ignores `ShipDesign` sensor types entirely.
2. **`test_bench.cpp`** — separate raycast AND occulus implementations, reads occulus fields from the renderer (not from `ShipDesign`).
3. **`input_vector_test.cpp`** — a 4th copy of the raycast encoding in a test-local `InputBuilder` struct.

The `ShipDesign` stored with each genome specifies sensor types, angles, ranges, and modes — but none of the actual training code reads it. The fly loop always uses hardcoded 13-ray raycasting regardless of what the genome says.

**Result:** What users see in the test bench does not match what trains in fly mode. Changing sensor configuration has no effect on training. The occulus sensor system is cosmetic-only.

## Goals

1. **One function for sensor queries** — `query_sensor()` dispatches on `SensorType` (Raycast or Occulus). All detection math lives here.
2. **One function for input vectors** — `build_input()` reads a `ShipDesign` and calls `query_sensor()` for each sensor. All encoding logic lives here.
3. **Every consumer calls `build_input()`** — fly mode, headless, test bench, input tests. Zero inline encoding.
4. **`ShipDesign` is the source of truth** — the genome's stored design determines what sensors exist and how they work.

## Non-Goals

- Adding new sensor types (noise, refresh_rate — future work)
- Changing the raycast or occulus math itself — just moving it to a shared location
- Changing the binary save format

---

## Design

### `sensor_engine.h`

```cpp
namespace neuroflyer {

/// Result of querying one sensor against the world.
struct SensorReading {
    float distance = 1.0f;     // 0.0 = touching, 1.0 = nothing in range
    HitType hit = HitType::Nothing;
};

/// Query a single sensor. Dispatches on sensor type:
///   Raycast → ray-circle intersection along sensor angle
///   Occulus → ellipse overlap detection at sensor position
[[nodiscard]] SensorReading query_sensor(
    const SensorDef& sensor,
    float ship_x, float ship_y,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens);

/// Build the complete neural net input vector from a ShipDesign.
/// This is the ONE function all consumers call.
[[nodiscard]] std::vector<float> build_ship_input(
    const ShipDesign& design,
    float ship_x, float ship_y,
    float game_w, float game_h,
    float scroll_speed,
    float pts_per_token,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    std::span<const float> memory);

/// Decode neural net output into actions + memory.
struct DecodedOutput {
    bool up = false, down = false, left = false, right = false, shoot = false;
    std::vector<float> memory;
};
[[nodiscard]] DecodedOutput decode_output(
    std::span<const float> output,
    std::size_t memory_slots);

} // namespace neuroflyer
```

### `query_sensor` Implementation

**Raycast:** Cast a single ray from (ship_x, ship_y) at `sensor.angle` with `sensor.range`. Check intersection with all towers and tokens (circles). Return the closest hit's distance and type. This is the same math as `cast_rays` in `ray.cpp` but for a single ray at an arbitrary angle.

**Occulus:** Place an ellipse at `(ship_x + cos(angle) * range, ship_y + sin(angle) * range)` with major radius proportional to range and minor radius = `major * sensor.width`. Check overlap with all towers and tokens using the same ellipse math currently in `test_bench.cpp`. Return the closest overlapping object's distance and type.

### `build_ship_input` Implementation

```
for each sensor in design.sensors:
    reading = query_sensor(sensor, ship_x, ship_y, towers, tokens)
    if sensor.is_full_sensor:
        push: distance, is_dangerous, is_valuable, is_collectible  (4 values)
    else:
        push: distance  (1 value)

push: normalized_pos_x, normalized_pos_y, normalized_speed  (3 values)
push: memory[0..N]  (memory_slots values)
```

This matches the current encoding order: sight sensors first (1 value each), then full sensors (4 values each), then position, then memory. But now the sensor list comes from the `ShipDesign`, not hardcoded indices.

### `decode_output` Implementation

```
up    = output[0] > 0
down  = output[1] > 0
left  = output[2] > 0
right = output[3] > 0
shoot = output[4] > 0
memory = output[5..5+memory_slots]
```

### Call Sites to Replace

| Location | Current | New |
|----------|---------|-----|
| `fly_session.cpp` build_input() | Hardcoded raycast + magic indices | `build_ship_input(design, ...)` |
| `fly_session.cpp` action decoding | Inline `output[0] > 0` etc. | `decode_output(output, mem_slots)` |
| `test_bench.cpp` occulus/raycast modes | 2 separate inline implementations | `build_ship_input(design, ...)` |
| `input_vector_test.cpp` InputBuilder | Local copy of raycast encoding | Delete — test `build_ship_input` directly |

### Where Does `ShipDesign` Come From at Runtime?

The `ShipDesign` is stored in each `Snapshot`. When a population is created (TrainFresh/TrainFrom), the snapshot is loaded and its `ship_design` should be stored alongside the population for use during training.

Currently `FlySessionState` (in `fly_session.cpp`) doesn't store a `ShipDesign`. It needs one:

```cpp
struct FlySessionState {
    // ... existing fields ...
    ShipDesign ship_design;  // from the loaded genome/variant snapshot
};
```

Set when creating the population from a snapshot. The fly loop reads `s.ship_design` instead of hardcoding sensor layout.

### Backward Compatibility

Old genomes created before `ShipDesign` was saved properly will have an empty sensor list. `build_ship_input` should handle this: if `design.sensors.empty()`, fall back to the legacy 13-ray layout (create a default `ShipDesign` matching the old hardcoded indices). This ensures old genomes still work.

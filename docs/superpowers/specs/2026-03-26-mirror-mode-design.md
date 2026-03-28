# Mirror Mode for Test Bench Sensor Editing

**Date:** 2026-03-26
**Scope:** NeuroFlyer test bench — edit sensor detail window

## Summary

Add a "Mirror Mode" checkbox to the sensor detail window that links a selected sensor to its symmetric partner on the opposite side. When active, edits to range, width, and angle propagate to both sensors, keeping them symmetric.

## SensorDef ID

Add a `uint16_t id` field to `SensorDef` for stable identity across vector reordering.

- **Sentinel:** `0` means unset (never a valid assigned ID).
- **Assignment:** Random value in `[1, 65535]`. On generation, check uniqueness against other sensors in the same `ShipDesign`; resample on collision.
- **Backfill:** On snapshot load, any sensor with `id == 0` gets a fresh random ID assigned immediately after deserialization.
- **Snapshot format:** Bump from v2 to v3. v3 includes the `id` field per sensor. v2 files load with `id == 0` for all sensors, backfilled on load. Any save writes v3.

## TestBenchState Additions

Two new fields:

- `bool mirror_mode = false` — whether mirror editing is active for the current selection.
- `uint16_t mirror_partner_id = 0` — ID of the partner sensor (`0` = no partner).

Both reset to defaults when `selected_oval` becomes `-1`.

## Mirror Partner Detection

Runs each time `selected_oval` changes. Given selected sensor `s`:

1. Compute `target_angle = -s.angle`.
2. **Exact match:** Scan `design.sensors` for a sensor with same `type`, same `is_full_sensor`, and angle exactly equal to `target_angle`. If found: set `mirror_partner_id` to its ID, auto-enable and auto-check `mirror_mode`.
3. **Near match** (no exact found): Scan for same `type` + `is_full_sensor`, angle within 5 degrees of `target_angle`. If found: set `mirror_partner_id` to its ID, enable the checkbox but leave `mirror_mode = false`.
4. **No match:** `mirror_partner_id = 0`, checkbox visible but disabled. Tooltip: "No matching sensor at opposite angle".
5. **Center sensor** (angle == 0): `mirror_partner_id = 0`, checkbox visible but disabled. Tooltip: "Center sensors cannot mirror".

The selected sensor is never its own partner.

## Detail Window UI

A checkbox labeled "Mirror Mode" placed between the Label field and the Type display.

**Checkbox states:**

| Condition | Enabled | Default checked |
|-----------|---------|-----------------|
| Exact match found | Yes | Yes (auto-checked) |
| Near match found | Yes | No |
| No match / center | No (greyed) | No |

## Sync Behavior

When `mirror_mode` is checked, slider changes propagate to both sensors:

| Property | Sync rule |
|----------|-----------|
| Range | Same value copied to partner |
| Width | Same value copied to partner |
| Angle | Negated value written to partner (+50 -> -50) |
| Label | Not synced (independent) |

**Activation snap:** When the user checks mirror mode on a near-match, the partner immediately receives the selected sensor's range, width, and negated angle — snapping it into exact symmetry.

**Index resolution:** All writes resolve `mirror_partner_id` to a current vector index at the moment of application. This is safe across reordering.

**Deactivation:** If the partner sensor is somehow removed, `mirror_mode` auto-disables.

## Visual Feedback

When `mirror_mode` is active, the partner sensor renders with the same gold outline (`RGB(255, 200, 50)`) as the selected sensor. No additional label or indicator text.

## Reset Behavior

- On deselection: `mirror_mode = false`, `mirror_partner_id = 0`.
- On re-selection (even same sensor): partner search runs fresh.
- Bulk sliders ("All Range", "All Width") do not interact with mirror mode — they write to all sensors independently.

## Files Changed

- `neuroflyer/include/neuroflyer/ship_design.h` — Add `uint16_t id` to `SensorDef`, add ID generation helper.
- `neuroflyer/include/neuroflyer/components/test_bench.h` — Add `mirror_mode`, `mirror_partner_id` to `TestBenchState`.
- `neuroflyer/src/components/test_bench.cpp` — Mirror detection on selection, checkbox UI, synced slider writes, partner gold outline.
- `neuroflyer/src/snapshot_io.cpp` — Bump format to v3, serialize/deserialize sensor ID, backfill on v2 load.

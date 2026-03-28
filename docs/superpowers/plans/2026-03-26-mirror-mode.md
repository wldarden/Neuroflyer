# Mirror Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Mirror Mode" checkbox to the test bench sensor detail window that links symmetric sensor pairs and syncs their edits.

**Architecture:** Add `uint16_t id` to `SensorDef` for stable sensor identity, bump snapshot format to v3, then add mirror detection and synced editing to the test bench detail window. All mirror logic is UI-only in `TestBenchState` — no changes to network data flow.

**Tech Stack:** C++20, ImGui, GoogleTest, binary serialization

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-26-mirror-mode-design.md`

---

### Task 1: Add `id` field to SensorDef + generation helper

**Files:**
- Modify: `neuroflyer/include/neuroflyer/ship_design.h:11-18`
- Test: `neuroflyer/tests/ship_design_test.cpp`

- [ ] **Step 1: Write the failing test — sensor ID generation**

Add to `neuroflyer/tests/ship_design_test.cpp`:

```cpp
#include <algorithm>

TEST(ShipDesignTest, AssignSensorIds_FillsZeroIds) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 0},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false, 0},
        {nf::SensorType::Occulus, 1.0f, 120.0f, 0.3f, true, 0},
    };
    nf::assign_sensor_ids(design);
    for (const auto& s : design.sensors) {
        EXPECT_NE(s.id, 0u);
    }
    // All IDs should be unique
    std::vector<uint16_t> ids;
    for (const auto& s : design.sensors) ids.push_back(s.id);
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::unique(ids.begin(), ids.end()), ids.end());
}

TEST(ShipDesignTest, AssignSensorIds_PreservesExistingIds) {
    nf::ShipDesign design;
    design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 42},
        {nf::SensorType::Raycast, 0.5f, 300.0f, 0.0f, false, 0},
    };
    nf::assign_sensor_ids(design);
    EXPECT_EQ(design.sensors[0].id, 42u);
    EXPECT_NE(design.sensors[1].id, 0u);
    EXPECT_NE(design.sensors[1].id, 42u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests 2>&1 | tail -20`
Expected: Compile error — `id` field and `assign_sensor_ids` don't exist yet.

- [ ] **Step 3: Add `id` field and `assign_sensor_ids` function**

In `neuroflyer/include/neuroflyer/ship_design.h`, add `id` to `SensorDef`:

```cpp
struct SensorDef {
    SensorType type;
    float angle;
    float range;
    float width;
    bool is_full_sensor;
    uint16_t id = 0;  // 0 = unset sentinel
};
```

Add the `assign_sensor_ids` function after `compute_output_size`:

```cpp
#include <random>

/// Assign random unique IDs to any sensor with id == 0.
inline void assign_sensor_ids(ShipDesign& design) {
    // Collect existing IDs
    std::vector<uint16_t> existing;
    for (const auto& s : design.sensors) {
        if (s.id != 0) existing.push_back(s.id);
    }

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint16_t> dist(1, 65535);

    for (auto& s : design.sensors) {
        if (s.id != 0) continue;
        uint16_t candidate = 0;
        do {
            candidate = dist(rng);
        } while (candidate == 0 ||
                 std::find(existing.begin(), existing.end(), candidate) != existing.end());
        s.id = candidate;
        existing.push_back(candidate);
    }
}
```

- [ ] **Step 4: Fix existing SensorDef aggregate initializations**

The new `id` field changes aggregate init. Update all existing brace-init sites to include `id = 0` as the 6th field. Key locations:

In `neuroflyer/src/sensor_engine.cpp` lines 271 and 277 (inside `create_legacy_ship_design`):
```cpp
// line 271 — add trailing 0 for id:
design.sensors.push_back({SensorType::Raycast, angle, range_for_ray(si), 0.0f, false, 0});
// line 277:
design.sensors.push_back({SensorType::Raycast, angle, range_for_ray(si), 0.0f, true, 0});
```

In `neuroflyer/src/screens/create_genome.cpp` lines 208-209 and 278-279 (sensor push_back calls) — add trailing `, 0` to each.

In `neuroflyer/src/evolution.cpp` line 167 — this pushes an existing `SensorDef` variable, so no change needed (default init handles it).

In test files (`snapshot_io_test.cpp:16-20`, `ship_design_test.cpp:9-12,19-23`, `input_vector_test.cpp:234-238`, `snapshot_population_test.cpp:14`) — add trailing `, 0` to each brace-init.

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R ShipDesign --output-on-failure`
Expected: All pass, including the two new ID tests.

- [ ] **Step 6: Commit**

```bash
git add neuroflyer/include/neuroflyer/ship_design.h \
       neuroflyer/src/sensor_engine.cpp \
       neuroflyer/src/screens/create_genome.cpp \
       neuroflyer/src/evolution.cpp \
       neuroflyer/tests/ship_design_test.cpp \
       neuroflyer/tests/snapshot_io_test.cpp \
       neuroflyer/tests/input_vector_test.cpp \
       neuroflyer/tests/snapshot_population_test.cpp
git commit -m "feat(neuroflyer): add id field to SensorDef with random unique ID generation"
```

---

### Task 2: Bump snapshot format to v3 with sensor IDs

**Files:**
- Modify: `neuroflyer/src/snapshot_io.cpp:82-83,97-103,145-151`
- Test: `neuroflyer/tests/snapshot_io_test.cpp`

- [ ] **Step 1: Write the failing test — v3 round-trip with IDs**

Add to `neuroflyer/tests/snapshot_io_test.cpp`:

```cpp
TEST(SnapshotIOTest, RoundTrip_SensorIds) {
    nf::Snapshot snap;
    snap.name = "IdTest";
    snap.generation = 1;
    snap.ship_design.memory_slots = 2;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 100},
        {nf::SensorType::Occulus, 0.5f, 120.0f, 0.3f, false, 200},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {{std::size_t{5}, neuralnet::Activation::Tanh}};
    snap.weights.resize(50, 0.1f);

    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    ASSERT_EQ(loaded.ship_design.sensors.size(), 2u);
    EXPECT_EQ(loaded.ship_design.sensors[0].id, 100u);
    EXPECT_EQ(loaded.ship_design.sensors[1].id, 200u);
}

TEST(SnapshotIOTest, V2Backfill_SensorsGetIds) {
    // Save a snapshot with the current format
    nf::Snapshot snap;
    snap.name = "OldFormat";
    snap.generation = 1;
    snap.ship_design.memory_slots = 0;
    snap.ship_design.sensors = {
        {nf::SensorType::Raycast, 0.0f, 300.0f, 0.0f, true, 0},
        {nf::SensorType::Raycast, 0.5f, 200.0f, 0.0f, false, 0},
    };
    snap.topology.input_size = nf::compute_input_size(snap.ship_design);
    snap.topology.layers = {{std::size_t{3}, neuralnet::Activation::Tanh}};
    snap.weights.resize(20, 0.0f);

    // Manually serialize as v2 (no sensor IDs in payload)
    // We'll use the public API and verify backfill happens on load
    // Since save now writes v3, we test by loading with id==0 sensors
    // and verifying assign_sensor_ids was called
    std::stringstream ss;
    nf::save_snapshot(snap, ss);
    ss.seekg(0);
    auto loaded = nf::load_snapshot(ss);

    // After loading, sensors with id==0 should have been backfilled
    for (const auto& s : loaded.ship_design.sensors) {
        EXPECT_NE(s.id, 0u);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R SnapshotIO --output-on-failure`
Expected: `RoundTrip_SensorIds` fails — IDs not serialized yet.

- [ ] **Step 3: Update snapshot_io.cpp for v3**

In `neuroflyer/src/snapshot_io.cpp`:

Update version constant (line 82):
```cpp
constexpr uint16_t CURRENT_VERSION = 3;
```

In `write_payload`, after `write_val<uint8_t>(out, sensor.is_full_sensor ? 1 : 0);` (line 102), add:
```cpp
        write_val<uint16_t>(out, sensor.id);
```

In `parse_payload`, after `sensor.is_full_sensor = read_val<uint8_t>(in) != 0;` (line 150), add:
```cpp
        if (version >= 3) {
            sensor.id = read_val<uint16_t>(in);
        }
        // v1/v2: id stays 0 (default)
```

After the full `parse_payload` returns the snapshot (just before `return snap;` at line 182), add ID backfill:
```cpp
    // Backfill sensor IDs for older format versions
    assign_sensor_ids(snap.ship_design);

    return snap;
```

Add `#include <neuroflyer/ship_design.h>` at the top if not already present (it's included transitively via snapshot.h, but explicit is better).

- [ ] **Step 4: Run tests**

Run: `cmake --build build --target neuroflyer_tests && ctest --test-dir build -R SnapshotIO --output-on-failure`
Expected: All pass, including new ID round-trip tests. Existing tests still pass (v2 data loads fine, IDs get backfilled).

- [ ] **Step 5: Run full test suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add neuroflyer/src/snapshot_io.cpp neuroflyer/tests/snapshot_io_test.cpp
git commit -m "feat(neuroflyer): bump snapshot format to v3 with sensor IDs"
```

---

### Task 3: Add mirror state to TestBenchState

**Files:**
- Modify: `neuroflyer/include/neuroflyer/components/test_bench.h:52-57`

- [ ] **Step 1: Add mirror fields to TestBenchState**

In `neuroflyer/include/neuroflyer/components/test_bench.h`, after the `select_flash_timer` field (line 57), add:

```cpp
    // Mirror mode (links symmetric sensor pair for synced editing)
    bool mirror_mode = false;
    uint16_t mirror_partner_id = 0;  // 0 = no partner
```

- [ ] **Step 2: Build to verify no regressions**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Clean build. These fields are only read/written in test_bench.cpp (Task 4).

- [ ] **Step 3: Commit**

```bash
git add neuroflyer/include/neuroflyer/components/test_bench.h
git commit -m "feat(neuroflyer): add mirror_mode and mirror_partner_id to TestBenchState"
```

---

### Task 4: Mirror detection on sensor selection

**Files:**
- Modify: `neuroflyer/src/components/test_bench.cpp:119-135`

- [ ] **Step 1: Add a helper function for mirror partner detection**

At the top of `test_bench.cpp` (inside the anonymous namespace or before `draw_test_bench`), add:

```cpp
namespace {

/// Find the mirror partner for the sensor at index `si`.
/// Returns the partner's ID, or 0 if no suitable partner exists.
///
/// Tiered matching:
///   1. Exact: same type + is_full_sensor + angle == -selected.angle → auto-check
///   2. Near:  same type + is_full_sensor + angle within 5° of -selected.angle → enable only
///   3. None / center (angle == 0): disabled
///
/// Sets `auto_check` to true for exact matches, false otherwise.
uint16_t find_mirror_partner(const ShipDesign& design, int selected_idx, bool& auto_check) {
    auto_check = false;
    const auto& sel = design.sensors[static_cast<std::size_t>(selected_idx)];

    // Center sensor cannot mirror
    constexpr float CENTER_THRESHOLD = 0.001f;  // ~0.06 degrees
    if (std::abs(sel.angle) < CENTER_THRESHOLD) return 0;

    float target_angle = -sel.angle;
    constexpr float NEAR_THRESHOLD = 5.0f * std::numbers::pi_v<float> / 180.0f;  // 5 degrees

    uint16_t exact_id = 0;
    uint16_t near_id = 0;

    for (std::size_t i = 0; i < design.sensors.size(); ++i) {
        if (static_cast<int>(i) == selected_idx) continue;
        const auto& s = design.sensors[i];
        if (s.type != sel.type || s.is_full_sensor != sel.is_full_sensor) continue;
        if (s.id == 0) continue;  // unassigned ID, skip

        float angle_diff = std::abs(s.angle - target_angle);
        if (angle_diff < CENTER_THRESHOLD) {
            exact_id = s.id;
            break;  // exact match found, no need to continue
        } else if (angle_diff < NEAR_THRESHOLD && near_id == 0) {
            near_id = s.id;
        }
    }

    if (exact_id != 0) {
        auto_check = true;
        return exact_id;
    }
    return near_id;
}

/// Resolve a sensor ID to its current index in the design.sensors vector.
/// Returns -1 if not found.
int resolve_sensor_id(const ShipDesign& design, uint16_t id) {
    if (id == 0) return -1;
    for (std::size_t i = 0; i < design.sensors.size(); ++i) {
        if (design.sensors[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

} // namespace
```

- [ ] **Step 2: Hook mirror detection into selection logic**

In `test_bench.cpp`, find the click detection block (around lines 119-135). After the line `state.selected_oval = state.hovered_sensor;` (line 128), add mirror detection:

```cpp
                state.selected_oval = state.hovered_sensor;
                state.select_flash_timer = 0.5f;
                // Detect mirror partner
                bool auto_check = false;
                state.mirror_partner_id = find_mirror_partner(
                    state.design, state.selected_oval, auto_check);
                state.mirror_mode = auto_check;
```

In both deselection paths (lines 125 and 132), add mirror reset:

```cpp
                state.selected_oval = -1;
                state.select_flash_timer = 0.0f;
                state.mirror_mode = false;
                state.mirror_partner_id = 0;
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/src/components/test_bench.cpp
git commit -m "feat(neuroflyer): mirror partner detection on sensor selection"
```

---

### Task 5: Mirror Mode checkbox in the detail window

**Files:**
- Modify: `neuroflyer/src/components/test_bench.cpp:582-603`

- [ ] **Step 1: Add the checkbox UI between Label and Type**

In `test_bench.cpp`, after the Label `InputText` block (line 580) and before the `ImGui::Separator()` (line 582), insert the mirror checkbox:

```cpp
            // Mirror Mode checkbox
            {
                bool has_partner = state.mirror_partner_id != 0;
                if (!has_partner) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Checkbox("Mirror Mode", &state.mirror_mode)) {
                    // User just checked mirror mode on a near-match: snap partner
                    if (state.mirror_mode && has_partner) {
                        int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                        if (pi >= 0) {
                            auto& partner = state.design.sensors[static_cast<std::size_t>(pi)];
                            partner.range = sensor.range;
                            partner.width = sensor.width;
                            partner.angle = -sensor.angle;
                        }
                    }
                }
                if (!has_partner) {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        float sel_angle_deg = sensor.angle * 180.0f / std::numbers::pi_v<float>;
                        if (std::abs(sel_angle_deg) < 0.1f) {
                            ImGui::SetTooltip("Center sensors cannot mirror");
                        } else {
                            ImGui::SetTooltip("No matching sensor at opposite angle");
                        }
                    }
                }
            }
```

- [ ] **Step 2: Make sliders sync to mirror partner when active**

Replace the Range slider (line 590) with:

```cpp
            // Range
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Range", &sensor.range, 40.0f, 400.0f, "%.0f")) {
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].range = sensor.range;
                    }
                }
            }
```

Replace the Width slider (line 594) with:

```cpp
            // Width
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Width", &sensor.width, 0.02f, 0.6f, "%.3f")) {
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].width = sensor.width;
                    }
                }
            }
```

Replace the Angle slider block (lines 597-603) with:

```cpp
            // Angle (whole degrees only)
            float angle_deg = sensor.angle * 180.0f / std::numbers::pi_v<float>;
            angle_deg = std::round(angle_deg);  // snap to whole
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("Angle", &angle_deg, -90.0f, 90.0f, "%.0f deg")) {
                angle_deg = std::round(angle_deg);
                sensor.angle = angle_deg * std::numbers::pi_v<float> / 180.0f;
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].angle = -sensor.angle;
                    }
                }
            }
```

- [ ] **Step 3: Auto-disable mirror if partner disappears**

After the mirror slider sync code, just before `ImGui::Separator();` (the one before "Network Inputs"), add:

```cpp
            // Auto-disable mirror if partner was lost
            if (state.mirror_mode && resolve_sensor_id(state.design, state.mirror_partner_id) < 0) {
                state.mirror_mode = false;
                state.mirror_partner_id = 0;
            }
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
git add neuroflyer/src/components/test_bench.cpp
git commit -m "feat(neuroflyer): mirror mode checkbox with synced slider editing"
```

---

### Task 6: Gold outline for mirror partner sensor

**Files:**
- Modify: `neuroflyer/src/components/test_bench.cpp:720-782`

- [ ] **Step 1: Add partner to the highlight loop**

In the sensor highlight drawing loop (line 720-782), modify the condition that skips non-highlighted sensors. Currently:

```cpp
        if (!is_hovered && !is_selected) continue;
```

Change to:

```cpp
        bool is_mirror_partner = state.mirror_mode &&
            state.design.sensors[static_cast<std::size_t>(si)].id == state.mirror_partner_id;

        if (!is_hovered && !is_selected && !is_mirror_partner) continue;
```

Then add a new case in the color selection block (after the `is_hovered` case around line 741), before the closing of the color block:

```cpp
        } else if (is_mirror_partner) {
            // Mirror partner = gold outline (same as selected)
            hr = 255; hg = 200; hb = 50; ha = 140;
        }
```

The full color block should read:

```cpp
        if (is_flashing) {
            hr = 80; hg = 255; hb = 80; ha = 120;
        } else if (is_selected && is_hovered) {
            hr = 255; hg = 60; hb = 60; ha = 100;
        } else if (is_selected) {
            hr = 255; hg = 200; hb = 50; ha = 140;
        } else if (is_mirror_partner) {
            hr = 255; hg = 200; hb = 50; ha = 140;
        } else if (is_hovered) {
            hr = 255; hg = 200; hb = 50; ha = 80;
        }
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build 2>&1 | tail -5`
Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add neuroflyer/src/components/test_bench.cpp
git commit -m "feat(neuroflyer): gold outline for mirror partner sensor in test bench"
```

---

### Task 7: Assign IDs at all sensor creation sites

**Files:**
- Modify: `neuroflyer/src/sensor_engine.cpp:255-280`
- Modify: `neuroflyer/src/screens/create_genome.cpp`
- Modify: `neuroflyer/src/screens/fly_session.cpp:172`

- [ ] **Step 1: Call `assign_sensor_ids` in `create_legacy_ship_design`**

In `neuroflyer/src/sensor_engine.cpp`, at the end of `create_legacy_ship_design` (line 279), before `return design;`:

```cpp
    assign_sensor_ids(design);
    return design;
```

Add `#include <neuroflyer/ship_design.h>` if not already present at the top of the file.

- [ ] **Step 2: Call `assign_sensor_ids` in create_genome.cpp**

Find the point in `create_genome.cpp` where the final `ShipDesign design` is fully built (after all `sensors.push_back` calls, around line 287). Add before the design is used:

```cpp
    assign_sensor_ids(design);
```

- [ ] **Step 3: Build and run full test suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/src/sensor_engine.cpp neuroflyer/src/screens/create_genome.cpp
git commit -m "feat(neuroflyer): assign sensor IDs at all creation sites"
```

---

### Task 8: Manual smoke test

- [ ] **Step 1: Build and launch NeuroFlyer**

Run: `cmake --build build && ./build/neuroflyer/neuroflyer`

- [ ] **Step 2: Test mirror mode with existing variant**

1. Open a genome with symmetric sensors
2. Enter the test bench
3. Click a sensor on one side — verify the detail window shows the Mirror Mode checkbox
4. If exact match exists: checkbox should be auto-checked, partner should have gold outline
5. Adjust Range/Width/Angle sliders — verify partner updates in real-time
6. Uncheck mirror mode — verify partner stops syncing
7. Click a center sensor — verify checkbox is disabled with tooltip

- [ ] **Step 3: Test with legacy/old variants**

1. Load a variant saved before this change (v2 format)
2. Verify it loads without errors (IDs get backfilled)
3. Enter test bench — verify mirror mode works normally

- [ ] **Step 4: Commit any fixes found during smoke testing**

If any issues found, fix and commit with descriptive messages.

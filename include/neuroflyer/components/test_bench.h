#pragma once

#include <neuroflyer/config.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <string>
#include <vector>

namespace neuroflyer {

/// A draggable test object (asteroid or coin) in the test bench.
struct TestObject {
    float x, y;
    float radius;
    bool is_tower;  // true=asteroid, false=coin
};

/// An angular arc segment for resolution analysis display.
struct ResArc {
    float start_rad;
    float span_rad;
    std::vector<int> active_set;
    bool has_sensor;
};

/// All mutable state for the sensor test bench component.
struct TestBenchState {
    // Test objects placed in the scene
    std::vector<TestObject> test_objects;

    // Drag interaction
    int drag_idx = -1;

    // Manual input sliders
    float manual_x = 0.0f;
    float manual_y = 0.0f;
    float manual_speed = 0.2f;

    // Resolution analysis
    float res_test_radius = 100.0f;
    bool show_arcs = true;
    bool show_resolution_window = false;
    int res_zones = 0;
    float res_coverage = 0.0f;
    float res_avg_arc = 0.0f;

    // Per-sensor selection (-1 = none selected)
    int selected_oval = -1;

    // Selection visual feedback
    int hovered_sensor = -1;         // sensor under mouse this frame
    float select_flash_timer = 0.0f; // countdown for green flash on select (seconds)

    // Mirror mode (links symmetric sensor pair for synced editing)
    bool mirror_mode = false;
    uint16_t mirror_partner_id = 0;  // 0 = no partner

    // Per-sensor custom labels (index matches design.sensors)
    std::vector<std::string> sensor_labels;

    // Draggable sensor detail window position (persists while open)
    float sensor_window_x = 100.0f;
    float sensor_window_y = 100.0f;

    // Shared data between ImGui computation and SDL rendering
    std::vector<float> input_shared;
    std::size_t mem_slots_shared = 0;
    std::vector<ResArc> arcs;

    // Variant's ShipDesign (per-variant sensor config, source of truth)
    ShipDesign design;

    // Path to the variant's .bin file (for saving sensor changes back)
    std::string variant_path;

    // Config backup (restored on cancel)
    GameConfig config_backup;

    // Design backup (restored on cancel)
    ShipDesign design_backup;

    // Signals
    bool wants_save = false;
    bool wants_cancel = false;
};

/// Draw the sensor test bench (ImGui panel + SDL game area).
///
/// Combines the ImGui control panel and SDL rendering into a single call.
/// The caller is responsible for calling ImGui::Render() and presenting
/// the frame AFTER this returns.
///
/// @param state        Mutable test bench state
/// @param networks     Built networks (only [0] is used for forward pass)
/// @param config       Game config (may be mutated by occulus sliders)
/// @param renderer     The SDL/Occulus renderer
/// @param settings_path  Path for saving config on "Save & Back"
void draw_test_bench(TestBenchState& state,
                     std::vector<neuralnet::Network>& networks,
                     GameConfig& config,
                     Renderer& renderer);

} // namespace neuroflyer

#include <neuroflyer/components/test_bench.h>

#include <neuroflyer/game.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ui/theme.h>

#include <neuralnet-ui/tiny_text.h>

#include <SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

namespace neuroflyer {

namespace {

/// Find the mirror partner for the sensor at index `si`.
/// Returns the partner's ID, or 0 if no suitable partner exists.
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
            break;
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

void draw_test_bench(TestBenchState& state,
                     std::vector<neuralnet::Network>& networks,
                     GameConfig& config,
                     Renderer& renderer) {
    state.wants_save = false;
    state.wants_cancel = false;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw_f = display.x;
    const float sh_f = display.y;

    constexpr float TB_PANEL_W = 350.0f;
    float tb_game_w = sw_f - TB_PANEL_W;

    // Ship position (centered in the game area)
    float ship_x = tb_game_w * 0.5f;
    float ship_y = sh_f * 0.75f;

    // ---------------------------------------------------------------
    // Mouse interaction for dragging test objects
    // ---------------------------------------------------------------
    int mx, my;
    uint32_t mouse_btn = SDL_GetMouseState(&mx, &my);
    float mxf = static_cast<float>(mx);
    float myf = static_cast<float>(my);
    bool mouse_in_game = (mxf < tb_game_w);
    bool mouse_down = (mouse_btn & SDL_BUTTON_LMASK) != 0;

    // Hover detection: find which sensor the mouse is closest to (runs every frame)
    state.hovered_sensor = -1;
    if (mouse_in_game) {
        float best_dist_sq = std::numeric_limits<float>::max();
        for (int si = 0; si < static_cast<int>(state.design.sensors.size()); ++si) {
            const auto& sensor = state.design.sensors[static_cast<std::size_t>(si)];
            bool inside = false;
            float center_x = ship_x, center_y = ship_y;

            if (sensor.type == SensorType::Occulus) {
                auto shape = compute_sensor_shape(sensor, ship_x, ship_y);
                center_x = shape.center_x;
                center_y = shape.center_y;
                float cos_r = std::cos(shape.rotation);
                float sin_r = std::sin(shape.rotation);
                float dx = mxf - shape.center_x;
                float dy = myf - shape.center_y;
                float lmaj = dx * cos_r + dy * sin_r;
                float lmin = -dx * sin_r + dy * cos_r;
                if (shape.major_radius > 0.01f && shape.minor_radius > 0.01f) {
                    float val = (lmaj * lmaj) / (shape.major_radius * shape.major_radius)
                              + (lmin * lmin) / (shape.minor_radius * shape.minor_radius);
                    inside = (val <= 1.0f);
                }
            } else {
                float rdx = std::sin(sensor.angle);
                float rdy = -std::cos(sensor.angle);
                float to_mouse_x = mxf - ship_x;
                float to_mouse_y = myf - ship_y;
                float proj = to_mouse_x * rdx + to_mouse_y * rdy;
                if (proj >= 0 && proj <= sensor.range) {
                    float perp = std::abs(to_mouse_x * rdy - to_mouse_y * rdx);
                    inside = (perp < 8.0f);
                    // Ray center point = midpoint along the ray
                    center_x = ship_x + rdx * sensor.range * 0.5f;
                    center_y = ship_y + rdy * sensor.range * 0.5f;
                }
            }

            if (inside) {
                float dx = mxf - center_x;
                float dy = myf - center_y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    state.hovered_sensor = si;
                }
            }
        }
    }

    // Object dragging
    if (mouse_down && mouse_in_game) {
        if (state.drag_idx < 0) {
            for (int oi = 0; oi < static_cast<int>(state.test_objects.size()); ++oi) {
                float dx = mxf - state.test_objects[static_cast<std::size_t>(oi)].x;
                float dy = myf - state.test_objects[static_cast<std::size_t>(oi)].y;
                float r = state.test_objects[static_cast<std::size_t>(oi)].radius;
                if (dx * dx + dy * dy < r * r * 4.0f) {
                    state.drag_idx = oi;
                    break;
                }
            }
        }
        if (state.drag_idx >= 0) {
            state.test_objects[static_cast<std::size_t>(state.drag_idx)].x = mxf;
            state.test_objects[static_cast<std::size_t>(state.drag_idx)].y = myf;
        }
    } else {
        state.drag_idx = -1;
    }

    // Click detection: toggle select/deselect
    // Skip if ImGui wants the mouse (e.g. clicking the sensor detail window)
    if (mouse_in_game && state.drag_idx < 0 && ImGui::IsMouseClicked(0)
        && !ImGui::GetIO().WantCaptureMouse) {
        if (state.hovered_sensor >= 0) {
            if (state.selected_oval == state.hovered_sensor) {
                state.selected_oval = -1;
                state.select_flash_timer = 0.0f;
                state.mirror_mode = false;
                state.mirror_partner_id = 0;
            } else {
                state.selected_oval = state.hovered_sensor;
                state.select_flash_timer = 0.5f;
                // Detect mirror partner
                bool auto_check = false;
                state.mirror_partner_id = find_mirror_partner(
                    state.design, state.selected_oval, auto_check);
                state.mirror_mode = auto_check;
            }
        } else {
            state.selected_oval = -1;
            state.select_flash_timer = 0.0f;
            state.mirror_mode = false;
            state.mirror_partner_id = 0;
        }
    }

    // Tick selection flash timer
    if (state.select_flash_timer > 0.0f) {
        state.select_flash_timer -= ImGui::GetIO().DeltaTime;
        if (state.select_flash_timer < 0.0f) state.select_flash_timer = 0.0f;
    }

    // ---------------------------------------------------------------
    // Build towers/tokens from test objects for sensor computation
    // ---------------------------------------------------------------
    std::vector<Tower> tb_towers;
    std::vector<Token> tb_tokens;
    for (const auto& obj : state.test_objects) {
        if (obj.is_tower) {
            tb_towers.push_back({.x = obj.x, .y = obj.y,
                .radius = obj.radius, .alive = true});
        } else {
            tb_tokens.push_back({.x = obj.x, .y = obj.y,
                .radius = obj.radius, .alive = true});
        }
    }

    // ---------------------------------------------------------------
    // Compute inputs via sensor engine (single source of truth)
    // ---------------------------------------------------------------
    std::size_t TB_MEM_SLOTS = static_cast<std::size_t>(state.design.memory_slots);

    // Use the variant's ShipDesign directly — no conversion from renderer fields.
    ShipDesign tb_design = state.design;

    std::vector<float> zero_mem(TB_MEM_SLOTS, 0.0f);
    auto tb_input = build_ship_input(
        tb_design, ship_x, ship_y,
        tb_game_w, sh_f,
        config.scroll_speed, config.pts_per_token,
        tb_towers, tb_tokens, zero_mem);

    // Overwrite position/speed with manual slider values.
    // Position and speed are the 3 values just before memory at the end.
    if (tb_input.size() >= TB_MEM_SLOTS + 3) {
        std::size_t pos_idx = tb_input.size() - TB_MEM_SLOTS - 3;
        tb_input[pos_idx + 0] = state.manual_x;
        tb_input[pos_idx + 1] = state.manual_y;
        tb_input[pos_idx + 2] = state.manual_speed;
    }

    // Share input data for SDL rendering
    state.input_shared = tb_input;
    state.mem_slots_shared = TB_MEM_SLOTS;

    // Forward pass (only if net input size matches)
    std::vector<float> tb_output;
    if (!networks.empty() &&
        tb_input.size() == networks[0].topology().input_size) {
        tb_output = networks[0].forward(tb_input);
    }

    // ===============================================================
    // ImGui panel (right side)
    // ===============================================================
    ImGui::SetNextWindowPos(ImVec2(tb_game_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(TB_PANEL_W, sh_f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##TestBenchPanel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("SENSOR TESTING");
    ImGui::PopStyleColor();
    if (ImGui::Button("Save & Back", ImVec2(120, 25))) {
        state.wants_save = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)", ImVec2(120, 25))) {
        state.wants_cancel = true;
    }
    ImGui::Separator();

    // Place objects
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Place Objects");
    if (ImGui::Button("Add Asteroid", ImVec2(150, 28))) {
        state.test_objects.push_back({ship_x, ship_y - 100.0f, 25.0f, true});
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Coin", ImVec2(150, 28))) {
        state.test_objects.push_back({ship_x + 50.0f, ship_y - 100.0f, 10.0f, false});
    }
    if (ImGui::Button("Clear All", ImVec2(150, 28))) {
        state.test_objects.clear();
    }
    ImGui::Dummy(ImVec2(0, 5));

    // Manual inputs
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Manual Inputs");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Pos X", &state.manual_x, -1.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Pos Y", &state.manual_y, -1.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderFloat("Speed", &state.manual_speed, 0.0f, 1.0f, "%.2f");
    ImGui::Dummy(ImVec2(0, 5));

    // Sensor config — edit the variant's ShipDesign sensors directly
    if (!state.design.sensors.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Sensor Settings");

        // Bulk-edit sliders: apply to all sensors at once
        {
            // Compute current average range/width for display
            float avg_range = 0.0f;
            float avg_width = 0.0f;
            for (const auto& s : state.design.sensors) {
                avg_range += s.range;
                avg_width += s.width;
            }
            avg_range /= static_cast<float>(state.design.sensors.size());
            avg_width /= static_cast<float>(state.design.sensors.size());

            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::SliderFloat("All Range", &avg_range, 40.0f, 400.0f, "%.0f")) {
                for (auto& s : state.design.sensors) {
                    s.range = avg_range;
                }
            }
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::SliderFloat("All Width", &avg_width, 0.02f, 0.6f, "%.3f")) {
                for (auto& s : state.design.sensors) {
                    s.width = avg_width;
                }
            }
        }
        ImGui::Dummy(ImVec2(0, 3));

        if (state.selected_oval >= 0) {
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 1.0f),
                "Sensor #%d selected — see floating window", state.selected_oval);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Click a sensor to inspect it");
        }
        ImGui::Dummy(ImVec2(0, 5));
    }

    // ---------------------------------------------------------------
    // Angular resolution analysis
    // ---------------------------------------------------------------
    // Angular resolution analysis — uses state.design.sensors via compute_sensor_shape
    {
        bool has_occulus = false;
        for (const auto& s : state.design.sensors) {
            if (s.type == SensorType::Occulus) { has_occulus = true; break; }
        }
        if (has_occulus) {
            ImGui::Separator();
            if (ImGui::Button(state.show_resolution_window
                    ? "Hide Resolution Analysis" : "Show Resolution Analysis",
                    ImVec2(200.0f, 0.0f))) {
                state.show_resolution_window = !state.show_resolution_window;
            }

            // Computation runs when window is visible (for arc drawing on game area)
            if (state.show_resolution_window) {
            constexpr float PI = std::numbers::pi_v<float>;
            constexpr int SAMPLE_COUNT = 3600;
            constexpr float STEP = PI / static_cast<float>(SAMPLE_COUNT);

            // Precompute sensor shapes at origin (0,0) for the resolution sweep.
            // The sweep tests a circle of points around (0,0) and checks which sensor
            // ellipses each point falls inside.
            struct ResSensor {
                SensorShape shape;
                int idx;
                bool is_full;
            };
            std::vector<ResSensor> res_sensors;
            for (int si = 0; si < static_cast<int>(state.design.sensors.size()); ++si) {
                const auto& sd = state.design.sensors[static_cast<std::size_t>(si)];
                if (sd.type != SensorType::Occulus) continue;
                auto shape = compute_sensor_shape(sd, 0.0f, 0.0f);
                res_sensors.push_back({shape, si, sd.is_full_sensor});
            }

            state.arcs.clear();
            std::vector<int> prev_set;
            float arc_start_rad = -PI * 0.5f;

            for (int si = 0; si <= SAMPLE_COUNT; ++si) {
                float theta = -PI * 0.5f + STEP * static_cast<float>(si);
                float px = std::sin(theta) * state.res_test_radius;
                float py = -std::cos(theta) * state.res_test_radius;

                std::vector<int> active;
                for (const auto& rs : res_sensors) {
                    float dx = px - rs.shape.center_x;
                    float dy = py - rs.shape.center_y;
                    float cos_a = std::cos(rs.shape.rotation);
                    float sin_a = std::sin(rs.shape.rotation);
                    float lmaj = dx * cos_a + dy * sin_a;
                    float lmin = -dx * sin_a + dy * cos_a;
                    float val = (lmaj * lmaj) / (rs.shape.major_radius * rs.shape.major_radius) +
                               (lmin * lmin) / (rs.shape.minor_radius * rs.shape.minor_radius);
                    if (val <= 1.0f) active.push_back(rs.idx);
                }
                std::sort(active.begin(), active.end());

                if (active != prev_set) {
                    if (si > 0) {
                        float span = theta - arc_start_rad;
                        bool hs = false;
                        for (int idx : prev_set) {
                            if (idx >= 0 && static_cast<std::size_t>(idx) < state.design.sensors.size()) {
                                hs = hs || state.design.sensors[static_cast<std::size_t>(idx)].is_full_sensor;
                            }
                        }
                        state.arcs.push_back({arc_start_rad, span, prev_set, hs});
                    }
                    arc_start_rad = theta;
                    prev_set = active;
                }
            }
            // Final arc
            {
                float span = PI * 0.5f - arc_start_rad;
                bool hs = false;
                for (int idx : prev_set) {
                    if (idx >= 0 && static_cast<std::size_t>(idx) < state.design.sensors.size()) {
                        hs = hs || state.design.sensors[static_cast<std::size_t>(idx)].is_full_sensor;
                    }
                }
                state.arcs.push_back({arc_start_rad, span, prev_set, hs});
            }

            // Stats
            float total_covered = 0.0f;
            float min_arc = 999.0f;
            float max_arc = 0.0f;
            int covered_arcs = 0;
            for (const auto& a : state.arcs) {
                if (a.active_set.empty()) continue;
                float deg = a.span_rad * 180.0f / PI;
                total_covered += deg;
                min_arc = std::min(min_arc, deg);
                max_arc = std::max(max_arc, deg);
                ++covered_arcs;
            }
            float avg_arc = (covered_arcs > 0) ? total_covered / static_cast<float>(covered_arcs) : 0.0f;

            // Stats stored for the floating window to display
            state.res_zones = covered_arcs;
            state.res_coverage = total_covered;
            state.res_avg_arc = avg_arc;
            } // end if (show_resolution_window)
        }
    }

    // ---------------------------------------------------------------
    // Show input values
    // ---------------------------------------------------------------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Input Values");
    ImGui::BeginChild("##InputScroll", ImVec2(0, 200), true);

    // Build label + value pairs sorted by sensor angle (left-to-right)
    struct LabeledInput { std::string label; float value; };
    std::vector<LabeledInput> tb_labeled;

    if (!state.design.sensors.empty()) {
        int num_s = static_cast<int>(state.design.sensors.size());

        // Sort sensors by angle for display
        std::vector<int> sorted_idx(static_cast<std::size_t>(num_s));
        for (int i = 0; i < num_s; ++i) sorted_idx[static_cast<std::size_t>(i)] = i;
        std::sort(sorted_idx.begin(), sorted_idx.end(),
            [&](int a, int b) {
                return state.design.sensors[static_cast<std::size_t>(a)].angle
                     < state.design.sensors[static_cast<std::size_t>(b)].angle;
            });

        // Compute input offsets per sensor (array order)
        std::vector<int> offsets(static_cast<std::size_t>(num_s));
        {
            int off = 0;
            for (int si = 0; si < num_s; ++si) {
                offsets[static_cast<std::size_t>(si)] = off;
                off += state.design.sensors[static_cast<std::size_t>(si)].is_full_sensor ? 5 : 1;
            }
        }

        for (int si : sorted_idx) {
            const auto& sd = state.design.sensors[static_cast<std::size_t>(si)];
            int base = offsets[static_cast<std::size_t>(si)];
            char buf[32];
            if (sd.is_full_sensor) {
                auto get = [&](int o) -> float {
                    auto idx = static_cast<std::size_t>(base + o);
                    return idx < tb_input.size() ? tb_input[idx] : 0.0f;
                };
                std::snprintf(buf, sizeof(buf), "SNS #%d DIST", si);
                tb_labeled.push_back({buf, get(0)});
                std::snprintf(buf, sizeof(buf), "SNS #%d DNGR", si);
                tb_labeled.push_back({buf, get(1)});
                std::snprintf(buf, sizeof(buf), "SNS #%d VAL", si);
                tb_labeled.push_back({buf, get(2)});
                std::snprintf(buf, sizeof(buf), "SNS #%d COIN", si);
                tb_labeled.push_back({buf, get(3)});
            } else {
                std::snprintf(buf, sizeof(buf), "EYE #%d", si);
                float v = static_cast<std::size_t>(base) < tb_input.size()
                    ? tb_input[static_cast<std::size_t>(base)] : 0.0f;
                tb_labeled.push_back({buf, v});
            }
        }
    } else {
        // Legacy ray mode labels (already in left-to-right order)
        const char* legacy_labels[] = {
            "EYE L4", "EYE L3", "EYE L1", "EYE R1",
            "EYE R3", "EYE R5", "EYE R7", "EYE R8",
            "SNS L2 DIST", "SNS L2 DNGR", "SNS L2 VAL", "SNS L2 COIN",
            "SNS L0 DIST", "SNS L0 DNGR", "SNS L0 VAL", "SNS L0 COIN",
            "SNS C DIST",  "SNS C DNGR",  "SNS C VAL",  "SNS C COIN",
            "SNS R0 DIST", "SNS R0 DNGR", "SNS R0 VAL", "SNS R0 COIN",
            "SNS R2 DIST", "SNS R2 DNGR", "SNS R2 VAL", "SNS R2 COIN",
        };
        constexpr int n_legacy = sizeof(legacy_labels) / sizeof(legacy_labels[0]);
        for (int i = 0; i < n_legacy && static_cast<std::size_t>(i) < tb_input.size(); ++i) {
            tb_labeled.push_back({legacy_labels[i], tb_input[static_cast<std::size_t>(i)]});
        }
    }

    // Append POS X, POS Y, SPEED
    if (tb_input.size() > state.mem_slots_shared + 3) {
        std::size_t pos_start = tb_input.size() - state.mem_slots_shared - 3;
        const char* pos_names[] = {"POS X", "POS Y", "SPEED"};
        for (int pi = 0; pi < 3; ++pi) {
            tb_labeled.push_back({pos_names[pi], tb_input[pos_start + static_cast<std::size_t>(pi)]});
        }
    }

    // Display sorted labeled inputs
    for (const auto& li : tb_labeled) {
        ImVec4 color = (std::abs(li.value) > 0.01f)
            ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
            : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%-14s %6.3f",
            li.label.c_str(), static_cast<double>(li.value));
    }

    // Show memory slots
    std::size_t labeled_count = tb_labeled.size();
    for (std::size_t ii = tb_input.size() - state.mem_slots_shared; ii < tb_input.size(); ++ii) {
        float val = tb_input[ii];
        char buf[16];
        std::snprintf(buf, sizeof(buf), "MEM %zu", ii - (tb_input.size() - state.mem_slots_shared));
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
            "%-14s %6.3f", buf, static_cast<double>(val));
    }
    (void)labeled_count;
    ImGui::EndChild();

    // Show output values
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Output Values");
    const char* out_labels[] = {"UP", "DOWN", "LEFT", "RIGHT", "SHOOT"};
    for (std::size_t oi = 0; oi < tb_output.size() && oi < 5; ++oi) {
        float val = tb_output[oi];
        bool active = val > 0.0f;
        ImVec4 color = active
            ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
            : ImVec4(0.5f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%-8s %6.3f  %s",
            out_labels[oi],
            static_cast<double>(val),
            active ? "ACTIVE" : "");
    }

    ImGui::End();

    // ===============================================================
    // Angular Resolution floating window
    // ===============================================================
    if (state.show_resolution_window) {
        ImGui::SetNextWindowSize(ImVec2(280, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.85f);

        if (ImGui::Begin("Angular Resolution", &state.show_resolution_window)) {
            ImGui::SliderFloat("Test Radius", &state.res_test_radius,
                20.0f, 400.0f, "%.0f px");
            ImGui::Checkbox("Show Arc Markers", &state.show_arcs);
            ImGui::Separator();
            ImGui::Text("Zones: %d", state.res_zones);
            ImGui::Text("Coverage: %.1f / 180 deg",
                static_cast<double>(state.res_coverage));
            ImGui::Text("Avg arc: %.2f deg",
                static_cast<double>(state.res_avg_arc));
        }
        ImGui::End();
    }

    // ===============================================================
    // Draggable sensor detail window (floats over game area)
    // ===============================================================
    // Ensure sensor_labels vector is sized to match sensors
    if (state.sensor_labels.size() != state.design.sensors.size()) {
        state.sensor_labels.resize(state.design.sensors.size());
        for (std::size_t i = 0; i < state.design.sensors.size(); ++i) {
            if (state.sensor_labels[i].empty()) {
                const auto& sd = state.design.sensors[i];
                char def[16];
                std::snprintf(def, sizeof(def), "%s%zu",
                    sd.is_full_sensor ? "S" : "E", i);
                state.sensor_labels[i] = def;
            }
        }
    }

    if (state.selected_oval >= 0 &&
        static_cast<std::size_t>(state.selected_oval) < state.design.sensors.size()) {

        int si = state.selected_oval;
        auto& sensor = state.design.sensors[static_cast<std::size_t>(si)];
        auto& label = state.sensor_labels[static_cast<std::size_t>(si)];

        ImGui::SetNextWindowPos(ImVec2(state.sensor_window_x, state.sensor_window_y),
                                 ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_Always);

        char win_title[64];
        std::snprintf(win_title, sizeof(win_title), "Sensor #%d###SensorDetail", si);

        bool open = true;
        if (ImGui::Begin(win_title, &open, ImGuiWindowFlags_NoCollapse)) {
            // Save window position for persistence
            ImVec2 wpos = ImGui::GetWindowPos();
            state.sensor_window_x = wpos.x;
            state.sensor_window_y = wpos.y;

            // Label
            char name_buf[64];
            std::snprintf(name_buf, sizeof(name_buf), "%s", label.c_str());
            ImGui::Text("Label");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Label", name_buf, sizeof(name_buf))) {
                label = name_buf;
            }

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

            ImGui::Separator();

            // Type (read-only)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Type: %s",
                sensor.is_full_sensor ? "Detail Sensor" : "Sight (distance only)");

            // Range
            ImGui::Text("Range");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##Range", &sensor.range, 40.0f, 400.0f, "%.0f")) {
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].range = sensor.range;
                    }
                }
            }

            // Width
            ImGui::Text("Width");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##Width", &sensor.width, 0.02f, 0.6f, "%.3f")) {
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].width = sensor.width;
                    }
                }
            }

            // Angle (whole degrees only)
            float angle_deg = sensor.angle * 180.0f / std::numbers::pi_v<float>;
            angle_deg = std::round(angle_deg);  // snap to whole
            ImGui::Text("Angle");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##Angle", &angle_deg, -90.0f, 90.0f, "%.0f deg")) {
                angle_deg = std::round(angle_deg);
                sensor.angle = angle_deg * std::numbers::pi_v<float> / 180.0f;
                if (state.mirror_mode) {
                    int pi = resolve_sensor_id(state.design, state.mirror_partner_id);
                    if (pi >= 0) {
                        state.design.sensors[static_cast<std::size_t>(pi)].angle = -sensor.angle;
                    }
                }
            }

            // Auto-disable mirror if partner was lost
            if (state.mirror_mode && resolve_sensor_id(state.design, state.mirror_partner_id) < 0) {
                state.mirror_mode = false;
                state.mirror_partner_id = 0;
            }

            ImGui::Separator();

            // Live input values for this sensor
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Network Inputs");

            // Compute this sensor's input offset
            int base_input = 0;
            for (int j = 0; j < si; ++j) {
                base_input += state.design.sensors[static_cast<std::size_t>(j)].is_full_sensor ? 5 : 1;
            }

            const auto& inp = state.input_shared;
            constexpr int LIVE_BAR_W = 120;
            constexpr int LIVE_BAR_H = 4;

            auto draw_live_input = [&](const char* name, int offset) {
                if (static_cast<std::size_t>(base_input + offset) >= inp.size()) return;
                float val = inp[static_cast<std::size_t>(base_input + offset)];
                float clamped = std::clamp(std::abs(val), 0.0f, 1.0f);

                ImVec4 color = (std::abs(val) > 0.01f)
                    ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                    : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
                ImGui::TextColored(color, "%-8s %6.3f", name, static_cast<double>(val));

                // Progress bar
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                int bar_w = static_cast<int>(clamped * static_cast<float>(LIVE_BAR_W));
                if (bar_w > 0) {
                    ImU32 bar_col = (std::abs(val) > 0.01f)
                        ? IM_COL32(80, 255, 130, 180)
                        : IM_COL32(100, 100, 100, 100);
                    dl->AddRectFilled(cursor,
                        ImVec2(cursor.x + static_cast<float>(bar_w),
                               cursor.y + static_cast<float>(LIVE_BAR_H)),
                        bar_col);
                }
                // Background track
                dl->AddRect(cursor,
                    ImVec2(cursor.x + static_cast<float>(LIVE_BAR_W),
                           cursor.y + static_cast<float>(LIVE_BAR_H)),
                    IM_COL32(60, 60, 60, 120));
                ImGui::Dummy(ImVec2(static_cast<float>(LIVE_BAR_W),
                                     static_cast<float>(LIVE_BAR_H + 2)));
            };

            draw_live_input("DIST", 0);
            if (sensor.is_full_sensor) {
                draw_live_input("DANGER", 1);
                draw_live_input("VALUE", 2);
                draw_live_input("COIN", 3);
            }
        }
        ImGui::End();

        if (!open) {
            state.selected_oval = -1;
            state.select_flash_timer = 0.0f;
        }
    }

    // ===============================================================
    // SDL rendering (game area, objects, rays, arcs, input bars)
    // ===============================================================
    SDL_Renderer* sdl_renderer = renderer.renderer_;
    int tb_screen_h = renderer.screen_h();

    // Clip to game area (left of panel)
    SDL_Rect tb_clip = {0, 0, static_cast<int>(tb_game_w), tb_screen_h};
    SDL_RenderSetClipRect(sdl_renderer, &tb_clip);

    // Update game view bounds for current window size
    renderer.game_view.set_bounds(0, 0, static_cast<int>(tb_game_w), tb_screen_h);

    // Background image — below the input label area, left of the panel
    constexpr int BG_TOP_MARGIN = 80;  // keep input labels readable
    renderer.render_test_bench_background(
        0, BG_TOP_MARGIN,
        static_cast<int>(tb_game_w), tb_screen_h - BG_TOP_MARGIN);

    // Draw Occulus fields if the design has occulus sensors
    {
        bool has_occulus = false;
        for (const auto& s : tb_design.sensors) {
            if (s.type == SensorType::Occulus) { has_occulus = true; break; }
        }
        if (has_occulus) {
            std::vector<Tower> oc_t;
            std::vector<Token> oc_k;
            for (const auto& obj : state.test_objects) {
                if (obj.is_tower)
                    oc_t.push_back({.x = obj.x, .y = obj.y, .radius = obj.radius, .alive = true});
                else
                    oc_k.push_back({.x = obj.x, .y = obj.y, .radius = obj.radius, .alive = true});
            }
            renderer.game_view.render_occulus(tb_design, ship_x, ship_y, oc_t, oc_k);
        }
    }

    // Draw raycast lines using the same sensor engine as the neural net
    if (tb_design.sensors.empty()) {
        auto tb_sensor_eps = query_sensors_with_endpoints(
            tb_design, ship_x, ship_y, tb_towers, tb_tokens);

        for (const auto& ep : tb_sensor_eps) {
            uint8_t cr = 0, cg = 0, cb = 0;
            if (ep.hit == HitType::Tower) {
                cr = 255; cg = static_cast<uint8_t>(ep.distance * 80);
            } else if (ep.hit == HitType::Token) {
                cr = theme::token_color.r; cg = theme::token_color.g; cb = theme::token_color.b;
            } else if (ep.hit == HitType::AllyShip) {
                cr = 80; cg = 180; cb = 255;   // blue for ally
            } else if (ep.hit == HitType::FoeShip) {
                cr = 255; cg = 100; cb = 60;   // orange for foe
            } else if (ep.is_full_sensor) {
                cr = theme::sensor_idle.r; cg = theme::sensor_idle.g; cb = theme::sensor_idle.b;
            } else {
                cg = static_cast<uint8_t>(100 + ep.distance * 100);
            }
            uint8_t alpha = ep.is_full_sensor ? theme::ray_alpha_sensor : theme::ray_alpha_sight;
            SDL_SetRenderDrawColor(sdl_renderer, cr, cg, cb, alpha);
            SDL_RenderDrawLine(sdl_renderer,
                static_cast<int>(ship_x), static_cast<int>(ship_y),
                static_cast<int>(ep.x), static_cast<int>(ep.y));
        }
    }

    // Draw sensor highlights (hover, selection, flash)
    for (int si = 0; si < static_cast<int>(tb_design.sensors.size()); ++si) {
        bool is_hovered = (state.hovered_sensor == si);
        bool is_selected = (state.selected_oval == si);
        bool is_flashing = is_selected && state.select_flash_timer > 0.0f;
        bool is_mirror_partner = state.mirror_mode &&
            state.design.sensors[static_cast<std::size_t>(si)].id == state.mirror_partner_id;

        if (!is_hovered && !is_selected && !is_mirror_partner) continue;

        // Determine highlight color from theme
        theme::Color hc = {};
        if (is_flashing) {
            hc = theme::select_flash;
        } else if (is_selected && is_hovered) {
            hc = theme::select_deselect;
        } else if (is_selected || is_mirror_partner) {
            hc = theme::select_outline;
        } else if (is_hovered) {
            hc = theme::hover_highlight;
        }
        uint8_t hr = hc.r, hg = hc.g, hb = hc.b, ha = hc.a;

        const auto& sensor = tb_design.sensors[static_cast<std::size_t>(si)];
        if (sensor.type == SensorType::Occulus) {
            auto shape = compute_sensor_shape(sensor, ship_x, ship_y);
            // Draw thick ellipse outline (multiple passes at offset radii)
            SDL_SetRenderDrawColor(sdl_renderer, hr, hg, hb, ha);
            float cos_r = std::cos(shape.rotation);
            float sin_r = std::sin(shape.rotation);
            constexpr int SEGMENTS = 48;
            constexpr int THICKNESS = 3;  // number of concentric outlines
            for (int t_off = -THICKNESS / 2; t_off <= THICKNESS / 2; ++t_off) {
                float r_off = static_cast<float>(t_off);
                float prev_px = 0, prev_py = 0;
                for (int seg = 0; seg <= SEGMENTS; ++seg) {
                    float t = 2.0f * std::numbers::pi_v<float> * static_cast<float>(seg) / SEGMENTS;
                    float ex = (shape.major_radius + r_off) * std::cos(t);
                    float ey = (shape.minor_radius + r_off) * std::sin(t);
                    float px = shape.center_x + ex * cos_r - ey * sin_r;
                    float py = shape.center_y + ex * sin_r + ey * cos_r;
                    if (seg > 0) {
                        SDL_RenderDrawLine(sdl_renderer,
                            static_cast<int>(prev_px), static_cast<int>(prev_py),
                            static_cast<int>(px), static_cast<int>(py));
                    }
                    prev_px = px; prev_py = py;
                }
            }
        } else {
            // Ray: draw a thicker highlight line along the ray
            float rdx = std::sin(sensor.angle);
            float rdy = -std::cos(sensor.angle);
            float ex = ship_x + rdx * sensor.range;
            float ey = ship_y + rdy * sensor.range;
            SDL_SetRenderDrawColor(sdl_renderer, hr, hg, hb, ha);
            // Draw 3 parallel lines for thickness
            for (int off = -1; off <= 1; ++off) {
                float ox = -rdy * static_cast<float>(off);
                float oy = rdx * static_cast<float>(off);
                SDL_RenderDrawLine(sdl_renderer,
                    static_cast<int>(ship_x + ox), static_cast<int>(ship_y + oy),
                    static_cast<int>(ex + ox), static_cast<int>(ey + oy));
            }
        }
    }

    // Draw test objects
    for (const auto& obj : state.test_objects) {
        if (obj.is_tower && renderer.game_view.asteroid_texture()) {
            int diam = static_cast<int>(obj.radius * 2.0f);
            SDL_Rect dst = {
                static_cast<int>(obj.x - obj.radius),
                static_cast<int>(obj.y - obj.radius),
                diam, diam
            };
            SDL_RenderCopy(sdl_renderer, renderer.game_view.asteroid_texture(), nullptr, &dst);
        } else if (!obj.is_tower && !renderer.game_view.coin_frames().empty()) {
            int diam = static_cast<int>(obj.radius * 2.0f);
            SDL_Rect dst = {
                static_cast<int>(obj.x - obj.radius),
                static_cast<int>(obj.y - obj.radius),
                diam, diam
            };
            SDL_RenderCopy(sdl_renderer, renderer.game_view.coin_frames()[0], nullptr, &dst);
        } else {
            // Fallback circle
            uint8_t cr = obj.is_tower ? static_cast<uint8_t>(200) : static_cast<uint8_t>(255);
            uint8_t cg = obj.is_tower ? static_cast<uint8_t>(80) : static_cast<uint8_t>(200);
            uint8_t cb = obj.is_tower ? static_cast<uint8_t>(80) : static_cast<uint8_t>(50);
            SDL_SetRenderDrawColor(sdl_renderer, cr, cg, cb, 200);
            int rad = static_cast<int>(obj.radius);
            int ocx = static_cast<int>(obj.x);
            int ocy = static_cast<int>(obj.y);
            for (int dy = -rad; dy <= rad; ++dy) {
                int dx_span = static_cast<int>(std::sqrt(static_cast<float>(rad * rad - dy * dy)));
                SDL_RenderDrawLine(sdl_renderer, ocx - dx_span, ocy + dy, ocx + dx_span, ocy + dy);
            }
        }
    }

    // Draw resolution arc markers (if arcs were computed from occulus sensors)
    if (state.show_arcs && !state.arcs.empty()) {
        constexpr float PI_F = std::numbers::pi_v<float>;
        constexpr float DISPLAY_R = 350.0f;

        // Faint inner circle at the test radius
        SDL_SetRenderDrawColor(sdl_renderer, 255, 50, 50, 180);
        {
            float pa_x = 0, pa_y = 0;
            for (int ai = 0; ai <= 64; ++ai) {
                float th = -PI_F * 0.5f + PI_F * static_cast<float>(ai) / 64.0f;
                float ax = ship_x + std::sin(th) * state.res_test_radius;
                float ay = ship_y - std::cos(th) * state.res_test_radius;
                if (ai > 0)
                    SDL_RenderDrawLine(sdl_renderer,
                        static_cast<int>(pa_x), static_cast<int>(pa_y),
                        static_cast<int>(ax), static_cast<int>(ay));
                pa_x = ax; pa_y = ay;
            }
        }

        // Color palette for arc segments
        struct ArcColor { uint8_t r, g, b; };
        const ArcColor palette[] = {
            {80, 200, 120},   // green
            {100, 150, 255},  // blue
            {255, 180, 60},   // orange
            {200, 80, 200},   // magenta
            {255, 255, 80},   // yellow
            {80, 220, 220},   // cyan
            {255, 100, 100},  // red
            {180, 220, 80},   // lime
        };
        constexpr int PALETTE_SIZE = 8;

        for (std::size_t ai = 0; ai < state.arcs.size(); ++ai) {
            const auto& arc = state.arcs[ai];
            if (arc.active_set.empty()) continue;

            const auto& col = palette[ai % PALETTE_SIZE];
            uint8_t alpha = arc.has_sensor ? static_cast<uint8_t>(200) : static_cast<uint8_t>(140);

            int segs = std::max(3, static_cast<int>(arc.span_rad * 100.0f));
            for (int thickness = -2; thickness <= 2; ++thickness) {
                float r = DISPLAY_R + static_cast<float>(thickness);
                SDL_SetRenderDrawColor(sdl_renderer, col.r, col.g, col.b, alpha);
                float pa_x = 0, pa_y = 0;
                for (int si = 0; si <= segs; ++si) {
                    float t = static_cast<float>(si) / static_cast<float>(segs);
                    float theta = arc.start_rad + arc.span_rad * t;
                    float ax = ship_x + std::sin(theta) * r;
                    float ay = ship_y - std::cos(theta) * r;
                    if (si > 0)
                        SDL_RenderDrawLine(sdl_renderer,
                            static_cast<int>(pa_x), static_cast<int>(pa_y),
                            static_cast<int>(ax), static_cast<int>(ay));
                    pa_x = ax; pa_y = ay;
                }
            }

            // Label at midpoint of arc
            float mid_theta = arc.start_rad + arc.span_rad * 0.5f;
            float label_r = DISPLAY_R + 12.0f;
            float lx = ship_x + std::sin(mid_theta) * label_r;
            float ly = ship_y - std::cos(mid_theta) * label_r;

            char label[64];
            float deg = arc.span_rad * 180.0f / PI_F;
            char set_str[48] = "";
            int spos = 0;
            for (std::size_t fi = 0; fi < arc.active_set.size() && spos < 40; ++fi) {
                spos += std::snprintf(set_str + spos,
                    static_cast<std::size_t>(48 - spos),
                    "%s%d", fi > 0 ? "," : "", arc.active_set[fi]);
            }
            std::snprintf(label, sizeof(label), "%.1f {%s}",
                static_cast<double>(deg), set_str);

            neuralnet_ui::draw_tiny_text(sdl_renderer,
                static_cast<int>(lx) - 20,
                static_cast<int>(ly) - 3,
                label, 1, col.r, col.g, col.b);
        }
    }

    // Draw ship
    renderer.game_view.render_ship_preview(config.ship_type,
        static_cast<int>(ship_x), static_cast<int>(ship_y), 30);

    // Draw horizontal input labels below the ship
    if (!state.input_shared.empty()) {
        int label_base_y = 10;
        const auto& inp = state.input_shared;

        // Helper: draw a small activation bar
        constexpr int BAR_MAX_W = 30;
        constexpr int BAR_H = 3;
        auto draw_bar = [&](int bx, int by, float val,
                            uint8_t br, uint8_t bg, uint8_t bb) {
            float clamped = std::clamp(std::abs(val), 0.0f, 1.0f);
            int bw = static_cast<int>(clamped * static_cast<float>(BAR_MAX_W));
            if (bw < 1) return;
            SDL_SetRenderDrawColor(sdl_renderer, br, bg, bb, 200);
            SDL_Rect bar = {bx, by, bw, BAR_H};
            SDL_RenderFillRect(sdl_renderer, &bar);
        };

        if (!tb_design.sensors.empty()) {
            int num_sensors = static_cast<int>(tb_design.sensors.size());

            // Build angle-sorted display order (left-to-right = most negative angle to most positive)
            std::vector<int> display_order(static_cast<std::size_t>(num_sensors));
            for (int i = 0; i < num_sensors; ++i) display_order[static_cast<std::size_t>(i)] = i;
            std::sort(display_order.begin(), display_order.end(),
                [&](int a, int b) {
                    return tb_design.sensors[static_cast<std::size_t>(a)].angle
                         < tb_design.sensors[static_cast<std::size_t>(b)].angle;
                });

            // Precompute input offset for each sensor (sensors produce inputs in array order)
            std::vector<int> input_offsets(static_cast<std::size_t>(num_sensors));
            {
                int off = 0;
                for (int si = 0; si < num_sensors; ++si) {
                    input_offsets[static_cast<std::size_t>(si)] = off;
                    const auto& sd = tb_design.sensors[static_cast<std::size_t>(si)];
                    off += sd.is_full_sensor ? 5 : 1;  // full sensor = dist+dng+val+con+ally, sight = dist only
                }
            }

            // Draw in angle-sorted order
            for (int di = 0; di < num_sensors; ++di) {
                int si = display_order[static_cast<std::size_t>(di)]; // original sensor index
                const auto& sd = tb_design.sensors[static_cast<std::size_t>(si)];
                int base_input = input_offsets[static_cast<std::size_t>(si)];

                // Display position based on sorted order
                float frac = (static_cast<float>(di) + 0.5f) / static_cast<float>(num_sensors);
                int lx = static_cast<int>(frac * tb_game_w);
                int ly = label_base_y;

                // Overline for selected sensor's input column (5px above labels)
                if (si == state.selected_oval) {
                    float col_w = tb_game_w / static_cast<float>(num_sensors);
                    int col_x = static_cast<int>(frac * tb_game_w - col_w * 0.5f);
                    int overline_y = label_base_y - 7;
                    SDL_SetRenderDrawColor(sdl_renderer, 255, 200, 50, 200);
                    SDL_RenderDrawLine(sdl_renderer, col_x, overline_y,
                                       col_x + static_cast<int>(col_w), overline_y);
                    SDL_RenderDrawLine(sdl_renderer, col_x, overline_y + 1,
                                       col_x + static_cast<int>(col_w), overline_y + 1);
                }

                if (static_cast<std::size_t>(base_input) < inp.size()) {
                    float val = inp[static_cast<std::size_t>(base_input)];
                    auto ic = sd.is_full_sensor ? theme::input_sensor : theme::input_sight;
                    if (val < 0.99f) ic = theme::input_active;
                    uint8_t cr = ic.r, cg = ic.g, cb = ic.b;

                    char name[16];
                    std::snprintf(name, sizeof(name), "%s%d",
                        sd.is_full_sensor ? "S" : "E", si);
                    neuralnet_ui::draw_tiny_text(sdl_renderer,
                        lx - 8, ly, name, 1, cr, cg, cb);

                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(val));
                    neuralnet_ui::draw_tiny_text(sdl_renderer,
                        lx - 8, ly + 8, buf, 1, cr, cg, cb);
                    draw_bar(lx - 8, ly + 15, 1.0f - val, cr, cg, cb);

                    // Sub-values for full sensors (grouped vertically under the distance)
                    if (sd.is_full_sensor && static_cast<std::size_t>(base_input + 3) < inp.size()) {
                        const char* subs[] = {"DNG", "VAL", "CON"};
                        for (int ssi = 0; ssi < 3; ++ssi) {
                            float sv = inp[static_cast<std::size_t>(base_input + 1 + ssi)];
                            auto sc = (sv > 0.01f) ? theme::input_sub_active : theme::input_sub_dim;
                            uint8_t sr = sc.r, sg = sc.g, sb = sc.b;
                            int sub_y = ly + 19 + ssi * 11;
                            std::snprintf(buf, sizeof(buf), "%s%.1f",
                                subs[ssi], static_cast<double>(sv));
                            neuralnet_ui::draw_tiny_text(sdl_renderer,
                                lx - 8, sub_y, buf, 1, sr, sg, sb);
                            draw_bar(lx - 8, sub_y + 7, sv, sr, sg, sb);
                        }
                    }
                }
            }
        } else {
            const bool ray_is_sensor[] = {
                false, false, true, false, true, false, true, false, true, false, true, false, false
            };
            int sight_pos = 0;
            int sensor_pos = 0;

            for (int ri = 0; ri < 13; ++ri) {
                float frac = (static_cast<float>(ri) + 0.5f) / 13.0f;
                int lx = static_cast<int>(frac * tb_game_w);
                int ly = label_base_y;
                char name[8];

                if (ray_is_sensor[ri]) {
                    std::snprintf(name, sizeof(name), "S%d", ri);
                    int base = 8 + sensor_pos * 4;
                    if (static_cast<std::size_t>(base) < inp.size()) {
                        float dv = inp[static_cast<std::size_t>(base)];
                        uint8_t cr = (dv < 0.99f) ? static_cast<uint8_t>(255) : static_cast<uint8_t>(160);
                        neuralnet_ui::draw_tiny_text(sdl_renderer,
                            lx - 8, ly, name, 1, cr, 90, 220);
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(dv));
                        neuralnet_ui::draw_tiny_text(sdl_renderer,
                            lx - 8, ly + 8, buf, 1, cr, 90, 220);
                        draw_bar(lx - 8, ly + 15, 1.0f - dv, cr, 90, 220);
                        const char* subs[] = {"DNG", "VAL", "CON"};
                        for (int si = 0; si < 3; ++si) {
                            float sv = inp[static_cast<std::size_t>(base + 1 + si)];
                            uint8_t sg = (sv > 0.01f) ? static_cast<uint8_t>(200) : static_cast<uint8_t>(60);
                            int sub_y = ly + 19 + si * 11;
                            char sbuf[16];
                            std::snprintf(sbuf, sizeof(sbuf), "%s%.1f", subs[si], static_cast<double>(sv));
                            neuralnet_ui::draw_tiny_text(sdl_renderer,
                                lx - 8, sub_y, sbuf, 1, 255, sg, 80);
                            draw_bar(lx - 8, sub_y + 7, sv, 255, sg, 80);
                        }
                    }
                    ++sensor_pos;
                } else {
                    std::snprintf(name, sizeof(name), "E%d", ri);
                    if (static_cast<std::size_t>(sight_pos) < inp.size()) {
                        float dv = inp[static_cast<std::size_t>(sight_pos)];
                        uint8_t cg = (dv < 0.99f) ? static_cast<uint8_t>(255) : static_cast<uint8_t>(130);
                        neuralnet_ui::draw_tiny_text(sdl_renderer,
                            lx - 8, ly, name, 1, 100, cg, 180);
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(dv));
                        neuralnet_ui::draw_tiny_text(sdl_renderer,
                            lx - 8, ly + 8, buf, 1, 100, cg, 180);
                        draw_bar(lx - 8, ly + 15, 1.0f - dv, 100, cg, 180);
                    }
                    ++sight_pos;
                }
            }
        }

        // POS X, POS Y, SPEED at the far right
        if (inp.size() > state.mem_slots_shared + 3) {
            std::size_t pos_start = inp.size() - state.mem_slots_shared - 3;
            const char* pos_names[] = {"PX", "PY", "SP"};
            for (int pi = 0; pi < 3; ++pi) {
                float pv = inp[pos_start + static_cast<std::size_t>(pi)];
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%s%.2f", pos_names[pi], static_cast<double>(pv));
                neuralnet_ui::draw_tiny_text(sdl_renderer,
                    static_cast<int>(tb_game_w) - 50,
                    label_base_y + pi * 8,
                    buf, 1, 200, 200, 200);
            }
        }
    }

    SDL_RenderSetClipRect(sdl_renderer, nullptr);
}

} // namespace neuroflyer

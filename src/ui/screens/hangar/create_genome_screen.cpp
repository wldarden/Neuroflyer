#include <neuroflyer/ui/screens/create_genome_screen.h>
#include <neuroflyer/ui/screens/variant_viewer_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/evolution.h>
#include <neuroflyer/genome_manager.h>
#include <neuroflyer/name_validation.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet-ui/render_net_topology.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

namespace neuroflyer {

// ==================== on_enter ====================

void CreateGenomeScreen::on_enter() {
    new_genome_name_[0] = '\0';
    sight_rays_ = 8;
    sensor_rays_ = 5;
    memory_ = 4;
    num_hidden_ = 2;
    layer_sizes_[0] = 12;
    layer_sizes_[1] = 12;
    layer_sizes_[2] = 0;
    layer_sizes_[3] = 0;
    vision_type_ = 0;
    evolve_sensor_angle_ = false;
    evolve_sensor_range_ = false;
    evolve_sensor_width_ = false;
    error_message_.clear();
    preview_ = {};
}

// ==================== on_draw ====================

void CreateGenomeScreen::on_draw(
    AppState& state, Renderer& renderer, UIManager& ui) {

    constexpr float pi = std::numbers::pi_v<float>;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::Begin("##CreateGenome", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ==================== TITLE: Centered name input at 1.8x ====================
    ImGui::Dummy(ImVec2(0, 10));
    float name_input_w = sw * 0.5f;
    ImGui::SetCursorPosX((sw - name_input_w) * 0.5f);
    ImGui::SetWindowFontScale(1.8f);
    ImGui::SetNextItemWidth(name_input_w);
    ImGui::InputTextWithHint("##GenomeName", "Genome Name",
                             new_genome_name_, sizeof(new_genome_name_));
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();

    // ==================== CONTENT AREA ====================
    float content_top = ImGui::GetCursorPosY();
    float footer_h = 50.0f;
    float content_h = sh - content_top - footer_h - ImGui::GetStyle().WindowPadding.y;
    float left_w = sw * 0.45f;

    // ==================== LEFT: Config Pane (scrollable) ====================
    ImGui::BeginChild("##CreateConfig", ImVec2(left_w, content_h), true);

    // -- Frame section --
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Frame");
    ImGui::Dummy(ImVec2(0, 3));

    // Ship Body combo
    {
        const char* ship_names[] = {
            "0: Pink Fighter", "1: Blue Multi", "2: Green Cruiser",
            "3: Red Fighter", "4: Orange Interceptor", "5: Blue Stealth",
            "6: Purple Bomber", "7: Orange Rocket", "8: Blue Heavy",
            "9: Green Armored"
        };
        int ship = state.config.ship_type;
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("Ship Body",
                              ship_names[std::clamp(ship, 0, 9)])) {
            for (int i = 0; i < 10; ++i) {
                bool selected = (ship == i);
                if (ImGui::Selectable(ship_names[i], selected)) {
                    state.config.ship_type = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // Vision combo
    {
        const char* vision_names[] = { "Raycast", "Occulus" };
        int vis = vision_type_;
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("Vision",
                              vision_names[std::clamp(vis, 0, 1)])) {
            if (ImGui::Selectable("Raycast", vis == 0))
                vision_type_ = 0;
            if (ImGui::Selectable("Occulus", vis == 1))
                vision_type_ = 1;
            ImGui::EndCombo();
        }
    }

    ImGui::Dummy(ImVec2(0, 10));

    // -- Node Types section --
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Node Types");
    ImGui::Dummy(ImVec2(0, 3));

    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("Sight Rays", &sight_rays_, 0, 13);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("Sensor Rays", &sensor_rays_, 0, 13);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("Memory Slots", &memory_, 0, 16);

    ImGui::Dummy(ImVec2(0, 10));

    // -- Evolution section --
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Evolvable Parameters");
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
        "Checked = can mutate during training");
    ImGui::Dummy(ImVec2(0, 2));

    ImGui::Checkbox("Sensor Angles", &evolve_sensor_angle_);
    ImGui::Checkbox("Sensor Range", &evolve_sensor_range_);
    ImGui::Checkbox("Sensor Width", &evolve_sensor_width_);

    ImGui::Dummy(ImVec2(0, 10));

    // -- Network Layers section --
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Network Layers");
    ImGui::Dummy(ImVec2(0, 3));

    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("Hidden Layers", &num_hidden_, 1, 4);
    for (int li = 0; li < num_hidden_; ++li) {
        char label[32];
        std::snprintf(label, sizeof(label), "Layer %d Size", li + 1);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt(label, &layer_sizes_[li], 1, 64);
    }

    ImGui::Dummy(ImVec2(0, 10));

    // Show computed input/output counts
    int total_inputs = sight_rays_
                     + sensor_rays_ * 4
                     + 3
                     + memory_;
    int total_outputs = 5 + memory_;
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Inputs: %d   Outputs: %d", total_inputs, total_outputs);

    ImGui::EndChild(); // end left config pane

    // ==================== RIGHT: Live Net Preview ====================
    ImGui::SameLine();
    ImGui::BeginChild("##CreateNetPreview", ImVec2(0, content_h), true,
        ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Neural Net Preview");

    // Record screen rect for SDL rendering
    ImVec2 panel_pos = ImGui::GetCursorScreenPos();
    ImVec2 panel_avail = ImGui::GetContentRegionAvail();
    preview_.preview_x = static_cast<int>(panel_pos.x);
    preview_.preview_y = static_cast<int>(panel_pos.y);
    preview_.preview_w = static_cast<int>(panel_avail.x);
    preview_.preview_h = static_cast<int>(panel_avail.y);

    // Build preview design from current slider values
    preview_.preview_design = {};
    preview_.preview_design.memory_slots =
        static_cast<uint16_t>(memory_);
    {
        SensorType stype = (vision_type_ == 1)
            ? SensorType::Occulus
            : SensorType::Raycast;
        float sight_width  = (vision_type_ == 1) ? 0.15f : 0.0f;
        float sensor_width = (vision_type_ == 1) ? 0.3f  : 0.0f;
        for (int r = 0; r < sight_rays_; ++r) {
            float frac = (sight_rays_ > 1)
                ? static_cast<float>(r)
                  / static_cast<float>(sight_rays_ - 1)
                : 0.5f;
            float angle = -pi / 2.0f + pi * frac;
            preview_.preview_design.sensors.push_back(
                {stype, angle, 300.0f, sight_width, false});
        }
        for (int r = 0; r < sensor_rays_; ++r) {
            float frac = (sensor_rays_ > 1)
                ? static_cast<float>(r)
                  / static_cast<float>(sensor_rays_ - 1)
                : 0.5f;
            float angle = -pi / 2.0f + pi * frac;
            preview_.preview_design.sensors.push_back(
                {stype, angle, 300.0f, sensor_width, true});
        }
    }

    // Build preview topology from current slider values
    preview_.preview_topology = {};
    preview_.preview_topology.input_size =
        static_cast<std::size_t>(total_inputs);
    for (int li = 0; li < num_hidden_; ++li) {
        preview_.preview_topology.layers.push_back({
            static_cast<std::size_t>(layer_sizes_[li]),
            neuralnet::Activation::Tanh, {}});
    }
    preview_.preview_topology.layers.push_back({
        static_cast<std::size_t>(total_outputs),
        neuralnet::Activation::Tanh, {}});

    ImGui::Dummy(panel_avail);

    // Build input colors from the preview design
    std::vector<neuralnet_ui::NodeColor> preview_input_colors;
    {
        auto nf_colors = build_input_colors(preview_.preview_design);
        preview_input_colors.reserve(nf_colors.size());
        for (const auto& c : nf_colors) {
            preview_input_colors.push_back({c.r, c.g, c.b});
        }
    }
    // Defer SDL rendering to after ImGui (so it draws on top)
    renderer.defer_topology({renderer.renderer_,
        &preview_.preview_topology,
        preview_.preview_x, preview_.preview_y,
        preview_.preview_w, preview_.preview_h,
        std::move(preview_input_colors)});

    ImGui::EndChild(); // end right preview pane

    // ==================== FOOTER ====================
    ImGui::Separator();
    float pad = ImGui::GetStyle().WindowPadding.x;
    float footer_y = ImGui::GetCursorPosY();

    // Back button (left)
    ImGui::SetCursorPos(ImVec2(pad, footer_y + 5.0f));
    if (ImGui::Button("Back", ImVec2(200, 35))) {
        ui.pop_screen();
    }

    // Create button (right)
    float create_btn_w = 200.0f;
    ImGui::SetCursorPos(ImVec2(sw - pad - create_btn_w, footer_y + 5.0f));
    if (ImGui::Button("Create Genome", ImVec2(create_btn_w, 35.0f))) {
        std::string genome_name(new_genome_name_);
        if (!is_valid_name(genome_name)) {
            error_message_ = "Invalid name. Use letters, numbers, spaces, hyphens, or underscores (1-64 chars).";
        } else {
            // Build ShipDesign from slider values
            ShipDesign design;
            design.memory_slots = static_cast<uint16_t>(memory_);
            {
                SensorType stype = (vision_type_ == 1)
                    ? SensorType::Occulus
                    : SensorType::Raycast;
                float sight_width  = (vision_type_ == 1) ? 0.15f : 0.0f;
                float sensor_width = (vision_type_ == 1) ? 0.3f  : 0.0f;
                for (int r = 0; r < sight_rays_; ++r) {
                    float frac = (sight_rays_ > 1)
                        ? static_cast<float>(r)
                          / static_cast<float>(sight_rays_ - 1)
                        : 0.5f;
                    float angle = -pi / 2.0f + pi * frac;
                    design.sensors.push_back(
                        {stype, angle, 300.0f, sight_width, false});
                }
                for (int r = 0; r < sensor_rays_; ++r) {
                    float frac = (sensor_rays_ > 1)
                        ? static_cast<float>(r)
                          / static_cast<float>(sensor_rays_ - 1)
                        : 0.5f;
                    float angle = -pi / 2.0f + pi * frac;
                    design.sensors.push_back(
                        {stype, angle, 300.0f, sensor_width, true});
                }
            }

            assign_sensor_ids(design);

            // Set evolvable flags from checkboxes
            design.evolvable.sensor_angle = evolve_sensor_angle_;
            design.evolvable.sensor_range = evolve_sensor_range_;
            design.evolvable.sensor_width = evolve_sensor_width_;

            // Build hidden layers vector
            std::vector<std::size_t> hidden;
            for (int li = 0; li < num_hidden_; ++li) {
                hidden.push_back(
                    static_cast<std::size_t>(layer_sizes_[li]));
            }

            // Create genome + initial variant
            auto snap = create_random_snapshot(
                genome_name, design, hidden, state.rng);
            try {
                std::string genomes_dir = state.data_dir + "/genomes";
                create_genome(genomes_dir, snap);

                Snapshot initial_variant = snap;
                initial_variant.name = genome_name + " v0";
                initial_variant.parent_name = genome_name;
                save_variant(genomes_dir + "/" + genome_name, initial_variant);

                state.active_genome = genome_name;
                state.genomes_dirty = true;
                state.variants_dirty = true;
                state.lineage_dirty = true;
                new_genome_name_[0] = '\0';
                error_message_.clear();

                // Navigate forward to VariantViewer
                ui.push_screen(std::make_unique<VariantViewerScreen>());
            } catch (const std::exception& e) {
                error_message_ = std::string("Failed to create genome: ") + e.what();
            }
        }
    }

    // Display error message below the Create button
    if (!error_message_.empty()) {
        ImGui::SetCursorPos(ImVec2(sw - pad - create_btn_w, footer_y + 42.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", error_message_.c_str());
    }

    ImGui::End();
}

} // namespace neuroflyer

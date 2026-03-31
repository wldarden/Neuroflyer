#include <neuroflyer/ui/screens/hangar/variant_net_editor_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/ui/modals/input_modal.h>
#include <neuroflyer/ui/modals/layer_editor_modal.h>
#include <neuroflyer/ui/modals/node_editor_modal.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

#include <string>
#include <utility>

namespace neuroflyer {

VariantNetEditorScreen::VariantNetEditorScreen(
    Individual individual, neuralnet::Network network,
    ShipDesign ship_design, std::string variant_path,
    std::string variant_name, NetType net_type)
    : individual_(std::move(individual))
    , ship_design_(std::move(ship_design))
    , net_type_(net_type)
    , variant_path_(std::move(variant_path))
    , variant_name_(std::move(variant_name))
{
    networks_.push_back(std::move(network));
    init_editor_from_topology();

    // Wire designer on_create callback
    designer_state_.on_create = [this](Individual ind, neuralnet::Network net) {
        individual_ = std::move(ind);
        networks_.clear();
        networks_.push_back(std::move(net));
        ship_design_ = individual_.effective_ship_design();
        init_editor_from_topology();
    };

    // Set up viewer state (non-owning pointers)
    viewer_state_.individual = &individual_;
    viewer_state_.network = &networks_[0];
    viewer_state_.ship_design = ship_design_;
    viewer_state_.net_type = net_type_;
    viewer_state_.editor_mode = true;
    viewer_state_.zoom = 2.0f;
    viewer_state_.zoom_enabled = true;
}

void VariantNetEditorScreen::init_editor_from_topology() {
    const auto& topo = individual_.topology;
    designer_state_.edit_num_hidden = std::max(1,
        static_cast<int>(topo.layers.size()) - 1);
    for (int li = 0;
         li < designer_state_.edit_num_hidden && li < 8;
         ++li) {
        designer_state_.edit_layer_sizes[li] =
            static_cast<int>(
                topo.layers[static_cast<std::size_t>(li)].output_size);
    }
    // Derive current memory slots from output layer
    if (!topo.layers.empty()) {
        int mem = static_cast<int>(topo.layers.back().output_size)
                  - static_cast<int>(ACTION_COUNT);
        designer_state_.current_memory_slots = std::max(0, mem);
    } else {
        designer_state_.current_memory_slots = 0;
    }

    // Re-sync viewer pointers after data replacement
    viewer_state_.individual = &individual_;
    if (!networks_.empty()) {
        viewer_state_.network = &networks_[0];
    }
    viewer_state_.ship_design = ship_design_;
    viewer_state_.net_type = net_type_;
}

void VariantNetEditorScreen::on_draw(
    AppState& state, Renderer& renderer, UIManager& ui) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw_f = display.x;
    const float sh_f = display.y;

    // ----- Full-screen ImGui window -----
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw_f, sh_f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::Begin("##NetEditorScreen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header bar
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
    ImGui::Text("NETWORK VIEWER");
    ImGui::PopStyleColor();
    ImGui::SameLine(sw_f - 150.0f);
    if (ImGui::Button("Back (Esc)", ImVec2(130, 25))) {
        ImGui::End();
        ui.pop_screen();
        return;
    }

    // Escape to go back (only when no modal is open)
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::End();
        ui.pop_screen();
        return;
    }

    ImGui::Separator();

    // Topology info
    if (!individual_.topology.layers.empty()) {
        ImGui::Text("Fitness: %.0f   Inputs: %zu   Hidden layers: %zu   Output: %zu",
            static_cast<double>(individual_.fitness),
            individual_.topology.input_size,
            individual_.topology.layers.size() - 1,
            individual_.topology.layers.back().output_size);
        for (std::size_t li = 0; li + 1 < individual_.topology.layers.size(); ++li) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
                "  H%zu:%zu", li, individual_.topology.layers[li].output_size);
        }
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));

    // ----- Left panel: Net Designer -----
    ImGui::BeginChild("##NetDesigner", ImVec2(300, 0), true);
    draw_net_designer(designer_state_, ship_design_,
                      state.config.population_size);
    ImGui::EndChild();

    // ----- Save As button (bottom-right) -----
    {
        constexpr float SAVE_BTN_W = 140.0f;
        constexpr float SAVE_BTN_H = 32.0f;
        float pad = ImGui::GetStyle().WindowPadding.x;
        ImGui::SetCursorPos(ImVec2(
            sw_f - SAVE_BTN_W - pad * 2.0f,
            sh_f - SAVE_BTN_H - pad * 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
        if (ImGui::Button("Save As", ImVec2(SAVE_BTN_W, SAVE_BTN_H))) {
            ui.push_modal(std::make_unique<InputModal>(
                "Save Variant",
                "Enter variant name:",
                [this, &state](const std::string& name) {
                    try {
                        auto snap = load_snapshot(variant_path_);
                        snap.weights = individual_.genome.flatten("layer_");
                        snap.topology = individual_.topology;
                        sync_activations_from_genome(individual_.genome, snap.topology);
                        if (name == snap.name) {
                            save_snapshot(snap, variant_path_);
                        } else {
                            snap.name = name;
                            std::string dir =
                                variant_path_.substr(
                                    0, variant_path_.rfind('/'));
                            save_snapshot(snap, dir + "/" + name + ".bin");
                        }
                        state.variants_dirty = true;
                        state.lineage_dirty = true;
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to save snapshot: " << e.what() << "\n";
                    }
                },
                variant_name_));
        }
        ImGui::PopStyleColor();
    }

    ImGui::End();

    // ----- Right area: Net Viewer (SDL deferred rendering) -----
    // Wire click callbacks (must capture ui by ref for modal push)
    viewer_state_.on_node_click =
        [this, &ui](const NodeClickInfo& info) {
            if (ui.has_modal()) ui.pop_modal();
            NetEditorContext ctx{
                individual_,
                networks_,
                0,
                ship_design_
            };
            ui.push_modal(std::make_unique<NodeEditorModal>(
                NodeEditorModal::NodeRef{info.column, info.node},
                ctx));
        };

    viewer_state_.on_layer_click =
        [this, &ui](int column) {
            if (ui.has_modal()) ui.pop_modal();
            NetEditorContext ctx{
                individual_,
                networks_,
                0,
                ship_design_
            };
            ui.push_modal(std::make_unique<LayerEditorModal>(
                column, ctx));
        };

    // Set render bounds for the net panel:
    // - Top edge: below the header/topology/separator (~70px)
    // - Bottom edge: above the Save As button (32px + padding)
    constexpr float LEFT_PANEL_W = 320.0f;
    constexpr float HEADER_H = 75.0f;
    constexpr float SAVE_BTN_H = 32.0f;
    float pad = ImGui::GetStyle().WindowPadding.x;
    float bottom_margin = SAVE_BTN_H + pad * 3.0f;

    viewer_state_.render_x = static_cast<int>(LEFT_PANEL_W) + 10;
    viewer_state_.render_y = static_cast<int>(HEADER_H);
    viewer_state_.render_w = static_cast<int>(display.x - LEFT_PANEL_W) - 20;
    viewer_state_.render_h = static_cast<int>(display.y - HEADER_H - bottom_margin);
    viewer_state_.modal_open = ui.has_modal();

    draw_net_viewer_view(viewer_state_, renderer.renderer_);
}

void VariantNetEditorScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(viewer_state_, sdl_renderer);
}

} // namespace neuroflyer

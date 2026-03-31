#include <neuroflyer/ui/views/topology_preview_view.h>

#include <neuroflyer/renderers/variant_net_render.h>
#include <neuroflyer/sensor_engine.h>

#include <neuralnet-ui/render_net_topology.h>

#include <imgui.h>

namespace neuroflyer {

void TopologyPreviewView::set_topology(
    const neuralnet::NetworkTopology* topology,
    const ShipDesign* design) {
    topology_ = topology;
    design_ = design;
}

void TopologyPreviewView::clear() {
    topology_ = nullptr;
    design_ = nullptr;
}

void TopologyPreviewView::on_draw(
    AppState& /*state*/, Renderer& renderer, UIManager& /*ui*/) {

    ImGui::BeginChild("##TopologyPreview",
        ImVec2(w_, h_), true,
        ImGuiWindowFlags_NoScrollbar);

    // Record the screen rect for SDL deferred rendering
    ImVec2 panel_pos = ImGui::GetCursorScreenPos();
    ImVec2 panel_avail = ImGui::GetContentRegionAvail();
    int px = static_cast<int>(panel_pos.x);
    int py = static_cast<int>(panel_pos.y);
    int pw = static_cast<int>(panel_avail.x);
    int ph = static_cast<int>(panel_avail.y);

    if (topology_ != nullptr && !topology_->layers.empty()
        && pw > 0 && ph > 0) {
        ImGui::TextColored(
            ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Neural Net Preview");

        // Reserve space for SDL rendering
        ImGui::Dummy(ImGui::GetContentRegionAvail());

        // Build input colors from ShipDesign
        std::vector<neuralnet_ui::NodeColor> input_colors;
        if (design_ != nullptr) {
            auto nf_colors = build_input_colors(*design_);
            input_colors.reserve(nf_colors.size());
            for (const auto& c : nf_colors) {
                input_colors.push_back({c.r, c.g, c.b});
            }
        }

        // Defer SDL rendering to after ImGui
        renderer.defer_topology({renderer.renderer_, topology_,
            px, py, pw, ph, std::move(input_colors)});
    } else {
        ImGui::TextColored(
            ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "No preview");
    }

    ImGui::EndChild();
}

} // namespace neuroflyer

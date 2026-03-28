#include <neuroflyer/ui/modals/layer_editor_modal.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuralnet/activation.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace neuroflyer {

LayerEditorModal::LayerEditorModal(int column, NetEditorContext ctx)
    : EditorModal("Layer Properties")
    , column_(column)
    , ctx_(std::move(ctx))
    , genome_backup_(ctx_.individual.genome) {}

void LayerEditorModal::on_ok(UIManager& /*ui*/) {
    // Changes are applied in real-time; nothing extra needed.
}

void LayerEditorModal::on_cancel(UIManager& /*ui*/) {
    if (modified_) {
        ctx_.individual.genome = genome_backup_;
        ctx_.networks[ctx_.best_idx] = ctx_.individual.build_network();
    }
}

void LayerEditorModal::draw_form(AppState& /*state*/, UIManager& /*ui*/) {
    const auto& topo = ctx_.individual.topology;

    if (column_ == 0) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "Input Layer");
        ImGui::Separator();
        ImGui::Text("Nodes: %zu", topo.input_size);
        ImGui::Text("Inputs are read-only.");
    } else if (column_ > 0
               && static_cast<std::size_t>(column_) <= topo.layers.size()) {

        std::size_t layer_idx = static_cast<std::size_t>(column_) - 1;
        bool is_output = (layer_idx == topo.layers.size() - 1);
        const auto& layer = topo.layers[layer_idx];

        if (is_output) {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.5f, 1.0f),
                "Output Layer");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                "Hidden Layer %zu", layer_idx + 1);
        }
        ImGui::Separator();

        ImGui::Text("Nodes: %zu", layer.output_size);
        auto prev_size = (layer_idx == 0)
            ? topo.input_size
            : topo.layers[layer_idx - 1].output_size;
        ImGui::Text("Incoming connections per node: %zu", prev_size);

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        std::string act_gene =
            "layer_" + std::to_string(layer_idx) + "_activations";

        if (ctx_.individual.genome.has_gene(act_gene)) {
            const auto& ag = ctx_.individual.genome.gene(act_gene);

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Activation Functions");
            ImGui::Dummy(ImVec2(0, 2));

            const char* act_names[] = {
                "ReLU", "Sigmoid", "Tanh", "Linear",
                "Gaussian", "Sine", "Abs"};

            int counts[7] = {};
            for (const auto& v : ag.values) {
                int idx = std::clamp(static_cast<int>(std::round(v)),
                    0, neuralnet::ACTIVATION_COUNT - 1);
                counts[idx]++;
            }
            for (int a = 0; a < neuralnet::ACTIVATION_COUNT; ++a) {
                if (counts[a] > 0) {
                    ImGui::Text("  %s: %d", act_names[a], counts[a]);
                }
            }

            ImGui::Dummy(ImVec2(0, 5));
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Set All To:");
            ImGui::Dummy(ImVec2(0, 2));

            for (int a = 0; a < neuralnet::ACTIVATION_COUNT; ++a) {
                if (ImGui::Button(act_names[a], ImVec2(140, 28))) {
                    auto& mg = ctx_.individual.genome.gene(act_gene);
                    for (auto& v : mg.values) {
                        v = static_cast<float>(a);
                    }
                    ctx_.networks[ctx_.best_idx] =
                        ctx_.individual.build_network();
                    modified_ = true;
                }
                if (a % 2 == 0 && a + 1 < neuralnet::ACTIVATION_COUNT) {
                    ImGui::SameLine();
                }
            }
        } else {
            ImGui::Text("Activation: %s (per-layer, not per-node)",
                layer.activation == neuralnet::Activation::Tanh ? "Tanh"
                : layer.activation == neuralnet::Activation::ReLU ? "ReLU"
                : "?");
        }
    }
}

} // namespace neuroflyer

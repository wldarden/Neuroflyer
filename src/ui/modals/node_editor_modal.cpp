#include <neuroflyer/ui/modals/node_editor_modal.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/sensor_engine.h>

#include <neuralnet/activation.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace neuroflyer {

NodeEditorModal::NodeEditorModal(NodeRef node, NetEditorContext ctx)
    : EditorModal("Node Properties")
    , node_(node)
    , ctx_(std::move(ctx))
    , genome_backup_(ctx_.individual.genome) {}

void NodeEditorModal::on_ok(UIManager& /*ui*/) {
    // Changes are applied in real-time; nothing extra needed.
}

void NodeEditorModal::on_cancel(UIManager& /*ui*/) {
    if (modified_) {
        ctx_.individual.genome = genome_backup_;
        ctx_.networks[ctx_.best_idx] = ctx_.individual.build_network();
    }
}

void NodeEditorModal::draw_form(AppState& /*state*/, UIManager& /*ui*/) {
    const auto& topo = ctx_.individual.topology;
    int col = node_.column;
    int node = node_.node;

    if (col == 0) {
        // ==================== INPUT NODE ====================
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f),
            "Input Node %d", node);
        ImGui::Separator();
        ImGui::Text("Type: Input (read-only)");

        auto labels = build_input_labels(ctx_.ship_design);
        auto order = build_display_order(ctx_.ship_design);
        std::size_t di = (static_cast<std::size_t>(node) < order.size())
            ? order[static_cast<std::size_t>(node)]
            : static_cast<std::size_t>(node);
        if (di < labels.size()) {
            ImGui::Text("Label: %s", labels[di].c_str());
        }

    } else if (col > 0
               && static_cast<std::size_t>(col) <= topo.layers.size()) {
        // ==================== HIDDEN / OUTPUT NODE ====================
        std::size_t layer_idx = static_cast<std::size_t>(col) - 1;
        bool is_output = (layer_idx == topo.layers.size() - 1);
        const auto& layer = topo.layers[layer_idx];

        if (is_output) {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.5f, 1.0f),
                "Output Node %d", node);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                "Hidden Layer %zu, Node %d", layer_idx + 1, node);
        }
        ImGui::Separator();

        // --- Activation function ---
        std::string act_gene =
            "layer_" + std::to_string(layer_idx) + "_activations";
        if (ctx_.individual.genome.has_gene(act_gene)) {
            const auto& ag = ctx_.individual.genome.gene(act_gene);
            if (static_cast<std::size_t>(node) < ag.values.size()) {
                int act_idx = std::clamp(
                    static_cast<int>(std::round(
                        ag.values[static_cast<std::size_t>(node)])),
                    0, neuralnet::ACTIVATION_COUNT - 1);
                const char* act_names[] = {
                    "ReLU", "Sigmoid", "Tanh", "Linear",
                    "Gaussian", "Sine", "Abs"};
                ImGui::Text("Activation:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::BeginCombo("##act", act_names[act_idx])) {
                    for (int a = 0; a < neuralnet::ACTIVATION_COUNT; ++a) {
                        if (ImGui::Selectable(act_names[a], a == act_idx)) {
                            auto& mg = ctx_.individual.genome.gene(act_gene);
                            mg.values[static_cast<std::size_t>(node)] =
                                static_cast<float>(a);
                            ctx_.networks[ctx_.best_idx] =
                                ctx_.individual.build_network();
                            modified_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        } else {
            const char* act_name =
                (layer.activation == neuralnet::Activation::Tanh) ? "Tanh"
                : (layer.activation == neuralnet::Activation::ReLU) ? "ReLU"
                : (layer.activation == neuralnet::Activation::Sigmoid) ? "Sigmoid"
                : "?";
            ImGui::Text("Activation: %s (per-layer)", act_name);
        }

        // --- Bias ---
        std::string bias_gene =
            "layer_" + std::to_string(layer_idx) + "_biases";
        if (ctx_.individual.genome.has_gene(bias_gene)) {
            const auto& bg = ctx_.individual.genome.gene(bias_gene);
            if (static_cast<std::size_t>(node) < bg.values.size()) {
                float bias = bg.values[static_cast<std::size_t>(node)];
                ImGui::Text("Bias: %.4f", static_cast<double>(bias));
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderFloat("##bias", &bias, -5.0f, 5.0f,
                        "%.4f")) {
                    auto& mg = const_cast<evolve::Gene&>(
                        ctx_.individual.genome.gene(bias_gene));
                    mg.values[static_cast<std::size_t>(node)] = bias;
                    ctx_.networks[ctx_.best_idx] =
                        ctx_.individual.build_network();
                    modified_ = true;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 3));

        // --- Layer info ---
        ImGui::Text("Layer size: %zu nodes", layer.output_size);
        auto prev_size = (layer_idx == 0)
            ? topo.input_size
            : topo.layers[layer_idx - 1].output_size;
        ImGui::Text("Incoming connections: %zu", prev_size);

        // --- Incoming weights ---
        std::string w_gene =
            "layer_" + std::to_string(layer_idx) + "_weights";
        if (ctx_.individual.genome.has_gene(w_gene)) {
            const auto& wg = ctx_.individual.genome.gene(w_gene);
            std::size_t start =
                static_cast<std::size_t>(node) * prev_size;
            if (start + prev_size <= wg.values.size()) {
                ImGui::Dummy(ImVec2(0, 3));
                if (ImGui::TreeNode("Incoming Weights")) {
                    for (std::size_t i = 0; i < prev_size; ++i) {
                        float w = wg.values[start + i];
                        ImGui::Text("[%zu] %.4f", i,
                            static_cast<double>(w));
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
}

} // namespace neuroflyer

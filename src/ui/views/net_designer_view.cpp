#include <neuroflyer/ui/views/net_designer_view.h>

#include <neuroflyer/sensor_engine.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

namespace neuroflyer {

void draw_net_designer(NetDesignerState& state, const ShipDesign& ship_design,
                       std::size_t population_size) {
    constexpr int MAX_HIDDEN_LAYERS = 8;

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Create New Network");
    ImGui::Dummy(ImVec2(0, 3));

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Hidden Layers", &state.edit_num_hidden, 1, 1);
    state.edit_num_hidden = std::clamp(state.edit_num_hidden, 1, MAX_HIDDEN_LAYERS);

    ImGui::Dummy(ImVec2(0, 3));
    for (int li = 0; li < state.edit_num_hidden; ++li) {
        char label[32];
        std::snprintf(label, sizeof(label), "Layer %d Nodes", li + 1);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt(label, &state.edit_layer_sizes[li], 1, 4);
        state.edit_layer_sizes[li] = std::clamp(state.edit_layer_sizes[li], 1, 128);
    }

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Text("Memory Slots: %d", state.current_memory_slots);

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("New Net Memory", &state.edit_memory_slots, 1, 2);
    state.edit_memory_slots = std::clamp(state.edit_memory_slots, 0, 16);

    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button("Create Random Net", ImVec2(270, 30))) {
        ShipDesign design = ship_design;
        // Override memory slots from the UI slider
        design.memory_slots = static_cast<uint16_t>(std::max(0, state.edit_memory_slots));
        auto input_size = compute_input_size(design);
        auto output_size = compute_output_size(design);

        std::vector<std::size_t> hidden;
        for (int li = 0; li < state.edit_num_hidden; ++li) {
            hidden.push_back(static_cast<std::size_t>(state.edit_layer_sizes[li]));
        }

        EvolutionConfig evo_cfg;
        evo_cfg.population_size = population_size;

        std::random_device rd;
        std::mt19937 rng(rd());
        auto population = create_population(
            input_size, hidden, output_size, evo_cfg, rng);

        if (!population.empty() && state.on_create) {
            auto net = population[0].build_network();
            state.on_create(std::move(population[0]), std::move(net));
        }
        std::cout << "Created new random net: "
                  << state.edit_num_hidden << " hidden layers\n";
    }
}

} // namespace neuroflyer

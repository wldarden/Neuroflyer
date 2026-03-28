#include <neuroflyer/components/pause_evolution.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>

namespace neuroflyer {
namespace {

constexpr float FOOTER_H = 50.0f;

} // namespace

void draw_pause_evolution(AppState& state, FlySessionState& fly_state) {
    auto& config = state.config;
    auto& evo = fly_state.evo_config;

    float sh = ImGui::GetIO().DisplaySize.y;
    float content_top = ImGui::GetCursorPosY();
    float content_h = sh - content_top - FOOTER_H - ImGui::GetStyle().WindowPadding.y;

    ImGui::BeginChild("##EvoPane", ImVec2(0, content_h), true);

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Evolution Settings");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Population ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Population");
    {
        int pop = static_cast<int>(evo.population_size);
        if (ImGui::InputInt("Population Size", &pop, 10, 50)) {
            evo.population_size = static_cast<std::size_t>(std::max(10, pop));
            config.population_size = evo.population_size;
        }
        int elite = static_cast<int>(evo.elitism_count);
        if (ImGui::InputInt("Elitism Count", &elite, 1, 5)) {
            evo.elitism_count = static_cast<std::size_t>(std::max(1, elite));
            config.elitism_count = evo.elitism_count;
        }
        int tourn = static_cast<int>(evo.tournament_size);
        if (ImGui::InputInt("Tournament Size", &tourn, 1, 5)) {
            evo.tournament_size = static_cast<std::size_t>(std::max(2, tourn));
        }
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Number of random individuals competing per selection. Higher = more selective.");
        }
    }
    ImGui::Dummy(ImVec2(0, 5));

    // --- Weight Mutations ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Weight Mutations");
    {
        ImGui::SliderFloat("Mutation Rate", &evo.weight_mutation_rate, 0.0f, 1.0f, "%.3f");
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Probability each weight gets mutated per generation.");
        }
        ImGui::SliderFloat("Mutation Strength", &evo.weight_mutation_strength, 0.01f, 2.0f, "%.3f");
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Standard deviation of Gaussian noise added to each mutated weight.\n"
                "E.g., strength=0.3 means ~68%% of mutations change by 0.3 or less,\n"
                "~95%% by 0.6 or less, with rare larger jumps.\n"
                "NOT a hard range — it's a bell curve centered at 0.");
        }
    }
    ImGui::Dummy(ImVec2(0, 5));

    // --- Topology Mutations ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Topology Mutations");
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Each chance is rolled ONCE per individual per generation.\n"
                "If it hits, ONE random node or layer is affected.\n"
                "E.g., Add Node 0.01 = 1%% chance per individual per generation\n"
                "that one random hidden layer gains a node.\n"
                "These are independent rolls — an individual could get\n"
                "both an added node and a removed layer in the same generation.");
        }
        ImGui::SliderFloat("Add Node Chance", &evo.add_node_chance, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat("Remove Node Chance", &evo.remove_node_chance, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat("Add Layer Chance", &evo.add_layer_chance, 0.0f, 0.05f, "%.4f");
        ImGui::SliderFloat("Remove Layer Chance", &evo.remove_layer_chance, 0.0f, 0.05f, "%.4f");
    }
    ImGui::Dummy(ImVec2(0, 5));

    // --- Sensor Evolution ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Sensor Evolution");
    {
        auto& ev = fly_state.ship_design.evolvable;
        ImGui::Checkbox("Evolve Sensor Angles", &ev.sensor_angle);
        ImGui::Checkbox("Evolve Sensor Range", &ev.sensor_range);
        ImGui::Checkbox("Evolve Sensor Width", &ev.sensor_width);
        ImGui::Checkbox("Evolve Activation Functions", &ev.activation_function);
    }

    ImGui::EndChild();
}

} // namespace neuroflyer

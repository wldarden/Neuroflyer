#include <neuroflyer/components/fitness_editor.h>

#include <imgui.h>
#include <neuroflyer/config.h>

namespace neuroflyer {

void draw_fitness_editor(GameConfig& config) {
    // --- Scoring weights ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scoring");
    ImGui::InputFloat("Pts / Distance", &config.pts_per_distance, 0.01f, 0.1f, "%.3f");
    ImGui::InputFloat("Pts / Tower Destroyed", &config.pts_per_tower, 1.0f, 10.0f, "%.1f");
    ImGui::InputFloat("Pts / Token Collected", &config.pts_per_token, 10.0f, 100.0f, "%.0f");
    ImGui::InputFloat("Pts / Bullet Fired", &config.pts_per_bullet, 0.1f, 1.0f, "%.2f");
    ImGui::Dummy(ImVec2(0, 10));

    // --- Position multipliers ---
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Position Multipliers");
    ImGui::InputFloat("X at Center", &config.x_center_mult, 0.1f, 0.5f, "%.2f");
    ImGui::InputFloat("X at Edge", &config.x_edge_mult, 0.1f, 0.5f, "%.2f");
    ImGui::InputFloat("Y at Top", &config.y_top_mult, 0.1f, 0.5f, "%.2f");
    ImGui::InputFloat("Y at Center", &config.y_center_mult, 0.1f, 0.5f, "%.2f");
    ImGui::InputFloat("Y at Bottom", &config.y_bottom_mult, 0.1f, 0.5f, "%.2f");
    ImGui::Dummy(ImVec2(0, 5));

    // --- Formula reference ---
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
        "Score = distance * pts_per_distance * avg(x_mult, y_mult)\n"
        "      + towers_destroyed * pts_per_tower\n"
        "      + tokens_collected * pts_per_token\n"
        "      + bullets_fired * pts_per_bullet");
}

} // namespace neuroflyer

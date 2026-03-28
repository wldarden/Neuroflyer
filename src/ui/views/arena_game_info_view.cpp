#include <neuroflyer/ui/views/arena_game_info_view.h>

#include <imgui.h>

namespace neuroflyer {

void draw_arena_game_info_view(const ArenaInfoState& info) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("ARENA");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Generation: %zu", info.generation);
    ImGui::Spacing();

    // Time remaining
    uint32_t remaining_ticks = 0;
    if (info.current_tick < info.time_limit_ticks) {
        remaining_ticks = info.time_limit_ticks - info.current_tick;
    }
    float remaining_seconds = static_cast<float>(remaining_ticks) / 60.0f;
    int minutes = static_cast<int>(remaining_seconds) / 60;
    int seconds = static_cast<int>(remaining_seconds) % 60;

    if (minutes > 0) {
        ImGui::Text("Time: %d:%02d", minutes, seconds);
    } else {
        ImGui::Text("Time: %ds", seconds);
    }

    // Progress bar for time
    float progress = (info.time_limit_ticks > 0)
        ? static_cast<float>(info.current_tick) / static_cast<float>(info.time_limit_ticks)
        : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 0.8f, 1.0f));
    ImGui::ProgressBar(progress, ImVec2(-1, 6), "");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Ships alive
    ImGui::Text("Ships: %zu / %zu", info.alive_count, info.total_count);
    ImGui::Text("Teams alive: %zu / %zu", info.teams_alive, info.num_teams);
    ImGui::Spacing();

    // Per-team scores and kills
    if (!info.team_scores.empty()) {
        ImGui::Separator();
        ImGui::Spacing();
        for (std::size_t t = 0; t < info.num_teams; ++t) {
            float score = (t < info.team_scores.size()) ? info.team_scores[t] : 0.0f;
            int ek = (t < info.team_enemy_kills.size()) ? info.team_enemy_kills[t] : 0;
            int ak = (t < info.team_ally_kills.size()) ? info.team_ally_kills[t] : 0;
            ImGui::Text("Team %zu: %.0f pts  K:%d  TK:%d",
                        t + 1, score, ek, ak);
        }
        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Keyboard hints
    ImGui::TextDisabled("Controls:");
    ImGui::TextDisabled("  Tab/Shift+Tab  Follow ship");
    ImGui::TextDisabled("  Arrow keys     Free camera");
    ImGui::TextDisabled("  Scroll/+/-     Zoom");
    ImGui::TextDisabled("  1-4            Speed");
    ImGui::TextDisabled("  Space          Pause");
    ImGui::TextDisabled("  Escape         Exit");
}

} // namespace neuroflyer

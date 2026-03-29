#include <neuroflyer/ui/views/arena_config_view.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

namespace neuroflyer {

bool draw_arena_config_view(ArenaConfig& config) {
    bool start_clicked = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::Text("ARENA MODE");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Teams ---
    ui::section_header("Teams");

    int num_teams = static_cast<int>(config.num_teams);
    if (ui::input_int("Number of Teams", &num_teams, 2, 8)) {
        config.num_teams = static_cast<std::size_t>(num_teams);
    }

    int num_squads = static_cast<int>(config.num_squads);
    if (ui::input_int("Squads per Team", &num_squads, 1, 10)) {
        config.num_squads = static_cast<std::size_t>(num_squads);
    }

    int fighters_per_squad = static_cast<int>(config.fighters_per_squad);
    if (ui::input_int("Fighters per Squad", &fighters_per_squad, 1, 50)) {
        config.fighters_per_squad = static_cast<std::size_t>(fighters_per_squad);
    }

    ImGui::TextDisabled("Population: %zu ships",
        config.population_size());
    ImGui::Spacing();

    // --- World ---
    ui::section_header("World");

    float world_w = config.world_width;
    if (ui::slider_float("World Width", &world_w, 2000.0f, 200000.0f)) {
        config.world_width = world_w;
    }

    float world_h = config.world_height;
    if (ui::slider_float("World Height", &world_h, 2000.0f, 200000.0f)) {
        config.world_height = world_h;
    }
    ImGui::Spacing();

    // --- Boundaries ---
    ui::section_header("Boundaries");

    ui::checkbox("Wrap North/South", &config.wrap_ns);
    ui::checkbox("Wrap East/West", &config.wrap_ew);
    ImGui::Spacing();

    // --- Round ---
    ui::section_header("Round");

    // Convert ticks to seconds for display (60 fps)
    float time_seconds = static_cast<float>(config.time_limit_ticks) / 60.0f;
    if (ui::slider_float("Time Limit (seconds)", &time_seconds, 10.0f, 300.0f)) {
        config.time_limit_ticks = static_cast<uint32_t>(time_seconds * 60.0f);
    }

    int rounds = static_cast<int>(config.rounds_per_generation);
    if (ui::input_int("Rounds per Generation", &rounds, 1, 10)) {
        config.rounds_per_generation = static_cast<std::size_t>(rounds);
    }
    ImGui::Spacing();

    // --- Obstacles ---
    ui::section_header("Obstacles");

    int towers = static_cast<int>(config.tower_count);
    if (ui::input_int("Tower Count", &towers, 0, 1000)) {
        config.tower_count = static_cast<std::size_t>(towers);
    }

    int tokens = static_cast<int>(config.token_count);
    if (ui::input_int("Token Count", &tokens, 0, 1000)) {
        config.token_count = static_cast<std::size_t>(tokens);
    }
    ImGui::Spacing();

    // --- Combat ---
    ui::section_header("Combat");

    ui::slider_float("Bullet Max Range", &config.bullet_max_range, 100.0f, 5000.0f);
    ui::slider_float("Rotation Speed", &config.rotation_speed, 0.01f, 0.2f);
    ImGui::Spacing();

    // --- Buttons ---
    ImGui::Separator();
    ImGui::Spacing();

    if (ui::button("Start Arena", ui::ButtonStyle::Primary, 200.0f)) {
        start_clicked = true;
    }

    ImGui::SameLine();

    if (ui::button("Back", ui::ButtonStyle::Secondary, 120.0f)) {
        // Caller should handle pop via Escape; this is an explicit back button.
        // We signal by returning false and letting Escape handle it.
        // For the explicit back button, we use ImGui key injection.
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    }

    return start_clicked;
}

} // namespace neuroflyer

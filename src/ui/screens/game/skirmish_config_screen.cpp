#include <neuroflyer/ui/screens/skirmish_config_screen.h>
#include <neuroflyer/ui/screens/skirmish_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

#include <memory>

namespace neuroflyer {

SkirmishConfigScreen::SkirmishConfigScreen(Snapshot squad_snapshot,
                                           Snapshot fighter_snapshot,
                                           std::string genome_dir,
                                           std::string variant_name)
    : squad_snapshot_(std::move(squad_snapshot))
    , fighter_snapshot_(std::move(fighter_snapshot))
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name)) {}

void SkirmishConfigScreen::on_draw(AppState& /*state*/, Renderer& /*renderer*/,
                                    UIManager& ui) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::Begin("##SkirmishConfig", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("Squad Skirmish \xe2\x80\x94 Configuration");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Tournament ---
    ui::section_header("Tournament");

    int pop = static_cast<int>(config_.population_size);
    if (ui::input_int("Population Size", &pop, 4, 200)) {
        config_.population_size = static_cast<std::size_t>(pop);
    }

    int seeds = static_cast<int>(config_.seeds_per_match);
    if (ui::input_int("Seeds per Match", &seeds, 1, 10)) {
        config_.seeds_per_match = static_cast<std::size_t>(seeds);
    }
    ImGui::Spacing();

    // --- Arena ---
    ui::section_header("Arena");

    ui::slider_float("World Width", &config_.world_width, 1000.0f, 10000.0f);
    ui::slider_float("World Height", &config_.world_height, 1000.0f, 10000.0f);

    int squads = static_cast<int>(config_.num_squads_per_team);
    if (ui::input_int("Squads per Team", &squads, 1, 4)) {
        config_.num_squads_per_team = static_cast<std::size_t>(squads);
    }

    int fighters = static_cast<int>(config_.fighters_per_squad);
    if (ui::input_int("Fighters per Squad", &fighters, 2, 20)) {
        config_.fighters_per_squad = static_cast<std::size_t>(fighters);
    }

    int towers = static_cast<int>(config_.tower_count);
    if (ui::input_int("Towers", &towers, 0, 200)) {
        config_.tower_count = static_cast<std::size_t>(towers);
    }

    int tokens = static_cast<int>(config_.token_count);
    if (ui::input_int("Tokens", &tokens, 0, 200)) {
        config_.token_count = static_cast<std::size_t>(tokens);
    }

    // Time limit: display in seconds, store as ticks
    int time_seconds = static_cast<int>(config_.time_limit_ticks / 60);
    if (ui::input_int("Time Limit (seconds)", &time_seconds, 10, 300)) {
        config_.time_limit_ticks = static_cast<uint32_t>(time_seconds) * 60;
    }
    ImGui::Spacing();

    // --- Bases ---
    ui::section_header("Bases");

    ui::slider_float("Base HP", &config_.base_hp, 100.0f, 10000.0f);
    ui::slider_float("Base Radius", &config_.base_radius, 20.0f, 300.0f);
    ui::slider_float("Base Bullet Damage", &config_.base_bullet_damage, 1.0f, 100.0f);
    ImGui::Spacing();

    // --- Physics ---
    ui::section_header("Physics");

    ui::slider_float("Rotation Speed", &config_.rotation_speed, 0.01f, 0.2f);
    ui::slider_float("Bullet Max Range", &config_.bullet_max_range, 200.0f, 3000.0f);
    ui::checkbox("Wrap N/S", &config_.wrap_ns);
    ui::checkbox("Wrap E/W", &config_.wrap_ew);
    ui::checkbox("Friendly Fire", &config_.friendly_fire);
    ImGui::Spacing();

    // --- Scoring ---
    ui::section_header("Scoring");

    ui::slider_float("Kill Points", &config_.kill_points, 10.0f, 1000.0f);
    ui::slider_float("Death Points", &config_.death_points, 0.0f, 500.0f);
    ui::slider_float("Base Hit Points", &config_.base_hit_points, 0.0f, 500.0f);
    ImGui::TextDisabled("Base Kill Points: %.0f", config_.base_kill_points());
    ImGui::Spacing();

    // --- Buttons ---
    ImGui::Separator();
    ImGui::Spacing();

    if (ui::button("Start", ui::ButtonStyle::Primary, 200.0f)) {
        ui.push_screen(std::make_unique<SkirmishScreen>(
            std::move(squad_snapshot_), std::move(fighter_snapshot_),
            std::move(genome_dir_), std::move(variant_name_), config_));
    }

    ImGui::SameLine();

    if (ui::button("Back", ui::ButtonStyle::Secondary, 120.0f)) {
        ui.pop_screen();
    }

    ImGui::End();

    // Escape pops back to the previous screen
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

} // namespace neuroflyer

#include <neuroflyer/ui/screens/arena_config_screen.h>
#include <neuroflyer/ui/screens/arena_game_screen.h>
#include <neuroflyer/ui/views/arena_config_view.h>
#include <neuroflyer/ui/ui_manager.h>

#include <imgui.h>

#include <memory>

namespace neuroflyer {

void ArenaConfigScreen::on_draw(AppState& /*state*/, Renderer& /*renderer*/,
                                 UIManager& ui) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::Begin("##ArenaConfig", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (draw_arena_config_view(config_)) {
        ui.push_screen(std::make_unique<ArenaGameScreen>(config_));
    }

    ImGui::End();

    // Escape pops back to the previous screen
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

} // namespace neuroflyer

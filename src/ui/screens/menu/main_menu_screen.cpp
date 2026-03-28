#include <neuroflyer/ui/screens/main_menu_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/ui/screens/fly_session_screen.h>
#include <neuroflyer/ui/screens/hangar_screen.h>
#include <neuroflyer/ui/screens/settings_screen.h>

#include <imgui.h>

namespace neuroflyer {

void MainMenuScreen::on_draw(AppState& state, Renderer& renderer,
                              UIManager& ui) {
    renderer.render_menu_background();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.3f);

    ImGui::Begin("##MainMenu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    // Title
    ImGui::Dummy(ImVec2(0, sh * 0.2f));

    {
        ImGui::PushFont(nullptr);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
        const char* title = "NEUROFLYER";
        float title_w = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((sw - title_w) * 0.5f);
        ImGui::Text("%s", title);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(0, 10));

    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
        const char* subtitle = "Neuroevolution Arcade";
        float sub_w = ImGui::CalcTextSize(subtitle).x;
        ImGui::SetCursorPosX((sw - sub_w) * 0.5f);
        ImGui::Text("%s", subtitle);
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, sh * 0.1f));

    // Menu buttons -- centered
    constexpr float BTN_W = 250.0f;
    constexpr float BTN_H = 50.0f;
    constexpr float BTN_GAP = 12.0f;
    float btn_x = (sw - BTN_W) * 0.5f;

    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Fly", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<FlySessionScreen>());
    }

    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Hangar", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<HangarScreen>());
    }

    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Settings", ImVec2(BTN_W, BTN_H))) {
        ui.push_screen(std::make_unique<SettingsScreen>());
    }

    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Quit", ImVec2(BTN_W, BTN_H))) {
        state.quit_requested = true;
    }

    ImGui::End();
}

} // namespace neuroflyer

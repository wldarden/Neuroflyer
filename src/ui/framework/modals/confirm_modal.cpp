#include <neuroflyer/ui/modals/confirm_modal.h>
#include <neuroflyer/ui/ui_manager.h>

#include <imgui.h>

#include <utility>

namespace neuroflyer {

ConfirmModal::ConfirmModal(std::string title, std::string message,
                           std::function<void()> on_confirm)
    : title_(std::move(title))
    , message_(std::move(message))
    , on_confirm_(std::move(on_confirm)) {}

void ConfirmModal::on_draw(AppState& /*state*/, UIManager& ui) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_Always);

    constexpr auto flags = ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin(("##" + title_).c_str(), nullptr, flags);

    // Title (orange/gold)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
    ImGui::TextUnformatted(title_.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    // Message
    ImGui::TextWrapped("%s", message_.c_str());
    ImGui::Dummy(ImVec2(0, 12));

    // Cancel button
    constexpr float BTN_W = 120.0f;
    constexpr float BTN_H = 32.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    bool cancel = ImGui::Button("Cancel", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();

    // Yes button (right-aligned)
    float window_w = ImGui::GetWindowSize().x;
    ImGui::SameLine(window_w - BTN_W - ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
    bool confirm = ImGui::Button("Yes", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancel = true;
    }

    ImGui::End();

    if (confirm) {
        on_confirm_();
        ui.pop_modal();
    } else if (cancel) {
        ui.pop_modal();
    }
}

} // namespace neuroflyer

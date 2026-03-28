#include <neuroflyer/ui/modals/input_modal.h>
#include <neuroflyer/ui/ui_manager.h>

#include <imgui.h>

#include <cstring>
#include <utility>

namespace neuroflyer {

InputModal::InputModal(std::string title, std::string prompt,
                       std::function<void(const std::string&)> on_submit,
                       std::string default_value)
    : title_(std::move(title))
    , prompt_(std::move(prompt))
    , on_submit_(std::move(on_submit))
    , default_value_(std::move(default_value)) {}

void InputModal::on_enter() {
    std::memset(buffer_, 0, sizeof(buffer_));
    if (!default_value_.empty()) {
        std::strncpy(buffer_, default_value_.c_str(), sizeof(buffer_) - 1);
    }
    focus_next_ = true;
}

void InputModal::on_draw(AppState& /*state*/, UIManager& ui) {
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

    // Prompt text
    if (!prompt_.empty()) {
        ImGui::TextWrapped("%s", prompt_.c_str());
        ImGui::Dummy(ImVec2(0, 4));
    }

    // Text input — auto-focus on first frame
    if (focus_next_) {
        ImGui::SetKeyboardFocusHere();
        focus_next_ = false;
    }

    ImGui::PushItemWidth(-1);
    bool enter = ImGui::InputText("##input", buffer_, sizeof(buffer_),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 8));

    // Cancel button
    constexpr float BTN_W = 120.0f;
    constexpr float BTN_H = 32.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    bool cancel = ImGui::Button("Cancel", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();

    // OK button (right-aligned)
    float window_w = ImGui::GetWindowSize().x;
    ImGui::SameLine(window_w - BTN_W - ImGui::GetStyle().WindowPadding.x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
    bool ok = ImGui::Button("OK", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancel = true;
    }

    ImGui::End();

    if (ok || enter) {
        on_submit_(std::string(buffer_));
        ui.pop_modal();
    } else if (cancel) {
        ui.pop_modal();
    }
}

} // namespace neuroflyer

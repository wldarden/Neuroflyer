#include <game-ui/components/modal.h>

#include <algorithm>

namespace gameui {

namespace {

// Shared modal drawing: centered on screen, dark overlay, consistent styling.
// Returns true if the modal is open (caller should draw content).
bool begin_modal(const char* id, const std::string& title, const ModalStyle& style) {
    ImGui::SetNextWindowSize(ImVec2(style.width, 0.0f), ImGuiCond_Always);

    // Center the modal
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(id, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        return false;
    }

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    return true;
}

// Draw cancel button. Returns true if clicked or Escape pressed.
bool draw_cancel(const ModalStyle& style) {
    ImGui::PushStyleColor(ImGuiCol_Button, style.cancel_color);
    bool clicked = ImGui::Button(style.cancel_label.c_str(), ImVec2(120, 32));
    ImGui::PopStyleColor();

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        clicked = true;
    }

    return clicked;
}

// Draw confirm button with appropriate color.
bool draw_confirm(const ModalStyle& style) {
    ImVec4 color = style.danger ? style.danger_color : style.confirm_color;
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    bool clicked = ImGui::Button(style.confirm_label.c_str(), ImVec2(120, 32));
    ImGui::PopStyleColor();
    return clicked;
}

} // namespace

void open_modal(const char* id) {
    ImGui::OpenPopup(id);
}

ModalResult draw_confirm_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    const ModalStyle& style) {

    if (!begin_modal(id, title, style)) {
        return ModalResult::Open;
    }

    ImGui::TextWrapped("%s", message.c_str());
    ImGui::Dummy(ImVec2(0, 12));

    // Buttons: Cancel on left, Confirm on right
    if (draw_cancel(style)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Cancel;
    }

    ImGui::SameLine(style.width - 120.0f - ImGui::GetStyle().WindowPadding.x);

    if (draw_confirm(style)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Confirm;
    }

    ImGui::EndPopup();
    return ModalResult::Open;
}

ModalResult draw_input_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    std::string& buffer,
    const ModalStyle& style) {

    if (!begin_modal(id, title, style)) {
        return ModalResult::Open;
    }

    if (!message.empty()) {
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::Dummy(ImVec2(0, 4));
    }

    // Text input — auto-focus on first appearance
    static bool s_focus_next = false;
    if (ImGui::IsWindowAppearing()) {
        s_focus_next = true;
    }
    if (s_focus_next) {
        ImGui::SetKeyboardFocusHere();
        s_focus_next = false;
    }

    // Resize buffer for InputText (needs mutable char*)
    buffer.resize(std::max(buffer.size(), static_cast<std::size_t>(256)));
    ImGui::PushItemWidth(-1);
    bool enter = ImGui::InputText("##input", buffer.data(), buffer.capacity(),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    // Trim to actual string length
    buffer.resize(std::strlen(buffer.c_str()));

    ImGui::Dummy(ImVec2(0, 8));

    // Cancel
    if (draw_cancel(style)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Cancel;
    }

    ImGui::SameLine(style.width - 120.0f - ImGui::GetStyle().WindowPadding.x);

    // Confirm (also on Enter key)
    if (draw_confirm(style) || enter) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Confirm;
    }

    // Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Cancel;
    }

    ImGui::EndPopup();
    return ModalResult::Open;
}

ModalResult draw_choice_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    const std::vector<std::string>& button_labels,
    const ModalStyle& style) {

    if (!begin_modal(id, title, style)) {
        return ModalResult::Open;
    }

    ImGui::TextWrapped("%s", message.c_str());
    ImGui::Dummy(ImVec2(0, 12));

    // Cancel on the left
    if (draw_cancel(style)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Cancel;
    }

    // Choice buttons on the right, right-to-left
    int num_buttons = std::min(static_cast<int>(button_labels.size()), 4);
    float btn_w = 120.0f;
    float gap = 8.0f;
    float right_edge = style.width - ImGui::GetStyle().WindowPadding.x;

    for (int i = num_buttons - 1; i >= 0; --i) {
        float x = right_edge - btn_w * static_cast<float>(num_buttons - i)
                  - gap * static_cast<float>(num_buttons - i - 1);
        ImGui::SameLine(x);

        ImGui::PushID(i);
        // First button gets confirm color, others get neutral
        if (i == 0) {
            ImVec4 color = style.danger ? style.danger_color : style.confirm_color;
            ImGui::PushStyleColor(ImGuiCol_Button, color);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));
        }

        if (ImGui::Button(button_labels[static_cast<std::size_t>(i)].c_str(),
                          ImVec2(btn_w, 32))) {
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();

            // Map button index to ModalResult::Choice0..Choice3
            constexpr ModalResult choices[] = {
                ModalResult::Choice0, ModalResult::Choice1,
                ModalResult::Choice2, ModalResult::Choice3,
            };
            return choices[i];
        }

        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return ModalResult::Cancel;
    }

    ImGui::EndPopup();
    return ModalResult::Open;
}

} // namespace gameui

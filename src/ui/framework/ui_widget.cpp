#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

#include <algorithm>
#include <string>

namespace neuroflyer::ui {

bool button(const char* label, ButtonStyle style, float width) {
    ImVec4 color;
    ImVec4 hovered;
    ImVec4 active;

    switch (style) {
    case ButtonStyle::Primary:
        color   = ImVec4(0.2f, 0.5f, 0.8f, 1.0f);
        hovered = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
        active  = ImVec4(0.15f, 0.4f, 0.7f, 1.0f);
        break;
    case ButtonStyle::Secondary:
        color   = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
        hovered = ImVec4(0.4f, 0.4f, 0.45f, 1.0f);
        active  = ImVec4(0.25f, 0.25f, 0.3f, 1.0f);
        break;
    case ButtonStyle::Danger:
        color   = ImVec4(0.7f, 0.15f, 0.15f, 1.0f);
        hovered = ImVec4(0.85f, 0.2f, 0.2f, 1.0f);
        active  = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);
        break;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);

    ImVec2 size = (width > 0.0f) ? ImVec2(width, 0) : ImVec2(0, 0);
    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

bool slider_float(const char* label, float* value, float min, float max) {
    ImGui::PushItemWidth(-1);
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    bool changed = ImGui::SliderFloat(
        (std::string("##") + label).c_str(), value, min, max);
    ImGui::PopItemWidth();
    return changed;
}

bool input_int(const char* label, int* value, int min, int max) {
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::InputInt(
        (std::string("##") + label).c_str(), value);
    ImGui::PopItemWidth();
    *value = std::clamp(*value, min, max);
    return changed;
}

bool input_text(const char* label, char* buf, std::size_t buf_size,
                const char* hint) {
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    ImGui::PushItemWidth(-1);
    bool changed;
    if (hint) {
        changed = ImGui::InputTextWithHint(
            (std::string("##") + label).c_str(), hint, buf, buf_size);
    } else {
        changed = ImGui::InputText(
            (std::string("##") + label).c_str(), buf, buf_size);
    }
    ImGui::PopItemWidth();
    return changed;
}

bool checkbox(const char* label, bool* value) {
    return ImGui::Checkbox(label, value);
}

void section_header(const char* label) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

} // namespace neuroflyer::ui

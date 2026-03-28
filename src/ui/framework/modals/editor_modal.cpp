#include <neuroflyer/ui/modals/editor_modal.h>
#include <neuroflyer/ui/ui_manager.h>

#include <imgui.h>

namespace neuroflyer {

void EditorModal::on_draw(AppState& state, UIManager& ui) {
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    bool open = true;
    ImGui::Begin(title_.c_str(), &open,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    draw_form(state, ui);

    ImGui::Separator();
    ImGui::Spacing();

    // Ok/Cancel buttons — single exit point to avoid double-pop
    bool should_close = false;
    if (ImGui::Button("Ok", ImVec2(120, 0))) {
        on_ok(ui);
        should_close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        on_cancel(ui);
        should_close = true;
    }

    ImGui::End();

    // Close on X button or Escape
    if (!should_close && (!open || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        on_cancel(ui);
        should_close = true;
    }

    if (should_close) {
        ui.pop_modal();
    }
}

} // namespace neuroflyer

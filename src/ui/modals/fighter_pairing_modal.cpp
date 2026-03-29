#include <neuroflyer/ui/modals/fighter_pairing_modal.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/paths.h>

#include <imgui.h>

#include <utility>

namespace neuroflyer {

FighterPairingModal::FighterPairingModal(
    std::vector<SnapshotHeader> fighters,
    std::function<void(const std::string&)> on_select)
    : fighters_(std::move(fighters))
    , on_select_(std::move(on_select)) {}

void FighterPairingModal::on_draw(AppState& /*state*/, UIManager& ui) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_Always);

    constexpr auto flags = ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##FighterPairing", nullptr, flags);

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
    ImGui::TextUnformatted("Select Fighter Variant");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));

    if (fighters_.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "(no fighter variants found)");
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Choose a fighter to pair with the squad net:");
        ImGui::Dummy(ImVec2(0, 4));

        // Fighter table
        float table_h = 350.0f - 130.0f; // leave room for buttons
        if (ImGui::BeginTable("##FighterTable", 3,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                ImVec2(0, table_h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name",
                ImGuiTableColumnFlags_None, 0.45f);
            ImGui::TableSetupColumn("Gen",
                ImGuiTableColumnFlags_None, 0.15f);
            ImGui::TableSetupColumn("Created",
                ImGuiTableColumnFlags_None, 0.40f);
            ImGui::TableHeadersRow();

            for (int i = 0;
                 i < static_cast<int>(fighters_.size()); ++i) {
                const auto& f =
                    fighters_[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                bool is_selected = (selected_idx_ == i);
                if (ImGui::Selectable(f.name.c_str(), is_selected,
                        ImGuiSelectableFlags_SpanAllColumns |
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_idx_ = i;
                    // Double-click selects and closes
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        on_select_(f.name);
                        ui.pop_modal();
                        ImGui::EndTable();
                        ImGui::End();
                        return;
                    }
                }

                ImGui::TableNextColumn();
                ImGui::Text("%u", f.generation);

                ImGui::TableNextColumn();
                if (f.created_timestamp > 0) {
                    ImGui::TextColored(
                        ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                        format_short_date(f.created_timestamp).c_str());
                } else {
                    ImGui::TextColored(
                        ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "---");
                }
            }

            ImGui::EndTable();
        }
    }

    ImGui::Dummy(ImVec2(0, 4));

    // Buttons
    constexpr float BTN_W = 120.0f;
    constexpr float BTN_H = 32.0f;

    // Cancel button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    bool cancel = ImGui::Button("Cancel", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();

    // Select button (right-aligned)
    float window_w = ImGui::GetWindowSize().x;
    ImGui::SameLine(window_w - BTN_W - ImGui::GetStyle().WindowPadding.x);
    bool can_select = !fighters_.empty() && selected_idx_ >= 0 &&
        static_cast<std::size_t>(selected_idx_) < fighters_.size();
    if (!can_select) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
    bool select = ImGui::Button("Select", ImVec2(BTN_W, BTN_H));
    ImGui::PopStyleColor();
    if (!can_select) ImGui::EndDisabled();

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancel = true;
    }

    ImGui::End();

    if (select && can_select) {
        on_select_(fighters_[static_cast<std::size_t>(selected_idx_)].name);
        ui.pop_modal();
    } else if (cancel) {
        ui.pop_modal();
    }
}

} // namespace neuroflyer

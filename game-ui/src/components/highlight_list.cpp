#include <game-ui/components/highlight_list.h>

#include <algorithm>
#include <cstdio>

namespace gameui {

HighlightListResult draw_highlight_list(
    const char* id,
    const std::vector<HighlightListRow>& rows,
    int selected,
    const HighlightListConfig& config) {

    HighlightListResult result;

    float w = config.width > 0.0f ? config.width : ImGui::GetContentRegionAvail().x;
    float h = config.height > 0.0f ? config.height : ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild(id, ImVec2(w, h), true);

    // Optional "Create" button at the top
    if (config.show_create) {
        if (ImGui::Button(config.create_label.c_str(), ImVec2(-1, 0))) {
            result.create_clicked = true;
        }
        ImGui::Separator();
    }

    // Scrollable row area
    ImGui::BeginChild("##rows", ImVec2(0, 0), false);

    float row_w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        bool is_selected = (selected == i);

        ImGui::PushID(i);

        // Draw the selectable (handles click, hover, keyboard nav)
        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, config.highlight_color);
        }

        bool clicked = ImGui::Selectable("##row", is_selected,
            ImGuiSelectableFlags_AllowDoubleClick, ImVec2(row_w, config.row_height));

        if (is_selected) {
            ImGui::PopStyleColor();
        }

        if (clicked) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                result.double_clicked = i;
            }
            result.clicked = i;
        }

        if (ImGui::IsItemHovered()) {
            result.hovered = i;
        }

        // Delete button (overlaid on the right side of the row)
        if (config.show_delete) {
            float del_w = 20.0f;
            float del_x = ImGui::GetItemRectMax().x - del_w - 8.0f;
            float del_y = ImGui::GetItemRectMin().y + (config.row_height - 16.0f) / 2.0f;

            ImGui::SetCursorScreenPos(ImVec2(del_x, del_y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));

            char del_id[32];
            std::snprintf(del_id, sizeof(del_id), "X##del_%d", i);
            if (ImGui::SmallButton(del_id)) {
                result.delete_clicked = i;
            }

            ImGui::PopStyleColor(3);
        }

        // Draw text via DrawList (no cursor advancement, no overlap)
        float item_x = ImGui::GetItemRectMin().x;
        float item_y = ImGui::GetItemRectMin().y;

        // Title
        dl->AddText(ImVec2(item_x + 10.0f, item_y + 6.0f),
                    config.title_color, row.title.c_str());

        // Subtitles
        float sub_y = item_y + 24.0f;
        for (const auto& sub : row.subtitles) {
            dl->AddText(ImVec2(item_x + 10.0f, sub_y),
                        config.subtitle_color, sub.c_str());
            sub_y += 16.0f;
        }

        ImGui::PopID();
    }

    ImGui::EndChild();  // ##rows
    ImGui::EndChild();  // id

    return result;
}

} // namespace gameui

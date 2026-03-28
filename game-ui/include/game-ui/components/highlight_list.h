#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace gameui {

struct HighlightListRow {
    std::string title;
    std::vector<std::string> subtitles;  // rendered below title in dim text
};

struct HighlightListConfig {
    float row_height = 55.0f;
    float width = -1.0f;               // -1 = fill available
    float height = -1.0f;              // -1 = fill available
    ImU32 highlight_color = IM_COL32(50, 100, 130, 150);
    ImU32 title_color = IM_COL32(255, 255, 255, 255);
    ImU32 subtitle_color = IM_COL32(165, 165, 190, 255);
    bool show_create = false;
    std::string create_label = "+ New";
    bool show_delete = false;
};

struct HighlightListResult {
    int clicked = -1;           // row index single-clicked, -1 if none
    int double_clicked = -1;    // row index double-clicked, -1 if none
    int hovered = -1;           // row index hovered, -1 if none
    bool create_clicked = false;
    int delete_clicked = -1;    // row index where delete was clicked, -1 if none
};

/// Draw a scrollable, selectable list with highlighted rows.
/// `id` must be unique within the current ImGui window (used for BeginChild).
/// `selected` is the currently selected index (-1 for none).
[[nodiscard]] HighlightListResult draw_highlight_list(
    const char* id,
    const std::vector<HighlightListRow>& rows,
    int selected,
    const HighlightListConfig& config = {});

} // namespace gameui

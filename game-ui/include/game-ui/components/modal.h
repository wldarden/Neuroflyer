#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace gameui {

enum class ModalResult {
    Open,       // modal is still showing, no action yet
    Confirm,    // user clicked the confirm/OK button
    Cancel,     // user clicked cancel or pressed Escape
    Choice0,    // first custom button (choice modal)
    Choice1,    // second custom button
    Choice2,    // third custom button
    Choice3,    // fourth custom button
};

struct ModalStyle {
    float width = 400.0f;
    float min_height = 0.0f;         // 0 = auto-size
    ImVec4 confirm_color = ImVec4(0.2f, 0.5f, 0.3f, 1.0f);
    ImVec4 cancel_color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    ImVec4 danger_color = ImVec4(0.5f, 0.2f, 0.2f, 1.0f);
    std::string confirm_label = "OK";
    std::string cancel_label = "Cancel";
    bool danger = false;             // if true, confirm button uses danger_color
};

/// Open a modal by ID. Call this once when the trigger event happens.
/// Must be called from the same ImGui window context where draw_* will run.
void open_modal(const char* id);

/// Draw a confirmation modal (title + message + Confirm/Cancel).
/// Returns ModalResult::Open while showing, Confirm or Cancel when done.
[[nodiscard]] ModalResult draw_confirm_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    const ModalStyle& style = {});

/// Draw an input modal (title + message + text field + OK/Cancel).
/// `buffer` is read/written — caller provides initial value and reads result.
/// Returns ModalResult::Open while showing, Confirm or Cancel when done.
[[nodiscard]] ModalResult draw_input_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    std::string& buffer,
    const ModalStyle& style = {});

/// Draw a choice modal (title + message + N custom buttons).
/// Returns ModalResult::Open while showing, or Choice0..Choice3 / Cancel.
[[nodiscard]] ModalResult draw_choice_modal(
    const char* id,
    const std::string& title,
    const std::string& message,
    const std::vector<std::string>& button_labels,
    const ModalStyle& style = {});

} // namespace gameui

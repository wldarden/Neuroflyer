#include <neuroflyer/ui/screens/settings_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <cstdio>

namespace neuroflyer {

namespace {

struct Resolution {
    int w, h;
    const char* label;
};

constexpr Resolution RESOLUTIONS[] = {
    {800,  600,  "800x600"},
    {1024, 768,  "1024x768"},
    {1280, 720,  "1280x720 (HD)"},
    {1280, 800,  "1280x800"},
    {1440, 900,  "1440x900"},
    {1600, 900,  "1600x900"},
    {1920, 1080, "1920x1080 (FHD)"},
    {2560, 1440, "2560x1440 (QHD)"},
};
constexpr int NUM_RESOLUTIONS = sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]);

int find_resolution_index(int w, int h) {
    for (int i = 0; i < NUM_RESOLUTIONS; ++i) {
        if (RESOLUTIONS[i].w == w && RESOLUTIONS[i].h == h) return i;
    }
    return -1;
}

} // namespace

void SettingsScreen::on_enter() {
    backup_saved_ = false;
}

void SettingsScreen::on_draw(AppState& state, Renderer& /*renderer*/,
                              UIManager& ui) {
    // Backup config on first draw for cancel
    if (!backup_saved_) {
        config_backup_ = state.config;
        resolution_idx_ = find_resolution_index(
            state.config.window_width, state.config.window_height);
        if (resolution_idx_ < 0) resolution_idx_ = 3; // default to 1280x800
        fullscreen_ = state.config.fullscreen;
        vsync_ = state.config.vsync;
        max_fps_ = state.config.max_fps;
        backup_saved_ = true;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::Begin("##Settings", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Title
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.3f, 1.0f));
    ImGui::SetWindowFontScale(1.4f);
    const char* title = "Settings";
    float title_w = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((sw - title_w) * 0.5f);
    ImGui::Text("%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    // Content area
    float footer_h = 50.0f;
    float content_h = sh - ImGui::GetCursorPosY() - footer_h
                     - ImGui::GetStyle().WindowPadding.y;

    ImGui::BeginChild("##SettingsContent", ImVec2(0, content_h), false);

    // Center the settings controls
    float panel_w = std::min(500.0f, sw * 0.8f);
    float indent = (sw - panel_w) * 0.5f;
    ImGui::Indent(indent);
    ImGui::PushItemWidth(panel_w * 0.55f);

    // ==================== Graphics ====================
    ui::section_header("Graphics");
    ImGui::Dummy(ImVec2(0, 5));

    // Resolution dropdown
    {
        const char* current_label = (resolution_idx_ >= 0 && resolution_idx_ < NUM_RESOLUTIONS)
            ? RESOLUTIONS[resolution_idx_].label : "Custom";

        if (ImGui::BeginCombo("Resolution", current_label)) {
            for (int i = 0; i < NUM_RESOLUTIONS; ++i) {
                bool selected = (i == resolution_idx_);
                if (ImGui::Selectable(RESOLUTIONS[i].label, selected)) {
                    resolution_idx_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Dummy(ImVec2(0, 3));
    ui::checkbox("Fullscreen", &fullscreen_);

    ImGui::Dummy(ImVec2(0, 3));
    if (ui::checkbox("VSync", &vsync_)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "(restart to apply)");
    }

    ImGui::Dummy(ImVec2(0, 3));
    ImGui::SliderInt("Max FPS", &max_fps_, 15, 240);
    max_fps_ = std::clamp(max_fps_, 15, 240);

    ImGui::PopItemWidth();
    ImGui::Unindent(indent);

    ImGui::EndChild();

    // ==================== Footer ====================
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    float pad = ImGui::GetStyle().WindowPadding.x;
    float btn_w = 150.0f;

    // Helper: apply current settings to config + window
    auto apply_settings = [&]() {
        if (resolution_idx_ >= 0 && resolution_idx_ < NUM_RESOLUTIONS) {
            state.config.window_width = RESOLUTIONS[resolution_idx_].w;
            state.config.window_height = RESOLUTIONS[resolution_idx_].h;
        }
        state.config.fullscreen = fullscreen_;
        state.config.vsync = vsync_;
        state.config.max_fps = max_fps_;

        state.config.save(state.settings_path);

        SDL_Window* win = ui.window();
        if (win) {
            if (fullscreen_) {
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            } else {
                SDL_SetWindowFullscreen(win, 0);
                SDL_SetWindowSize(win, state.config.window_width,
                                  state.config.window_height);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED);
            }
        }
    };

    // Cancel — left
    ImGui::SetCursorPosX(pad);
    auto revert_settings = [&]() {
        state.config = config_backup_;
        SDL_Window* win = ui.window();
        if (win) {
            if (config_backup_.fullscreen) {
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            } else {
                SDL_SetWindowFullscreen(win, 0);
                SDL_SetWindowSize(win, config_backup_.window_width,
                                  config_backup_.window_height);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED);
            }
        }
    };

    if (ui::button("Cancel", ui::ButtonStyle::Secondary, btn_w)) {
        revert_settings();
        ui.pop_screen();
    }

    // Apply — center
    ImGui::SameLine((sw - btn_w) * 0.5f);
    if (ui::button("Apply", ui::ButtonStyle::Secondary, btn_w)) {
        apply_settings();
    }

    // Save & Back — right
    ImGui::SameLine(sw - btn_w - pad * 2.0f);
    if (ui::button("Save & Back", ui::ButtonStyle::Primary, btn_w)) {
        apply_settings();
        ui.pop_screen();
    }

    // Escape = cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        revert_settings();
        ui.pop_screen();
    }

    ImGui::End();
}

} // namespace neuroflyer

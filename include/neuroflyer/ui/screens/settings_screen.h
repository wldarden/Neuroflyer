#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/config.h>

namespace neuroflyer {

class SettingsScreen : public UIScreen {
public:
    void on_enter() override;
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "Settings"; }

private:
    GameConfig config_backup_;
    bool backup_saved_ = false;

    // Graphics state (editable, applied on save)
    int resolution_idx_ = 0;
    bool fullscreen_ = false;
    bool vsync_ = true;
    int max_fps_ = 60;
};

} // namespace neuroflyer

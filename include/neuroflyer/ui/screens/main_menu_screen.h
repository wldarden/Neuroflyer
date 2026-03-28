#pragma once

#include <neuroflyer/ui/ui_screen.h>

namespace neuroflyer {

class MainMenuScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer,
                 UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "MainMenu"; }
};

} // namespace neuroflyer

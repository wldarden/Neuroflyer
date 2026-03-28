#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/ui/ui_screen.h>

namespace neuroflyer {

class ArenaConfigScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "ArenaConfig"; }

private:
    ArenaConfig config_;
};

} // namespace neuroflyer

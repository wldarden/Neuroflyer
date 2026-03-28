#pragma once

#include <neuroflyer/ui/ui_screen.h>

#include <functional>
#include <string>

namespace neuroflyer {

/// Adapter that wraps a legacy draw_*() free function as a UIScreen.
/// Used during incremental migration — legacy screens work alongside
/// new UIScreen subclasses without changes.
class LegacyScreen : public UIScreen {
public:
    using DrawFn = std::function<void(AppState&, Renderer&)>;

    LegacyScreen(std::string screen_name, DrawFn draw_fn)
        : name_(std::move(screen_name)), draw_fn_(std::move(draw_fn)) {}

    void on_draw(AppState& state, Renderer& renderer,
                 UIManager& /*ui*/) override {
        draw_fn_(state, renderer);
    }

    [[nodiscard]] const char* name() const override {
        return name_.c_str();
    }

private:
    std::string name_;
    DrawFn draw_fn_;
};

} // namespace neuroflyer

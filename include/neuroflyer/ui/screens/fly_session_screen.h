#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/views/net_viewer_view.h>

namespace neuroflyer {

/// UIScreen subclass wrapping the fly-session game loop.
///
/// All persistent state lives in the global FlySessionState singleton
/// (accessed via get_fly_session_state()). This screen merely drives the
/// init / tick / render / input cycle through on_draw().
class FlySessionScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;
    void post_render(SDL_Renderer* sdl_renderer) override;
    [[nodiscard]] const char* name() const override { return "Flying"; }

private:
    NetViewerViewState fly_net_state_;
};

} // namespace neuroflyer

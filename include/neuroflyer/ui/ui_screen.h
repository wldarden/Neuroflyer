#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

struct SDL_Renderer;

namespace neuroflyer {

class UIManager;

class UIScreen {
public:
    virtual ~UIScreen() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) { (void)w; (void)h; }
    virtual void on_draw(AppState& state, Renderer& renderer, UIManager& ui) = 0;

    /// Called after ImGui renders, before SDL_RenderPresent.
    /// Override this to do deferred SDL rendering (e.g., neural net views).
    virtual void post_render(SDL_Renderer* /*sdl_renderer*/) {}

    [[nodiscard]] virtual const char* name() const = 0;
};

} // namespace neuroflyer

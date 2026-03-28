#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/ui_modal.h>
#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

#include <memory>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace neuroflyer {

class UIManager {
public:
    UIManager(SDL_Window* window, SDL_Renderer* sdl_renderer,
              int width, int height);

    // Screen stack
    void push_screen(std::unique_ptr<UIScreen> screen);
    void pop_screen();
    void replace_screen(std::unique_ptr<UIScreen> screen);
    [[nodiscard]] UIScreen* active_screen() const;
    [[nodiscard]] bool has_screens() const;

    // Modal stack
    void push_modal(std::unique_ptr<UIModal> modal);
    void pop_modal();
    [[nodiscard]] bool has_modal() const;

    /// True when a blocking modal is open — screens should skip input processing.
    [[nodiscard]] bool input_blocked() const { return input_blocked_; }

    // Resolution / window
    void set_resolution(int width, int height);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] SDL_Window* window() const { return window_; }
    [[nodiscard]] SDL_Renderer* sdl_renderer() const { return sdl_renderer_; }

    // Main loop — draws active screen + modal stack
    void draw(AppState& state, Renderer& renderer);

    /// Called after ImGui renders. Delegates to active screen's post_render().
    void post_render();

    /// Bridge for legacy go_to_screen() calls during migration.
    void sync_legacy_navigation(AppState& state, Renderer& renderer);

private:
    std::vector<std::unique_ptr<UIScreen>> screen_stack_;
    std::vector<std::unique_ptr<UIModal>> modal_stack_;
    int width_;
    int height_;
    [[maybe_unused]] SDL_Window* window_;
    [[maybe_unused]] SDL_Renderer* sdl_renderer_;
    Screen last_known_screen_ = Screen::MainMenu;
    bool input_blocked_ = false;
};

} // namespace neuroflyer

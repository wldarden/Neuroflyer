#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/screens/hangar_screen.h>
#include <neuroflyer/ui/screens/create_genome_screen.h>
#include <neuroflyer/ui/screens/variant_viewer_screen.h>
#include <neuroflyer/ui/screens/lineage_tree_screen.h>
#include <neuroflyer/ui/screens/fly_session_screen.h>
#include <neuroflyer/ui/screens/pause_config_screen.h>

namespace neuroflyer {

UIManager::UIManager(SDL_Window* window, SDL_Renderer* sdl_renderer,
                     int width, int height)
    : width_(width), height_(height), window_(window),
      sdl_renderer_(sdl_renderer) {}

// --- Screen stack ---

void UIManager::push_screen(std::unique_ptr<UIScreen> screen) {
    if (!screen_stack_.empty()) {
        screen_stack_.back()->on_exit();
    }
    screen->on_resize(width_, height_);
    screen->on_enter();
    screen_stack_.push_back(std::move(screen));
}

void UIManager::pop_screen() {
    if (screen_stack_.size() <= 1) return;
    // Clear any modals owned by the departing screen
    while (!modal_stack_.empty()) {
        modal_stack_.back()->on_exit();
        modal_stack_.pop_back();
    }
    screen_stack_.back()->on_exit();
    screen_stack_.pop_back();
    screen_stack_.back()->on_enter();
}

// Currently unused — available for future use
void UIManager::replace_screen(std::unique_ptr<UIScreen> screen) {
    for (auto it = screen_stack_.rbegin(); it != screen_stack_.rend(); ++it) {
        (*it)->on_exit();
    }
    screen_stack_.clear();
    screen->on_resize(width_, height_);
    screen->on_enter();
    screen_stack_.push_back(std::move(screen));
}

UIScreen* UIManager::active_screen() const {
    if (screen_stack_.empty()) return nullptr;
    return screen_stack_.back().get();
}

bool UIManager::has_screens() const {
    return !screen_stack_.empty();
}

// --- Modal stack ---

void UIManager::push_modal(std::unique_ptr<UIModal> modal) {
    modal->on_enter();
    modal_stack_.push_back(std::move(modal));
}

void UIManager::pop_modal() {
    if (modal_stack_.empty()) return;
    modal_stack_.back()->on_exit();
    modal_stack_.pop_back();
}

bool UIManager::has_modal() const {
    return !modal_stack_.empty();
}

// --- Resolution ---

void UIManager::set_resolution(int width, int height) {
    width_ = width;
    height_ = height;
    for (auto& screen : screen_stack_) {
        screen->on_resize(width, height);
    }
}

// --- Draw ---

void UIManager::draw(AppState& state, Renderer& renderer) {
    if (!screen_stack_.empty()) {
        // If a blocking modal is open, still draw the screen visually
        // but tell it to skip input processing
        input_blocked_ = false;
        for (const auto& modal : modal_stack_) {
            if (modal->blocks_input()) {
                input_blocked_ = true;
                break;
            }
        }
        screen_stack_.back()->on_draw(state, renderer, *this);
        input_blocked_ = false;
    }
    for (auto& modal : modal_stack_) {
        modal->on_draw(state, *this);
    }
}

void UIManager::post_render() {
    if (!screen_stack_.empty()) {
        screen_stack_.back()->post_render(sdl_renderer_);
    }
}

void UIManager::sync_legacy_navigation(AppState& state, Renderer& /*renderer*/) {
    if (state.current == last_known_screen_) return;

    last_known_screen_ = state.current;

    // If a legacy screen navigated "back" to a screen that's already on
    // the stack below us, pop down to it instead of pushing a new one.
    // This preserves state for migrated screens (like HangarScreen).
    if (screen_stack_.size() > 1) {
        // Check if any screen below the top matches where we're going
        // For MainMenu: pop everything. For Hangar: pop to HangarScreen.
        using S = Screen;
        if (state.current == S::MainMenu) {
            while (screen_stack_.size() > 1) {
                screen_stack_.back()->on_exit();
                screen_stack_.pop_back();
            }
            screen_stack_.back()->on_enter();
            return;
        }
        if (state.current == S::Hangar && screen_stack_.size() > 1) {
            // Pop the top screen to return to HangarScreen below
            pop_screen();
            return;
        }
    }

    // Forward navigation — push a legacy screen on top of the current stack
    using S = Screen;
    std::unique_ptr<UIScreen> next;

    switch (state.current) {
    case S::MainMenu:
        return;  // handled above
    case S::Hangar:
        next = std::make_unique<HangarScreen>();
        break;
    case S::CreateGenome:
        next = std::make_unique<CreateGenomeScreen>();
        break;
    case S::VariantViewer:
        next = std::make_unique<VariantViewerScreen>();
        break;
    case S::LineageTree:
        next = std::make_unique<LineageTreeScreen>();
        break;
    case S::Flying:
        next = std::make_unique<FlySessionScreen>();
        break;
    case S::PauseConfig:
        next = std::make_unique<PauseConfigScreen>();
        break;
    }

    if (next) {
        push_screen(std::move(next));
    }
}

} // namespace neuroflyer

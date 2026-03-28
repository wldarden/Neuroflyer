#include <neuroflyer/renderer.h>

#include <neuralnet-ui/render_net_topology.h>

#include <SDL.h>
#include <SDL_image.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace neuroflyer {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Renderer::Renderer(SDL_Renderer* sdl_renderer, SDL_Window* sdl_window,
                   [[maybe_unused]] int screen_w,
                   [[maybe_unused]] int screen_h)
    : game_view(sdl_renderer),
      renderer_(sdl_renderer), window_(sdl_window) {
}

Renderer::~Renderer() {
    if (menu_bg_tex_) SDL_DestroyTexture(menu_bg_tex_);
    if (tb_bg_tex_) SDL_DestroyTexture(tb_bg_tex_);
    // game_view destructor handles all game assets
}

// ---------------------------------------------------------------------------
// Live-query dimension helpers
// ---------------------------------------------------------------------------

int Renderer::screen_w() const {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    (void)h;
    return w;
}

int Renderer::screen_h() const {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    (void)w;
    return h;
}

int Renderer::game_w() const {
    return static_cast<int>(screen_w() * 0.6f);
}

int Renderer::net_w() const {
    return screen_w() - game_w();
}

// ---------------------------------------------------------------------------
// Fly mode: main frame render
// ---------------------------------------------------------------------------

void Renderer::render(const std::vector<GameSession>& sessions,
                       std::size_t focused_idx,
                       [[maybe_unused]] const Individual& focused_individual,
                       [[maybe_unused]] const neuralnet::Network& focused_net,
                       const std::vector<RayEndpoint>& focused_rays,
                       const RenderState& state,
                       const ShipDesign& ship_design,
                       [[maybe_unused]] int mouse_x,
                       [[maybe_unused]] int mouse_y,
                       [[maybe_unused]] const std::vector<float>& focused_input) {
    // Game panel (left side) -- delegate to GameView
    game_view.render(sessions, focused_idx, focused_rays, state, ship_design);

    // Divider line
    SDL_SetRenderDrawColor(renderer_, 60, 60, 60, 255);
    SDL_RenderDrawLine(renderer_, game_w(), 0, game_w(), screen_h());

    // Window title HUD
    auto title = "NeuroFlyer | Gen " + std::to_string(state.generation)
               + " | Alive: " + std::to_string(state.alive_count) + "/" + std::to_string(state.total_count)
               + " | Best: " + std::to_string(static_cast<int>(state.best_score));
    SDL_SetWindowTitle(window_, title.c_str());

    // Note: net panel rendering moved to FlySessionScreen via NetViewerView.
    // SDL_RenderClear/Present handled by main loop.
}

// ---------------------------------------------------------------------------
// Menu background
// ---------------------------------------------------------------------------

void Renderer::load_menu_background(const std::string& png_path) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image init failed: " << IMG_GetError() << "\n";
        return;
    }

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load menu background: " << IMG_GetError() << "\n";
        return;
    }

    menu_bg_w_ = surface->w;
    menu_bg_h_ = surface->h;
    menu_bg_tex_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);

    if (!menu_bg_tex_) {
        std::cerr << "Failed to create menu bg texture: " << SDL_GetError() << "\n";
        return;
    }

    std::cout << "Loaded menu background: " << menu_bg_w_ << "x" << menu_bg_h_ << "\n";
}

void Renderer::render_menu_background() {
    if (!menu_bg_tex_) return;

    // Get actual current window size (handles resize/fullscreen)
    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(renderer_, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    // Cover-fill: scale image to fill screen, cropping edges to preserve aspect ratio
    float img_aspect = static_cast<float>(menu_bg_w_) / static_cast<float>(menu_bg_h_);
    float scr_aspect = static_cast<float>(win_w) / static_cast<float>(win_h);

    SDL_Rect dst;
    if (scr_aspect > img_aspect) {
        // Screen is wider than image -- fit to width, crop top/bottom
        dst.w = win_w;
        dst.h = static_cast<int>(static_cast<float>(win_w) / img_aspect);
        dst.x = 0;
        dst.y = (win_h - dst.h) / 2;
    } else {
        // Screen is taller than image -- fit to height, crop left/right
        dst.h = win_h;
        dst.w = static_cast<int>(static_cast<float>(win_h) * img_aspect);
        dst.x = (win_w - dst.w) / 2;
        dst.y = 0;
    }

    SDL_RenderCopy(renderer_, menu_bg_tex_, nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Test bench background
// ---------------------------------------------------------------------------

void Renderer::load_test_bench_background(const std::string& png_path) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image init failed: " << IMG_GetError() << "\n";
        return;
    }

    SDL_Surface* surface = IMG_Load(png_path.c_str());
    if (!surface) {
        std::cerr << "Failed to load test bench background: " << IMG_GetError() << "\n";
        return;
    }

    tb_bg_w_ = surface->w;
    tb_bg_h_ = surface->h;
    tb_bg_tex_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);

    if (!tb_bg_tex_) {
        std::cerr << "Failed to create test bench bg texture: " << SDL_GetError() << "\n";
        return;
    }

    std::cout << "Loaded test bench background: " << tb_bg_w_ << "x" << tb_bg_h_ << "\n";
}

void Renderer::render_test_bench_background(int x, int y, int w, int h) {
    if (!tb_bg_tex_ || w <= 0 || h <= 0) return;

    // Cover-fill within the given rect, preserving aspect ratio
    float img_aspect = static_cast<float>(tb_bg_w_) / static_cast<float>(tb_bg_h_);
    float rect_aspect = static_cast<float>(w) / static_cast<float>(h);

    SDL_Rect dst;
    if (rect_aspect > img_aspect) {
        dst.w = w;
        dst.h = static_cast<int>(static_cast<float>(w) / img_aspect);
        dst.x = x;
        dst.y = y + (h - dst.h) / 2;
    } else {
        dst.h = h;
        dst.w = static_cast<int>(static_cast<float>(h) * img_aspect);
        dst.x = x + (w - dst.w) / 2;
        dst.y = y;
    }

    SDL_RenderCopy(renderer_, tb_bg_tex_, nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Deferred rendering (drawn after ImGui, so SDL content appears on top)
// ---------------------------------------------------------------------------

void Renderer::defer_topology(neuralnet_ui::TopologyRenderConfig config) {
    deferred_previews_.push_back({std::move(config)});
}

void Renderer::flush_deferred() {
    // Topology previews (used by hangar/create genome for small structure views)
    for (auto& dp : deferred_previews_) {
        neuralnet_ui::render_net_topology(dp.config);
    }
    deferred_previews_.clear();

    // Note: full neural net panel rendering is now handled by
    // NetViewerView::flush_net_viewer_view(), called via screen post_render().
}

} // namespace neuroflyer

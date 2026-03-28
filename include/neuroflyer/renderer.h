#pragma once

#include <neuroflyer/ui/views/game_view.h>

#include <neuralnet-ui/render_net_topology.h>

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace neuroflyer {

class Renderer {
public:
    Renderer(SDL_Renderer* sdl_renderer, SDL_Window* sdl_window,
             int screen_w, int screen_h);
    ~Renderer();

    /// Render one frame: game panel (left) + divider + window title HUD.
    void render(const std::vector<GameSession>& sessions,
                std::size_t focused_idx,
                const Individual& focused_individual,
                const neuralnet::Network& focused_net,
                const std::vector<RayEndpoint>& focused_rays,
                const RenderState& state,
                const ShipDesign& ship_design,
                int mouse_x = 0, int mouse_y = 0,
                const std::vector<float>& focused_input = {});

    /// Queue a topology preview to be drawn after ImGui.
    void defer_topology(neuralnet_ui::TopologyRenderConfig config);

    /// Execute all deferred topology preview draws. Call after ImGui::Render().
    void flush_deferred();

    /// Load background image for main menu.
    void load_menu_background(const std::string& png_path);

    /// Render the menu background (cover-fill, preserving aspect ratio).
    void render_menu_background();

    /// Load background image for test bench.
    void load_test_bench_background(const std::string& png_path);

    /// Render the test bench background into a given rect (cover-fill).
    void render_test_bench_background(int x, int y, int w, int h);

    // --- Game view (owns all game-panel rendering) -------------------------
    GameView game_view;

    // --- Layout & SDL handles (still used by other screens) ----------------
    SDL_Renderer* renderer_;
    SDL_Window* window_;

    [[nodiscard]] int screen_w() const;
    [[nodiscard]] int screen_h() const;
    [[nodiscard]] int game_w() const;
    [[nodiscard]] int net_w() const;

private:
    // Menu background
    SDL_Texture* menu_bg_tex_ = nullptr;
    int menu_bg_w_ = 0;
    int menu_bg_h_ = 0;

    // Test bench background
    SDL_Texture* tb_bg_tex_ = nullptr;
    int tb_bg_w_ = 0;
    int tb_bg_h_ = 0;

    // Deferred topology preview queue (rendered after ImGui)
    struct DeferredTopologyRender {
        neuralnet_ui::TopologyRenderConfig config;
    };
    std::vector<DeferredTopologyRender> deferred_previews_;
};

} // namespace neuroflyer

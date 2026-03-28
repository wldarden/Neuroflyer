#include <neuroflyer/app_state.h>
#include <neuroflyer/constants.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/config.h>
#include <neuroflyer/genome_manager.h>

#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/screens/main_menu_screen.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>

int main() {
    using neuroflyer::SCREEN_W;
    using neuroflyer::SCREEN_H;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    auto* window = SDL_CreateWindow("NeuroFlyer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    neuroflyer::Renderer renderer(sdl_renderer, window, SCREEN_W, SCREEN_H);

    // Load assets
    {
        auto assets = neuroflyer::asset_dir();
        renderer.game_view.load_asteroid_texture(assets + "/asteroid-1.png");
        renderer.game_view.load_coin_strip(assets + "/coin-strip.png", 16);
        renderer.game_view.load_star_atlas(assets + "/star-atlas.png", 42);
        renderer.load_menu_background(assets + "/pixel-hangar.png");
        renderer.load_test_bench_background(assets + "/large-mechanic-hangar.png");
        for (int i = 0; i < 10; ++i) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s/ships/ship-%02d-strip.png", assets.c_str(), i);
            renderer.game_view.load_ship_strip(buf, 4);
        }
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 3.0f;
    style.FramePadding = ImVec2(8.0f, 4.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    // App state
    neuroflyer::AppState state;
    state.data_dir = neuroflyer::data_dir();
    state.asset_dir = neuroflyer::asset_dir();
    state.settings_path = state.data_dir + "/settings.json";
    std::filesystem::create_directories(state.data_dir + "/nets");
    std::filesystem::create_directories(state.data_dir + "/genomes");
    state.config = neuroflyer::GameConfig::load(state.settings_path);
    state.rng.seed(std::random_device{}());

    // Recover auto-saves
    (void)neuroflyer::recover_autosaves(state.data_dir + "/genomes");

    // Apply saved graphics settings on startup
    {
        const auto& cfg = state.config;
        if (cfg.fullscreen) {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else if (cfg.window_width != SCREEN_W || cfg.window_height != SCREEN_H) {
            SDL_SetWindowSize(window, cfg.window_width, cfg.window_height);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }

    // UI Manager — push main menu as the root screen
    neuroflyer::UIManager ui(window, sdl_renderer,
                              state.config.window_width, state.config.window_height);
    ui.push_screen(std::make_unique<neuroflyer::MainMenuScreen>());

    // Main loop
    while (!state.quit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) state.quit_requested = true;
        }

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // UIManager handles screen + modal dispatch
        ui.draw(state, renderer);

        // Bridge: sync legacy go_to_screen() calls with UIManager stack
        ui.sync_legacy_navigation(state, renderer);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);

        // Execute deferred SDL draws (on top of ImGui)
        ui.post_render();
        renderer.flush_deferred();

        SDL_RenderPresent(sdl_renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

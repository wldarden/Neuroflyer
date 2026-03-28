#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/ship_design.h>
#include <neuralnet/network.h>
#include <neuralnet-ui/net_background.h>
#include <neuralnet-ui/render_neural_net.h>

#include <functional>
#include <memory>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace neuroflyer {

struct NodeClickInfo {
    int column = -1;  // 0 = input, 1+ = hidden/output layer
    int node = -1;    // node index within column
};

struct NetViewerViewState {
    // Data to render (set by owning screen each frame)
    Individual* individual = nullptr;    // non-owning pointer
    neuralnet::Network* network = nullptr;
    ShipDesign ship_design;
    std::vector<float> input_values;     // empty = no input display

    // Render area (set by owning screen)
    int render_x = 0, render_y = 0, render_w = 0, render_h = 0;

    // Editor mode
    bool editor_mode = false;
    bool modal_open = false;  // set by owning screen — suppresses SDL blit so ImGui modals show on top
    std::function<void(const NodeClickInfo&)> on_node_click;
    std::function<void(int column)> on_layer_click;

    // --- Internal state (managed by the view) ---

    // Scrolling (for nets taller than the view area)
    float scroll_y = 0.0f;        // current scroll offset (pixels, 0 = top)
    float scroll_x = 0.0f;        // horizontal scroll offset (pixels, 0 = left)
    float content_height = 0.0f;  // computed natural height of the net
    static constexpr float MIN_NODE_SPACING = 22.0f;  // minimum px between nodes

    // Zoom (only active when zoom_enabled is true)
    float zoom = 1.0f;            // canvas scale factor (1.0 = no zoom)
    bool zoom_enabled = false;    // set by owning screen to enable zoom controls

    // Background
    neuralnet_ui::NetBackgroundConfig bg_config;
    SDL_Texture* bg_tex = nullptr;

    // Off-screen render target for the net (allows scrolling via src rect)
    SDL_Texture* net_canvas = nullptr;
    int canvas_w = 0, canvas_h = 0;  // current canvas dimensions

    // Pending render (stored during draw, executed during flush)
    bool has_pending_render = false;
    neuralnet_ui::NetRenderConfig pending_config;
    std::shared_ptr<neuralnet::Network> pending_net_owner;

    // Click/hover results from last frame
    neuralnet_ui::NetRenderResult last_result;
};

/// Phase 1: Called during ImGui phase.
/// Stores the render config and checks last frame's click results.
/// Does NOT do any SDL rendering.
void draw_net_viewer_view(NetViewerViewState& state, SDL_Renderer* sdl_renderer);

/// Phase 2: Called after ImGui renders, before SDL_RenderPresent.
/// Renders the background + neural net to SDL.
void flush_net_viewer_view(NetViewerViewState& state, SDL_Renderer* sdl_renderer);

/// Clean up SDL resources owned by the view (background texture).
void destroy_net_viewer_view(NetViewerViewState& state);

} // namespace neuroflyer

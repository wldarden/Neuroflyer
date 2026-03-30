#include <neuroflyer/ui/views/net_viewer_view.h>

#include <neuroflyer/renderers/variant_net_render.h>

#include <neuralnet-ui/net_background.h>
#include <neuralnet-ui/render_neural_net.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace neuroflyer {

void draw_net_viewer_view(NetViewerViewState& state, SDL_Renderer* sdl_renderer) {
    if (!state.network) return;

    // Calculate the natural (unzoomed) height the net needs based on the tallest column
    const auto& topo = state.network->topology();
    std::size_t max_nodes = topo.input_size;
    for (const auto& layer : topo.layers) {
        max_nodes = std::max(max_nodes, layer.output_size);
    }
    float base_content_height = static_cast<float>(max_nodes + 1) *
                                NetViewerViewState::MIN_NODE_SPACING + 20.0f;

    // Expose unzoomed height to external consumers (e.g. topology preview)
    state.content_height = base_content_height;

    float view_w = static_cast<float>(state.render_w);
    float view_h = static_cast<float>(state.render_h);

    // Handle mouse wheel input when mouse is over the view area
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    bool mouse_in_view = mx >= state.render_x &&
                         mx <= state.render_x + state.render_w &&
                         my >= state.render_y &&
                         my <= state.render_y + state.render_h;

    float wheel   = ImGui::GetIO().MouseWheel;
    float wheel_h = ImGui::GetIO().MouseWheelH;
    bool  shift_held = ImGui::GetIO().KeyShift;

    if (mouse_in_view) {
        if (state.zoom_enabled && shift_held && wheel != 0.0f) {
            // Shift+scroll = zoom; keep the viewport center stable
            float old_zoom = state.zoom;
            state.zoom = std::clamp(state.zoom + wheel * 0.2f, 0.5f, 5.0f);
            if (old_zoom != state.zoom) {
                float ratio = state.zoom / old_zoom;
                float cx = state.scroll_x + view_w * 0.5f;
                float cy = state.scroll_y + view_h * 0.5f;
                state.scroll_x = cx * ratio - view_w * 0.5f;
                state.scroll_y = cy * ratio - view_h * 0.5f;
            }
        } else {
            // Regular scroll = pan (vertical and horizontal)
            if (wheel   != 0.0f) state.scroll_y -= wheel   * 40.0f;
            if (wheel_h != 0.0f) state.scroll_x += wheel_h * 40.0f;
        }
    }

    // Canvas dimensions scale with zoom
    int canvas_w = static_cast<int>(view_w * state.zoom);
    int canvas_h = static_cast<int>(std::max(view_h, base_content_height) * state.zoom);

    // Clamp scroll to the scrollable region on both axes
    float max_scroll_x = std::max(0.0f, static_cast<float>(canvas_w) - view_w);
    float max_scroll_y = std::max(0.0f, static_cast<float>(canvas_h) - view_h);
    state.scroll_x = std::clamp(state.scroll_x, 0.0f, max_scroll_x);
    state.scroll_y = std::clamp(state.scroll_y, 0.0f, max_scroll_y);

    // Map mouse from screen space to canvas space.
    // When canvas < viewport (zoomed out), SDL stretches the canvas to fill the
    // viewport, so we must reverse that scale in the mouse mapping.
    float vis_w = std::min(static_cast<float>(canvas_w), view_w);
    float vis_h = std::min(static_cast<float>(canvas_h), view_h);
    float scale_x = view_w / vis_w;  // >= 1.0 when zoomed out, 1.0 when zoomed in
    float scale_y = view_h / vis_h;
    int canvas_mx = static_cast<int>(static_cast<float>(mx - state.render_x) / scale_x + state.scroll_x);
    int canvas_my = static_cast<int>(static_cast<float>(my - state.render_y) / scale_y + state.scroll_y);

    auto net_ptr = std::make_shared<neuralnet::Network>(*state.network);
    auto cfg = build_variant_net_config({
        state.ship_design, *net_ptr, sdl_renderer,
        0, 0, canvas_w, canvas_h,
        state.input_values, canvas_mx, canvas_my,
        state.net_type});
    cfg.text_scale = std::max(1, static_cast<int>(std::round(state.zoom)));

    // Store for flush phase
    state.pending_config = std::move(cfg);
    state.pending_net_owner = std::move(net_ptr);
    state.has_pending_render = true;

    // Check click results from last frame
    if (state.editor_mode) {
        const auto& hit = state.last_result;
        if (hit.clicked.valid() && state.on_node_click) {
            state.on_node_click({hit.clicked.column, hit.clicked.node});
        } else if (hit.column_clicked >= 0 && state.on_layer_click) {
            state.on_layer_click(hit.column_clicked);
        }
    }
}

void flush_net_viewer_view(NetViewerViewState& state, SDL_Renderer* sdl_renderer) {
    if (!state.has_pending_render) return;
    state.has_pending_render = false;

    int canvas_w = state.pending_config.w;
    int canvas_h = state.pending_config.h;

    // Recreate the off-screen canvas if size changed
    if (!state.net_canvas ||
        state.canvas_w != canvas_w || state.canvas_h != canvas_h) {
        if (state.net_canvas) SDL_DestroyTexture(state.net_canvas);
        state.net_canvas = SDL_CreateTexture(sdl_renderer,
            SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
            canvas_w, canvas_h);
        SDL_SetTextureBlendMode(state.net_canvas, SDL_BLENDMODE_BLEND);
        state.canvas_w = canvas_w;
        state.canvas_h = canvas_h;
    }

    // Render the net into the off-screen canvas with transparent background.
    // BLENDMODE_NONE while rendering INTO the canvas so draw calls write exact
    // RGBA values without blending against the transparent clear color.
    SDL_SetRenderTarget(sdl_renderer, state.net_canvas);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
    SDL_RenderClear(sdl_renderer);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

    // Render neural net at (0, 0) in the canvas
    state.last_result = neuralnet_ui::render_neural_net(state.pending_config);

    // Switch back to the default render target (screen)
    SDL_SetRenderTarget(sdl_renderer, nullptr);

    // Skip screen blit when a modal is open (so ImGui modal renders on top)
    if (!state.modal_open) {
        // Lazy-generate background texture
        if (!state.bg_tex) {
            state.bg_tex = neuralnet_ui::generate_net_background(
                sdl_renderer, state.bg_config);
        }

        // Draw background at the view position (no scroll)
        neuralnet_ui::render_net_background(sdl_renderer, state.bg_tex,
            state.render_x - 10, state.render_y - 10,
            state.render_w + 20, state.render_h + 20,
            state.bg_config.render_alpha);

        // Blit the visible portion of the canvas onto the screen.
        // When zoomed out, the canvas is smaller than the viewport — SDL stretches
        // it to fill. When zoomed in, we copy a viewport-sized window from the
        // larger canvas (1:1 pixels).
        SDL_SetTextureBlendMode(state.net_canvas, SDL_BLENDMODE_BLEND);
        int scroll_x_px = static_cast<int>(std::round(state.scroll_x));
        int scroll_y_px = static_cast<int>(std::round(state.scroll_y));
        int src_w = std::min(canvas_w, state.render_w);
        int src_h = std::min(canvas_h, state.render_h);
        SDL_Rect src = {scroll_x_px, scroll_y_px, src_w, src_h};
        SDL_Rect dst = {state.render_x, state.render_y,
                        state.render_w, state.render_h};
        SDL_RenderCopy(sdl_renderer, state.net_canvas, &src, &dst);
    }

    // Release ownership
    state.pending_net_owner.reset();
}

void destroy_net_viewer_view(NetViewerViewState& state) {
    if (state.bg_tex) {
        SDL_DestroyTexture(state.bg_tex);
        state.bg_tex = nullptr;
    }
    if (state.net_canvas) {
        SDL_DestroyTexture(state.net_canvas);
        state.net_canvas = nullptr;
    }
}

} // namespace neuroflyer

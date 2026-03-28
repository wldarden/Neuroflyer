# Net Viewer Zoom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make neural net nodes 50% bigger, render the variant net editor at 2x zoom with pinch-to-zoom support.

**Architecture:** The net renderer already takes `w`/`h` and lays out nodes proportionally. Zoom works by rendering into a larger off-screen canvas and blitting a viewport-sized window from it. Pinch events flow from SDL main loop → AppState → editor screen → view state.

**Tech Stack:** SDL2 (SDL_MULTIGESTURE, SDL_Texture), ImGui (MouseWheel/KeySuper), C++20

---

### Task 1: Increase Node Radii

**Files:**
- Modify: `libs/neuralnet-ui/src/render_neural_net.cpp`

- [ ] **Step 1: Update node radius constants**

In `render_neural_net.cpp`, change three radius values and the hover detection radius:

```cpp
// Line 63: hover detection radius
constexpr float HOVER_RADIUS = 18.0f;  // was 12.0f

// Line 251: input node radius
render_filled_circle(config.renderer, columns[0][i].x, columns[0][i].y, 6.0f, input_act, 68, 136, 255);  // was 4.0f

// Line 287: output node radius
render_filled_circle(config.renderer, columns[l + 1][i].x, columns[l + 1][i].y, 9.0f, act, 0, 255, 136);  // was 6.0f

// Line 311: hidden node radius
render_filled_circle(config.renderer, columns[l + 1][i].x, columns[l + 1][i].y, 6.0f, act, 255, 136, 68);  // was 4.0f
```

- [ ] **Step 2: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer`
Expected: Clean build, no errors.

- [ ] **Step 3: Commit**

```bash
git add libs/neuralnet-ui/src/render_neural_net.cpp
git commit -m "feat(neuralnet-ui): increase node radii by 50% for readability"
```

---

### Task 2: Add Zoom and Scroll State to NetViewerViewState

**Files:**
- Modify: `neuroflyer/include/neuroflyer/ui/views/net_viewer_view.h`

- [ ] **Step 1: Add zoom, scroll_x, and zoom_enabled fields**

In `net_viewer_view.h`, add three fields to `NetViewerViewState` in the "Internal state" section, after the existing `scroll_y` and `content_height` fields:

```cpp
    // Scrolling (for nets taller than the view area)
    float scroll_y = 0.0f;        // current scroll offset (pixels, 0 = top)
    float scroll_x = 0.0f;        // horizontal scroll offset (pixels, 0 = left)
    float content_height = 0.0f;  // computed natural height of the net
    static constexpr float MIN_NODE_SPACING = 22.0f;  // minimum px between nodes

    // Zoom (only active when zoom_enabled is true)
    float zoom = 1.0f;            // canvas scale factor (1.0 = no zoom)
    bool zoom_enabled = false;    // set by owning screen to enable zoom controls
```

- [ ] **Step 2: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer`
Expected: Clean build. New fields default to no-zoom behavior.

- [ ] **Step 3: Commit**

```bash
git add neuroflyer/include/neuroflyer/ui/views/net_viewer_view.h
git commit -m "feat(neuroflyer): add zoom and horizontal scroll state to NetViewerViewState"
```

---

### Task 3: Implement Zoom-Aware Canvas Rendering

**Files:**
- Modify: `neuroflyer/src/ui/views/net_viewer_view.cpp`

This is the core change. The view function must:
1. Scale the canvas by `zoom`
2. Handle horizontal scroll alongside vertical
3. Support Cmd+scroll for zoom when `zoom_enabled` is true
4. Map mouse coordinates correctly for hover/click
5. Blit the correct viewport-sized portion of the zoomed canvas

- [ ] **Step 1: Rewrite draw_net_viewer_view with zoom support**

Replace the body of `draw_net_viewer_view` in `net_viewer_view.cpp`:

```cpp
void draw_net_viewer_view(NetViewerViewState& state, SDL_Renderer* sdl_renderer) {
    if (!state.network) return;

    // Calculate the natural height the net needs based on the tallest column
    const auto& topo = state.network->topology();
    std::size_t max_nodes = topo.input_size;
    for (const auto& layer : topo.layers) {
        max_nodes = std::max(max_nodes, layer.output_size);
    }
    float base_content_height = static_cast<float>(max_nodes + 1) *
                                NetViewerViewState::MIN_NODE_SPACING + 20.0f;
    state.content_height = base_content_height;

    float view_w = static_cast<float>(state.render_w);
    float view_h = static_cast<float>(state.render_h);

    // Handle mouse input
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    bool mouse_in_view = mx >= state.render_x &&
                         mx <= state.render_x + state.render_w &&
                         my >= state.render_y &&
                         my <= state.render_y + state.render_h;

    float wheel = ImGui::GetIO().MouseWheel;
    float wheel_h = ImGui::GetIO().MouseWheelH;
    bool cmd_held = ImGui::GetIO().KeySuper;

    if (mouse_in_view) {
        if (state.zoom_enabled && cmd_held && wheel != 0.0f) {
            // Cmd+scroll = zoom
            float old_zoom = state.zoom;
            state.zoom = std::clamp(state.zoom + wheel * 0.2f, 0.5f, 5.0f);
            // Keep viewport center stable
            if (old_zoom != state.zoom) {
                float ratio = state.zoom / old_zoom;
                float cx = state.scroll_x + view_w * 0.5f;
                float cy = state.scroll_y + view_h * 0.5f;
                state.scroll_x = cx * ratio - view_w * 0.5f;
                state.scroll_y = cy * ratio - view_h * 0.5f;
            }
        } else {
            // Regular scroll = pan
            if (wheel != 0.0f) state.scroll_y -= wheel * 40.0f;
            if (wheel_h != 0.0f) state.scroll_x += wheel_h * 40.0f;
        }
    }

    // Compute zoomed canvas dimensions
    int canvas_w = static_cast<int>(view_w * state.zoom);
    int canvas_h = static_cast<int>(std::max(view_h, base_content_height) * state.zoom);

    // Clamp scroll to valid range
    float max_scroll_x = std::max(0.0f, static_cast<float>(canvas_w) - view_w);
    float max_scroll_y = std::max(0.0f, static_cast<float>(canvas_h) - view_h);
    state.scroll_x = std::clamp(state.scroll_x, 0.0f, max_scroll_x);
    state.scroll_y = std::clamp(state.scroll_y, 0.0f, max_scroll_y);

    // Map mouse from screen space to canvas space
    int canvas_mx = mx - state.render_x + static_cast<int>(std::round(state.scroll_x));
    int canvas_my = my - state.render_y + static_cast<int>(std::round(state.scroll_y));

    auto net_ptr = std::make_shared<neuralnet::Network>(*state.network);
    auto cfg = build_variant_net_config({
        state.ship_design, *net_ptr, sdl_renderer,
        0, 0, canvas_w, canvas_h,
        state.input_values, canvas_mx, canvas_my});

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
```

- [ ] **Step 2: Update flush_net_viewer_view for zoom blit**

In `flush_net_viewer_view`, update the blit section to use both scroll offsets:

Replace the blit block (the section after `if (!state.modal_open) {`):

```cpp
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
        // BLENDMODE_BLEND so transparent areas show the background through.
        SDL_SetTextureBlendMode(state.net_canvas, SDL_BLENDMODE_BLEND);
        int scroll_x_px = static_cast<int>(std::round(state.scroll_x));
        int scroll_y_px = static_cast<int>(std::round(state.scroll_y));
        SDL_Rect src = {scroll_x_px, scroll_y_px, state.render_w, state.render_h};
        SDL_Rect dst = {state.render_x, state.render_y,
                        state.render_w, state.render_h};
        SDL_RenderCopy(sdl_renderer, state.net_canvas, &src, &dst);
    }
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer`
Expected: Clean build. At zoom=1 (default for read-only viewers), behavior is identical to before.

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/src/ui/views/net_viewer_view.cpp
git commit -m "feat(neuroflyer): implement zoom-aware canvas with horizontal scroll"
```

---

### Task 4: Add Pinch Event Forwarding

**Files:**
- Modify: `neuroflyer/include/neuroflyer/app_state.h`
- Modify: `neuroflyer/src/ui/main.cpp`

- [ ] **Step 1: Add pinch_zoom_delta to AppState**

In `app_state.h`, add a field to `AppState` after `quit_requested`:

```cpp
    bool quit_requested = false;

    // Per-frame pinch-to-zoom delta (from SDL_MULTIGESTURE events).
    // Accumulated during event polling, consumed and reset each frame.
    float pinch_zoom_delta = 0.0f;
```

- [ ] **Step 2: Capture SDL_MULTIGESTURE in main loop and reset each frame**

In `main.cpp`, inside the main loop, add pinch capture to the event poll and reset the delta each frame.

Add the reset line just before `SDL_PollEvent`:

```cpp
    while (!state.quit_requested) {
        state.pinch_zoom_delta = 0.0f;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) state.quit_requested = true;
            if (event.type == SDL_MULTIGESTURE) {
                state.pinch_zoom_delta += event.mgesture.dDist;
            }
        }
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer`
Expected: Clean build. No functional change yet (nothing reads pinch_zoom_delta).

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/include/neuroflyer/app_state.h neuroflyer/src/ui/main.cpp
git commit -m "feat(neuroflyer): capture SDL_MULTIGESTURE pinch events in main loop"
```

---

### Task 5: Wire Up Variant Net Editor — Default Zoom + Pinch

**Files:**
- Modify: `neuroflyer/src/ui/screens/hangar/variant_net_editor_screen.cpp`

- [ ] **Step 1: Set default zoom and zoom_enabled in constructor**

In the `VariantNetEditorScreen` constructor, after setting `viewer_state_.editor_mode = true`, add:

```cpp
    viewer_state_.editor_mode = true;
    viewer_state_.zoom = 2.0f;
    viewer_state_.zoom_enabled = true;
```

- [ ] **Step 2: Read pinch_zoom_delta in on_draw**

In `VariantNetEditorScreen::on_draw()`, add pinch-to-zoom handling right before the render bounds section (before the `constexpr float LEFT_PANEL_W` line):

```cpp
    // Pinch-to-zoom
    if (state.pinch_zoom_delta != 0.0f) {
        float old_zoom = viewer_state_.zoom;
        viewer_state_.zoom = std::clamp(
            viewer_state_.zoom + state.pinch_zoom_delta * 5.0f, 0.5f, 5.0f);
        // Keep viewport center stable during zoom
        if (old_zoom != viewer_state_.zoom) {
            float vw = static_cast<float>(viewer_state_.render_w);
            float vh = static_cast<float>(viewer_state_.render_h);
            float ratio = viewer_state_.zoom / old_zoom;
            float cx = viewer_state_.scroll_x + vw * 0.5f;
            float cy = viewer_state_.scroll_y + vh * 0.5f;
            viewer_state_.scroll_x = cx * ratio - vw * 0.5f;
            viewer_state_.scroll_y = cy * ratio - vh * 0.5f;
        }
    }

    // Set render bounds for the net panel:
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add neuroflyer/src/ui/screens/hangar/variant_net_editor_screen.cpp
git commit -m "feat(neuroflyer): wire variant net editor with 2x default zoom and pinch-to-zoom"
```

---

### Task 6: Manual Testing

- [ ] **Step 1: Test node size increase**

Launch NeuroFlyer, open any genome in the variant viewer. Verify nodes are visibly larger than before. Hover over nodes — tooltip/weight labels should still appear correctly.

- [ ] **Step 2: Test variant net editor zoom**

From variant viewer, click "View Neural Net" to open the variant net editor. Verify:
- The net renders at 2x scale (labels more readable, nodes more spread out)
- Two-finger scroll pans vertically and horizontally
- Hover highlights the correct node
- Left-click opens node editor modal for the correct node
- Right-click opens layer editor for the correct column

- [ ] **Step 3: Test pinch-to-zoom**

In the variant net editor:
- Pinch to zoom in (expand fingers) — net should get larger, viewport center stays stable
- Pinch to zoom out (contract fingers) — net should get smaller
- If pinch doesn't fire SDL_MULTIGESTURE, use Cmd+scroll wheel as fallback
- Verify zoom clamps at 0.5x and 5.0x

- [ ] **Step 4: Test read-only viewer is unaffected**

Open the variant viewer's inline net panel. Verify:
- Nodes are bigger (from Task 1)
- No zoom controls — scroll still works as before
- No horizontal scrollbar behavior

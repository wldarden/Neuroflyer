# Net Viewer: Bigger Nodes, Zoom, and Pinch-to-Zoom

## Goal

Make the neural net visualization more readable by increasing node sizes, drawing the net at 2x scale in the variant net editor, and adding pinch-to-zoom for dynamic zoom control.

## Three Changes

### 1. Node Radii +50% (all net viewers)

In `render_neural_net.cpp`, increase node drawing radii:

| Element | Before | After |
|---------|--------|-------|
| Input/hidden node radius | 4.0 | 6.0 |
| Output node radius | 6.0 | 9.0 |
| Hover detection radius | 12.0 | 18.0 |

This applies everywhere the net renderer is used (variant viewer, variant net editor, fly session).

### 2. Zoom-Scaled Canvas (variant net editor only)

The net is rendered into an off-screen SDL texture (canvas), then blitted to screen. Currently the canvas is 1:1 with the viewport. With zoom:

- Canvas dimensions = `viewport_size * zoom` (e.g., zoom=2 → 2x canvas)
- `render_neural_net()` receives the larger canvas dimensions, so nodes are naturally more spread out
- The viewport shows a `viewport_w x viewport_h` window into the larger canvas
- Two-finger scroll pans both vertically and horizontally
- Mouse coordinates map from screen to canvas: `canvas_pos = screen_pos - viewport_origin + scroll_offset`

Default zoom for the variant net editor: **2.0x**.

### 3. Pinch-to-Zoom (variant net editor only)

On macOS, trackpad pinch generates `SDL_MULTIGESTURE` events with a `dDist` delta. These are NOT processed by ImGui's SDL2 backend, so they must be captured in the main event loop.

**Event flow:**
1. `main.cpp` event loop captures `SDL_MULTIGESTURE` → accumulates `dDist` into `AppState::pinch_zoom_delta`
2. `VariantNetEditorScreen::on_draw()` reads `pinch_zoom_delta`, adjusts `viewer_state_.zoom`
3. Zoom is clamped to [0.5, 5.0]
4. `pinch_zoom_delta` is reset to 0 each frame

**Viewport centering:** When zoom changes, scroll offsets are adjusted proportionally so the center of the viewport remains stable (no jarring jump to top-left).

**Fallback:** If `SDL_MULTIGESTURE` doesn't fire on a given system, Cmd+scroll (Super+MouseWheel) also adjusts zoom. This is handled in `draw_net_viewer_view()` when `zoom_enabled` is true.

## State Changes

`NetViewerViewState` gains:
- `float zoom = 1.0f` — current zoom level
- `float scroll_x = 0.0f` — horizontal scroll offset (needed when zoomed)
- `bool zoom_enabled = false` — only the variant net editor sets this to true

`AppState` gains:
- `float pinch_zoom_delta = 0.0f` — per-frame pinch accumulator

## Files Modified

| File | Change |
|------|--------|
| `libs/neuralnet-ui/src/render_neural_net.cpp` | Node radii constants |
| `neuroflyer/include/neuroflyer/app_state.h` | `pinch_zoom_delta` field |
| `neuroflyer/src/ui/main.cpp` | Capture `SDL_MULTIGESTURE`, reset delta each frame |
| `neuroflyer/include/neuroflyer/ui/views/net_viewer_view.h` | `zoom`, `scroll_x`, `zoom_enabled` fields |
| `neuroflyer/src/ui/views/net_viewer_view.cpp` | Zoom-aware canvas, scroll, mouse mapping, blit |
| `neuroflyer/src/ui/screens/hangar/variant_net_editor_screen.cpp` | Set zoom=2, zoom_enabled=true, read pinch delta |

## What Doesn't Change

- The read-only net viewer in `VariantViewerScreen` — no zoom, just bigger nodes from Change 1
- The generic `render_neural_net()` API — it already takes w/h, bigger canvas = bigger layout
- Click/hover detection — works automatically because mouse coordinates are mapped to canvas space

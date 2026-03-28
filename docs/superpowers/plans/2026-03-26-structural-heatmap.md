# Structural Heatmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an analysis screen with a cumulative structural heatmap showing network topology exploration across generations.

**Architecture:** Record per-generation topology histograms during flight, add an Analysis screen accessible from pause, render the cumulative heatmap as a smooth SDL texture with bilinear interpolation displayed via `ImGui::Image()`. The analysis screen is a thin shell that hosts independent chart components one at a time.

**Tech Stack:** C++20, SDL2 (texture rendering), ImGui, CMake

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-26-structural-heatmap-design.md`

---

### Task 1: Add StructuralHistogram and data collection

**Files:**
- Modify: `neuroflyer/include/neuroflyer/evolution.h:13-25`
- Modify: `neuroflyer/include/neuroflyer/screens/fly_session.h:40-43`
- Modify: `neuroflyer/src/screens/fly_session.cpp:92-124`

- [ ] **Step 1: Add StructuralHistogram struct to evolution.h**

In `neuroflyer/include/neuroflyer/evolution.h`, add after the `EvolutionConfig` struct (after line 25) and before the `Individual` struct:

```cpp
/// Per-generation histogram of (hidden_layer_count, total_hidden_nodes) pairs.
struct StructuralHistogram {
    std::map<std::pair<int,int>, int> bins;
};
```

Add `#include <map>` and `#include <utility>` to the includes at the top of the file.

- [ ] **Step 2: Add structural_history to FlySessionState**

In `neuroflyer/include/neuroflyer/screens/fly_session.h`, after the `gen_history` field (line 42), add:

```cpp
    std::vector<StructuralHistogram> structural_history;
```

Add `#include <map>` and `#include <utility>` to the file's includes if not already present (they come in transitively via evolution.h, but check).

- [ ] **Step 3: Record histogram at generation end**

In `neuroflyer/src/screens/fly_session.cpp`, inside `do_evolution()`, after `s.gen_history.push_back(...)` (line 114) and before the console log (line 116), add:

```cpp
    // Record structural histogram for this generation
    {
        StructuralHistogram hist;
        for (const auto& ind : s.population) {
            int layer_count = 0;
            int node_count = 0;
            // Exclude the last layer (output layer) — count only hidden layers
            if (ind.topology.layers.size() > 1) {
                layer_count = static_cast<int>(ind.topology.layers.size()) - 1;
                for (std::size_t li = 0; li < ind.topology.layers.size() - 1; ++li) {
                    node_count += static_cast<int>(ind.topology.layers[li].output_size);
                }
            }
            hist.bins[{layer_count, node_count}]++;
        }
        s.structural_history.push_back(std::move(hist));
    }
```

- [ ] **Step 4: Clear structural_history on reset**

In `neuroflyer/src/screens/fly_session.cpp`, find the `FlySessionState::reset()` function. Add `structural_history.clear();` alongside the existing `gen_history.clear();`.

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add neuroflyer/include/neuroflyer/evolution.h \
       neuroflyer/include/neuroflyer/screens/fly_session.h \
       neuroflyer/src/screens/fly_session.cpp
git commit -m "feat(neuroflyer): record structural histogram per generation during flight"
```

---

### Task 2: Add Analysis screen enum and routing

**Files:**
- Modify: `neuroflyer/include/neuroflyer/app_state.h:13-21`
- Create: `neuroflyer/include/neuroflyer/screens/analysis.h`
- Create: `neuroflyer/src/screens/analysis.cpp`
- Modify: `neuroflyer/src/main.cpp:97-109`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Add Analysis to Screen enum**

In `neuroflyer/include/neuroflyer/app_state.h`, add `Analysis` to the `Screen` enum (after `PauseConfig`, line 21):

```cpp
enum class Screen {
    MainMenu,
    Hangar,
    CreateGenome,
    VariantViewer,
    LineageTree,
    Flying,
    PauseConfig,
    Analysis,
};
```

- [ ] **Step 2: Create analysis screen header**

Create `neuroflyer/include/neuroflyer/screens/analysis.h`:

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/renderer.h>

#include <SDL.h>

#include <map>
#include <utility>
#include <vector>

namespace neuroflyer {

struct StructuralHeatmapState {
    SDL_Texture* texture = nullptr;
    bool dirty = true;
    int tex_w = 256;
    int tex_h = 256;

    // Axis bounds (auto-computed from data)
    int min_layers = 0;
    int max_layers = 1;
    int min_nodes = 0;
    int max_nodes = 1;

    // Cumulative grid: cumulative[layers][nodes] = total count
    std::map<std::pair<int,int>, int> cumulative;
    int max_count = 1;

    void destroy() {
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }
};

enum class AnalysisChart {
    StructuralHeatmap,
};

struct AnalysisState {
    AnalysisChart active_chart = AnalysisChart::StructuralHeatmap;
    StructuralHeatmapState heatmap;
    bool wants_back = false;
};

void draw_analysis(AnalysisState& analysis,
                   const std::vector<StructuralHistogram>& structural_history,
                   AppState& state,
                   Renderer& renderer);

} // namespace neuroflyer
```

- [ ] **Step 3: Create analysis screen stub**

Create `neuroflyer/src/screens/analysis.cpp`:

```cpp
#include <neuroflyer/screens/analysis.h>

#include <imgui.h>

namespace neuroflyer {

void draw_analysis(AnalysisState& analysis,
                   const std::vector<StructuralHistogram>& structural_history,
                   AppState& state,
                   Renderer& renderer) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::Begin("##AnalysisScreen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    // Sidebar
    constexpr float SIDEBAR_W = 200.0f;
    ImGui::BeginChild("##Sidebar", ImVec2(SIDEBAR_W, -1), true);

    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Analysis");
    ImGui::Separator();

    // Chart picker
    bool is_heatmap = (analysis.active_chart == AnalysisChart::StructuralHeatmap);
    if (ImGui::Selectable("Structural History", is_heatmap)) {
        analysis.active_chart = AnalysisChart::StructuralHeatmap;
    }

    ImGui::Separator();

    // Stats
    ImGui::Text("Generations: %zu", structural_history.size());

    if (!analysis.heatmap.cumulative.empty()) {
        // Find most common topology
        int best_count = 0;
        std::pair<int,int> best_topo = {0, 0};
        for (const auto& [key, count] : analysis.heatmap.cumulative) {
            if (count > best_count) {
                best_count = count;
                best_topo = key;
            }
        }
        ImGui::Text("Most common:");
        ImGui::Text("  %dL x %d nodes", best_topo.first, best_topo.second);
    }

    // Back button at bottom
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40.0f);
    if (ImGui::Button("Back", ImVec2(-1, 30))) {
        analysis.wants_back = true;
    }

    ImGui::EndChild();

    // Main chart area
    ImGui::SameLine();
    ImGui::BeginChild("##ChartArea", ImVec2(0, -1), false);

    switch (analysis.active_chart) {
    case AnalysisChart::StructuralHeatmap:
        // Placeholder — Task 3 fills this in
        ImGui::Text("Structural Heatmap (placeholder)");
        break;
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace neuroflyer
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add `src/screens/analysis.cpp` after the `src/screens/pause_config.cpp` line (line 23):

```cmake
    src/screens/analysis.cpp
```

- [ ] **Step 5: Wire into main.cpp router**

In `neuroflyer/src/main.cpp`, add the include at the top:

```cpp
#include <neuroflyer/screens/analysis.h>
```

Add a static `AnalysisState` and the switch case. Find the `switch (state.current)` block (around line 98). After the `PauseConfig` case (line 108), add:

```cpp
        case S::Analysis: {
            static neuroflyer::AnalysisState s_analysis;
            neuroflyer::draw_analysis(s_analysis,
                neuroflyer::get_fly_session_state().structural_history,
                state, renderer);
            if (s_analysis.wants_back) {
                s_analysis.wants_back = false;
                go_to_screen(state, Screen::PauseConfig);
            }
            break;
        }
```

- [ ] **Step 6: Add "Analysis" button to pause screen**

In `neuroflyer/src/screens/pause_config.cpp`, add the include at the top:

```cpp
#include <neuroflyer/screens/analysis.h>
```

Find the footer buttons section (around line 240). After the "Cancel" button block (lines 244-250) and before the "Headless run" section (line 252), add:

```cpp
    ImGui::SameLine(0.0f, 20.0f);
    if (ImGui::Button("Analysis", ImVec2(120, btn_h))) {
        go_to_screen(state, Screen::Analysis);
    }
```

- [ ] **Step 7: Build and verify**

Run: `cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build. The analysis screen should be reachable from pause but shows placeholder text.

- [ ] **Step 8: Commit**

```bash
git add neuroflyer/include/neuroflyer/app_state.h \
       neuroflyer/include/neuroflyer/screens/analysis.h \
       neuroflyer/src/screens/analysis.cpp \
       neuroflyer/src/main.cpp \
       neuroflyer/src/screens/pause_config.cpp \
       neuroflyer/CMakeLists.txt
git commit -m "feat(neuroflyer): analysis screen with sidebar and routing from pause"
```

---

### Task 3: Structural heatmap SDL texture rendering

**Files:**
- Create: `neuroflyer/include/neuroflyer/components/structural_heatmap.h`
- Create: `neuroflyer/src/components/structural_heatmap.cpp`
- Modify: `neuroflyer/src/screens/analysis.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create heatmap component header**

Create `neuroflyer/include/neuroflyer/components/structural_heatmap.h`:

```cpp
#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/screens/analysis.h>

#include <SDL.h>

#include <vector>

namespace neuroflyer {

/// Rebuild the cumulative grid from structural history and render to SDL texture.
void update_heatmap_texture(StructuralHeatmapState& hm,
                            const std::vector<StructuralHistogram>& history,
                            SDL_Renderer* sdl_renderer);

/// Draw the heatmap texture + ImGui overlays (axes, labels, legend).
void draw_structural_heatmap(StructuralHeatmapState& hm,
                             const std::vector<StructuralHistogram>& history,
                             SDL_Renderer* sdl_renderer);

} // namespace neuroflyer
```

- [ ] **Step 2: Implement heatmap rendering**

Create `neuroflyer/src/components/structural_heatmap.cpp`:

```cpp
#include <neuroflyer/components/structural_heatmap.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuroflyer {

namespace {

/// Map a normalized [0,1] value to a color: dark → purple → orange.
void intensity_to_rgba(float t, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.001f) {
        // Background
        r = 20; g = 20; b = 30; a = 255;
        return;
    }
    // Purple (80,40,120) at low → Orange (255,142,60) at high
    float lo_r = 80,  lo_g = 40,  lo_b = 120;
    float hi_r = 255, hi_g = 142, hi_b = 60;
    r = static_cast<uint8_t>(lo_r + (hi_r - lo_r) * t);
    g = static_cast<uint8_t>(lo_g + (hi_g - lo_g) * t);
    b = static_cast<uint8_t>(lo_b + (hi_b - lo_b) * t);
    a = 255;
}

/// Sample the cumulative grid at fractional (layers, nodes) with bilinear interpolation.
float sample_bilinear(const std::map<std::pair<int,int>, int>& grid,
                      float layers_f, float nodes_f, int max_count) {
    if (max_count <= 0) return 0.0f;

    int l0 = static_cast<int>(std::floor(layers_f));
    int l1 = l0 + 1;
    int n0 = static_cast<int>(std::floor(nodes_f));
    int n1 = n0 + 1;

    float fl = layers_f - static_cast<float>(l0);
    float fn = nodes_f - static_cast<float>(n0);

    auto lookup = [&](int l, int n) -> float {
        auto it = grid.find({l, n});
        if (it == grid.end()) return 0.0f;
        return static_cast<float>(it->second) / static_cast<float>(max_count);
    };

    float v00 = lookup(l0, n0);
    float v10 = lookup(l1, n0);
    float v01 = lookup(l0, n1);
    float v11 = lookup(l1, n1);

    float top    = v00 * (1.0f - fn) + v01 * fn;
    float bottom = v10 * (1.0f - fn) + v11 * fn;
    return top * (1.0f - fl) + bottom * fl;
}

} // namespace

void update_heatmap_texture(StructuralHeatmapState& hm,
                            const std::vector<StructuralHistogram>& history,
                            SDL_Renderer* sdl_renderer) {
    // Accumulate all histograms
    hm.cumulative.clear();
    hm.min_layers = std::numeric_limits<int>::max();
    hm.max_layers = 0;
    hm.min_nodes = std::numeric_limits<int>::max();
    hm.max_nodes = 0;
    hm.max_count = 0;

    for (const auto& hist : history) {
        for (const auto& [key, count] : hist.bins) {
            hm.cumulative[key] += count;
            hm.min_layers = std::min(hm.min_layers, key.first);
            hm.max_layers = std::max(hm.max_layers, key.first);
            hm.min_nodes  = std::min(hm.min_nodes, key.second);
            hm.max_nodes  = std::max(hm.max_nodes, key.second);
        }
    }

    // Find max cumulative count for normalization
    for (const auto& [key, count] : hm.cumulative) {
        hm.max_count = std::max(hm.max_count, count);
    }

    // Ensure minimum range for axes
    if (hm.min_layers == std::numeric_limits<int>::max()) hm.min_layers = 0;
    if (hm.min_nodes == std::numeric_limits<int>::max()) hm.min_nodes = 0;
    if (hm.max_layers <= hm.min_layers) hm.max_layers = hm.min_layers + 1;
    if (hm.max_nodes <= hm.min_nodes) hm.max_nodes = hm.min_nodes + 1;

    // Add padding to axis ranges
    int layer_pad = std::max(1, (hm.max_layers - hm.min_layers) / 4);
    int node_pad  = std::max(2, (hm.max_nodes - hm.min_nodes) / 4);
    hm.min_layers = std::max(0, hm.min_layers - layer_pad);
    hm.max_layers += layer_pad;
    hm.min_nodes  = std::max(0, hm.min_nodes - node_pad);
    hm.max_nodes  += node_pad;

    // Create or recreate texture
    if (!hm.texture) {
        hm.texture = SDL_CreateTexture(sdl_renderer,
            SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
            hm.tex_w, hm.tex_h);
        SDL_SetTextureBlendMode(hm.texture, SDL_BLENDMODE_BLEND);
    }

    // Render pixels
    std::vector<uint8_t> pixels(static_cast<std::size_t>(hm.tex_w * hm.tex_h * 4));

    float layer_range = static_cast<float>(hm.max_layers - hm.min_layers);
    float node_range  = static_cast<float>(hm.max_nodes - hm.min_nodes);

    for (int py = 0; py < hm.tex_h; ++py) {
        for (int px = 0; px < hm.tex_w; ++px) {
            // Map pixel to data coordinates
            // Y-axis: layers (top = max, bottom = min)
            float layers_f = static_cast<float>(hm.max_layers) -
                             (static_cast<float>(py) / static_cast<float>(hm.tex_h - 1)) * layer_range;
            // X-axis: nodes (left = min, right = max)
            float nodes_f = static_cast<float>(hm.min_nodes) +
                            (static_cast<float>(px) / static_cast<float>(hm.tex_w - 1)) * node_range;

            float intensity = sample_bilinear(hm.cumulative, layers_f, nodes_f, hm.max_count);

            std::size_t idx = static_cast<std::size_t>((py * hm.tex_w + px) * 4);
            intensity_to_rgba(intensity, pixels[idx], pixels[idx+1], pixels[idx+2], pixels[idx+3]);
        }
    }

    SDL_UpdateTexture(hm.texture, nullptr, pixels.data(), hm.tex_w * 4);
    hm.dirty = false;
}

void draw_structural_heatmap(StructuralHeatmapState& hm,
                             const std::vector<StructuralHistogram>& history,
                             SDL_Renderer* sdl_renderer) {
    if (history.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "No generation data yet. Run some generations first.");
        return;
    }

    if (hm.dirty || !hm.texture) {
        update_heatmap_texture(hm, history, sdl_renderer);
    }

    // Title
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Structural History");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
        "Cumulative topology density across %zu generations", history.size());
    ImGui::Spacing();

    // Layout: Y-axis label | heatmap image | legend
    ImVec2 avail = ImGui::GetContentRegionAvail();
    constexpr float LABEL_W = 60.0f;
    constexpr float LEGEND_W = 80.0f;
    constexpr float BOTTOM_H = 40.0f;
    float img_w = avail.x - LABEL_W - LEGEND_W;
    float img_h = avail.y - BOTTOM_H;
    if (img_w < 100.0f) img_w = 100.0f;
    if (img_h < 100.0f) img_h = 100.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Y-axis labels (left of image)
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int y_ticks = std::min(hm.max_layers - hm.min_layers + 1, 8);
    for (int i = 0; i <= y_ticks; ++i) {
        float frac = static_cast<float>(i) / static_cast<float>(std::max(y_ticks, 1));
        float py = cursor.y + frac * img_h;
        int layer_val = hm.max_layers - static_cast<int>(
            frac * static_cast<float>(hm.max_layers - hm.min_layers));
        char label[16];
        std::snprintf(label, sizeof(label), "%dL", layer_val);
        draw->AddText(ImVec2(cursor.x, py - 6.0f), IM_COL32(150, 150, 150, 255), label);
    }

    // Heatmap image
    ImGui::SetCursorScreenPos(ImVec2(cursor.x + LABEL_W, cursor.y));
    ImGui::Image(reinterpret_cast<ImTextureID>(hm.texture), ImVec2(img_w, img_h));

    // X-axis labels (below image)
    int x_ticks = std::min(hm.max_nodes - hm.min_nodes + 1, 10);
    for (int i = 0; i <= x_ticks; ++i) {
        float frac = static_cast<float>(i) / static_cast<float>(std::max(x_ticks, 1));
        float px = cursor.x + LABEL_W + frac * img_w;
        int node_val = hm.min_nodes + static_cast<int>(
            frac * static_cast<float>(hm.max_nodes - hm.min_nodes));
        char label[16];
        std::snprintf(label, sizeof(label), "%d", node_val);
        draw->AddText(ImVec2(px - 8.0f, cursor.y + img_h + 4.0f),
                      IM_COL32(150, 150, 150, 255), label);
    }

    // X-axis title
    draw->AddText(ImVec2(cursor.x + LABEL_W + img_w * 0.35f, cursor.y + img_h + 20.0f),
                  IM_COL32(180, 180, 180, 255), "Total Hidden Nodes");

    // Color legend (right of image)
    float legend_x = cursor.x + LABEL_W + img_w + 10.0f;
    float legend_h = img_h * 0.6f;
    float legend_top = cursor.y + (img_h - legend_h) * 0.5f;
    constexpr int LEGEND_STEPS = 32;
    float step_h = legend_h / static_cast<float>(LEGEND_STEPS);

    for (int i = 0; i < LEGEND_STEPS; ++i) {
        float t = 1.0f - static_cast<float>(i) / static_cast<float>(LEGEND_STEPS - 1);
        uint8_t r, g, b, a;
        intensity_to_rgba(t, r, g, b, a);
        float y0 = legend_top + static_cast<float>(i) * step_h;
        draw->AddRectFilled(
            ImVec2(legend_x, y0),
            ImVec2(legend_x + 16.0f, y0 + step_h + 1.0f),
            IM_COL32(r, g, b, a));
    }
    draw->AddText(ImVec2(legend_x + 20.0f, legend_top - 2.0f),
                  IM_COL32(150, 150, 150, 255), "High");
    draw->AddText(ImVec2(legend_x + 20.0f, legend_top + legend_h - 10.0f),
                  IM_COL32(150, 150, 150, 255), "Low");
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add after `src/components/test_bench.cpp` (line 16):

```cmake
    src/components/structural_heatmap.cpp
```

- [ ] **Step 4: Wire heatmap into analysis screen**

In `neuroflyer/src/screens/analysis.cpp`, add the include at the top:

```cpp
#include <neuroflyer/components/structural_heatmap.h>
```

Replace the placeholder in the chart switch case:

```cpp
    case AnalysisChart::StructuralHeatmap:
        // Placeholder — Task 3 fills this in
        ImGui::Text("Structural Heatmap (placeholder)");
        break;
```

With:

```cpp
    case AnalysisChart::StructuralHeatmap:
        draw_structural_heatmap(analysis.heatmap, structural_history,
                                renderer.renderer_);
        break;
```

- [ ] **Step 5: Mark heatmap dirty when new generation data arrives**

In `neuroflyer/src/screens/analysis.cpp`, at the top of `draw_analysis()`, after the `ImGui::Begin()` call, add:

```cpp
    // Mark heatmap dirty if new data arrived
    if (structural_history.size() != analysis.heatmap.cumulative.size()) {
        analysis.heatmap.dirty = true;
    }
```

Note: This is an approximation — `cumulative.size()` is the number of unique topology bins, not the number of generations. A more precise approach: track `last_gen_count` in `AnalysisState`. Actually, let's do that properly. Add a field to `AnalysisState` in `analysis.h`:

```cpp
struct AnalysisState {
    AnalysisChart active_chart = AnalysisChart::StructuralHeatmap;
    StructuralHeatmapState heatmap;
    bool wants_back = false;
    std::size_t last_history_size = 0;  // track when new data arrives
};
```

Then the dirty check becomes:

```cpp
    if (structural_history.size() != analysis.last_history_size) {
        analysis.heatmap.dirty = true;
        analysis.last_history_size = structural_history.size();
    }
```

- [ ] **Step 6: Clean up texture on back**

In `neuroflyer/src/main.cpp`, in the `Analysis` case, when `wants_back` is true, destroy the texture:

```cpp
        case S::Analysis: {
            static neuroflyer::AnalysisState s_analysis;
            neuroflyer::draw_analysis(s_analysis,
                neuroflyer::get_fly_session_state().structural_history,
                state, renderer);
            if (s_analysis.wants_back) {
                s_analysis.wants_back = false;
                s_analysis.heatmap.destroy();
                go_to_screen(state, Screen::PauseConfig);
            }
            break;
        }
```

- [ ] **Step 7: Build and verify**

Run: `cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 8: Commit**

```bash
git add neuroflyer/include/neuroflyer/components/structural_heatmap.h \
       neuroflyer/src/components/structural_heatmap.cpp \
       neuroflyer/include/neuroflyer/screens/analysis.h \
       neuroflyer/src/screens/analysis.cpp \
       neuroflyer/src/main.cpp \
       neuroflyer/CMakeLists.txt
git commit -m "feat(neuroflyer): structural heatmap with SDL texture and bilinear interpolation"
```

---

### Task 4: Manual smoke test

- [ ] **Step 1: Build and launch**

Run: `cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu) && ./build/neuroflyer/neuroflyer`

- [ ] **Step 2: Run a few generations**

1. Open a genome, start a fly session
2. Let it run for 10-20 generations (use speed 4 / 100x to go fast)
3. Hit Space to pause

- [ ] **Step 3: Open analysis screen**

1. Click "Analysis" button on pause screen
2. Verify the structural heatmap appears with:
   - Smooth purple-to-orange blob showing topology density
   - Y-axis labels (hidden layer counts)
   - X-axis labels (total hidden nodes)
   - Color legend (High/Low)
   - Sidebar with generation count and most common topology

- [ ] **Step 4: Test navigation**

1. Click "Back" — should return to pause screen
2. Go back to "Analysis" — heatmap should still show data
3. Resume flight, run more generations, pause again, open Analysis — heatmap should show updated data

- [ ] **Step 5: Commit any fixes**

If issues found, fix and commit with descriptive messages.

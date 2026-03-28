#include <neuroflyer/components/structural_heatmap.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuroflyer {

namespace {

/// Map a normalized [0,1] value to a color: dark -> purple -> orange.
void intensity_to_rgba(float t, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.001f) {
        // Background
        r = 20; g = 20; b = 30; a = 255;
        return;
    }
    // Purple (80,40,120) at low -> Orange (255,142,60) at high
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

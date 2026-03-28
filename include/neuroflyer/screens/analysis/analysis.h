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
    std::size_t last_history_size = 0;
};

void draw_analysis(AnalysisState& analysis,
                   const std::vector<StructuralHistogram>& structural_history,
                   AppState& state,
                   Renderer& renderer,
                   bool as_tab = false);

} // namespace neuroflyer

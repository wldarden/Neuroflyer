#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/screens/analysis/analysis.h>

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

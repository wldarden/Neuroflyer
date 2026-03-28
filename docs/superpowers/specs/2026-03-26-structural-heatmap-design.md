# Structural Heatmap Analysis Screen

**Date:** 2026-03-26
**Scope:** NeuroFlyer — new analysis screen with structural heatmap chart component

## Summary

Add an analysis screen accessible from the pause screen during flight. Its first chart component is a cumulative structural heatmap — a 2D density visualization showing what network topologies the population has explored across all generations. Rendered as a smooth SDL texture with bilinear interpolation, displayed via `ImGui::Image()`.

## Data Collection

At each generation end (after `do_evolution` completes), scan the population and record a histogram of `(hidden_layer_count, total_hidden_nodes)` pairs.

**Struct** (defined in `evolution.h`):

```cpp
struct StructuralHistogram {
    std::map<std::pair<int,int>, int> bins;
    // Key: (layer_count, node_count), Value: count of individuals
};
```

**Storage**: `std::vector<StructuralHistogram> structural_history` added to `FlySessionState`, one entry per generation.

**Recording**: At generation end, iterate `population`, extract `topology.layers.size()` (excluding output layer = hidden layer count) and sum of `layer.output_size` for hidden layers (total hidden nodes). Increment the corresponding bin.

**Extensibility**: The axis system is designed so future axis parameters (avg layer nodes, total weights, avg sensor widths, avg sensor range, avg sensor angle, points scored) can be added by recording additional per-individual metrics in the histogram or a parallel data structure.

## Heatmap Rendering

SDL texture rendered at a fixed resolution (256x256), updated when the analysis screen opens or new generation data arrives.

**Pipeline:**

1. **Accumulate**: Sum all per-generation histograms into a single cumulative grid: `cumulative[layers][nodes] = total count`.
2. **Normalize**: Divide by the max bin value to get [0.0, 1.0] intensity.
3. **Interpolate**: Map discrete bins to pixel coordinates. Use bilinear interpolation between neighboring bins to produce a smooth blob effect rather than blocky cells.
4. **Color ramp**: 0.0 = transparent/dark background → purple midtones → hot orange at 1.0. Matches the NeuroFlyer visual aesthetic.
5. **Write to texture**: `SDL_UpdateTexture` with the computed pixel data.
6. **Display**: `ImGui::Image(texture, size)` in the chart area.

**Axis labels and overlay**: ImGui renders axis labels, tick marks, and color legend on top of the texture. The texture handles the heatmap, ImGui handles the chrome.

**Axis ranges**: Auto-computed from the data — min/max hidden layers and hidden nodes observed across all generations. Dynamically adjusts as evolution explores new topologies.

**Texture lifecycle**: Created when the analysis screen opens, destroyed on close. Re-rendered when dirty flag is set (new generation completed while screen is open).

## Analysis Screen (Page)

A full-screen component that hosts chart components one at a time. Designed as a shell — the screen itself is thin, the chart components do the real work.

**State** (`AnalysisState`):
- Active chart selector (enum or index)
- Back signal (bool, returns to pause screen)
- Per-chart state (each chart component owns its own state struct)

**Layout**:
- Left sidebar: chart picker list (initially just "Structural History"), stats summary, color legend, "Back" button
- Main area: renders the active chart component

**Navigation**: Pause screen gets an "Analysis" button. Analysis screen has a "Back" button that returns to pause.

**Adding future charts**: Create a new component (header + cpp), register it in the analysis screen's chart list. The analysis screen renders whichever is selected. Each chart is self-contained with its own state and draw function.

## Structural Heatmap Component

Self-contained chart component with its own state and draw function.

**State** (`StructuralHeatmapState`):
- `SDL_Texture*` handle (the rendered heatmap)
- `bool dirty` — needs re-render
- Cumulative grid (2D array of normalized intensities)
- Axis bounds (min/max layers, min/max nodes — auto-computed from data)
- Texture dimensions

**Interface**:
- `draw_structural_heatmap(state, structural_history, renderer)` — computes grid if dirty, renders texture, draws ImGui overlay (axis labels, ticks, legend)
- Takes `structural_history` as read-only input from `FlySessionState`

**Stats panel content** (rendered by the analysis screen sidebar or by the component):
- Total generations recorded
- Most common topology (layers x nodes)
- Current population topology breakdown

## File Structure

**New files:**
- `neuroflyer/include/neuroflyer/screens/analysis.h` — `AnalysisState`, `draw_analysis()` declaration
- `neuroflyer/src/screens/analysis.cpp` — Screen shell: sidebar, chart routing, back navigation
- `neuroflyer/include/neuroflyer/components/structural_heatmap.h` — `StructuralHeatmapState`, `draw_structural_heatmap()` declaration
- `neuroflyer/src/components/structural_heatmap.cpp` — Grid computation, bilinear interpolation, SDL texture rendering, color ramp, ImGui overlay

**Modified files:**
- `neuroflyer/include/neuroflyer/evolution.h` — Add `StructuralHistogram` struct
- `neuroflyer/src/screens/fly_session.cpp` — Record histogram at generation end, add `structural_history` to `FlySessionState`
- `neuroflyer/src/screens/pause_config.cpp` — "Analysis" button
- Screen manager (main.cpp or equivalent) — Route to/from analysis screen
- `neuroflyer/CMakeLists.txt` — Add new source files

## V1 Axes

- **Y-axis**: Hidden layer count (integer, typically 1-6)
- **X-axis**: Total hidden nodes (integer, sum of all hidden layer output_sizes)

## Future Axis Options (not in scope for v1)

- Average layer node count
- Total weight count
- Average sensor width/range/angle
- Points scored

#pragma once

#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>

#include <neuralnet/network.h>
#include <neuralnet-ui/render_neural_net.h>

#include <cstdint>
#include <vector>

struct SDL_Renderer;

namespace neuroflyer {

struct VariantNetConfig {
    const ShipDesign& ship_design;
    const neuralnet::Network& network;
    SDL_Renderer* renderer;  // Non-owning; SDL manages lifetime
    int x, y, w, h;

    std::vector<float> input_values;
    int mouse_x = -1;
    int mouse_y = -1;

    NetType net_type = NetType::Solo;
};

/// Build a NetRenderConfig from a VariantNetConfig without rendering.
/// Use this for deferred rendering (queue the config, render later).
[[nodiscard]] neuralnet_ui::NetRenderConfig build_variant_net_config(
    const VariantNetConfig& config);

/// Build input node colors for solo/scroller nets from a ShipDesign.
/// Green=sight, Purple=sensor, Blue=system, Red=memory.
/// Presentation-only — uses theme colors. Not needed by engine code.
[[nodiscard]] std::vector<NodeStyle> build_input_colors(const ShipDesign& design);

/// Build input node colors for arena fighter nets from a ShipDesign.
/// Green=sight, Purple=sensor, Yellow=squad inputs, Red=memory.
[[nodiscard]] std::vector<NodeStyle> build_arena_fighter_input_colors(const ShipDesign& design);

} // namespace neuroflyer

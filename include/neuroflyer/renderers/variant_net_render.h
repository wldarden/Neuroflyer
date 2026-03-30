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
    SDL_Renderer* renderer;
    int x, y, w, h;

    std::vector<float> input_values;
    int mouse_x = -1;
    int mouse_y = -1;

    NetType net_type = NetType::Solo;
};

/// Render a neural net with NeuroFlyer-specific labels, colors, and ordering.
/// Calls build_input_labels/colors/order from ship_design, builds output labels,
/// then delegates to neuralnet_ui::render_neural_net.
/// Returns hover/click info for node interaction.
[[nodiscard]] neuralnet_ui::NetRenderResult render_variant_net(const VariantNetConfig& config);

/// Build a NetRenderConfig from a VariantNetConfig without rendering.
/// Use this for deferred rendering (queue the config, render later).
[[nodiscard]] neuralnet_ui::NetRenderConfig build_variant_net_config(
    const VariantNetConfig& config);

} // namespace neuroflyer

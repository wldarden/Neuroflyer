#include <neuroflyer/renderers/variant_net_render.h>

#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ship_design.h>

#include <neuralnet-ui/render_neural_net.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace neuroflyer {

neuralnet_ui::NetRenderConfig build_variant_net_config(
    const VariantNetConfig& config) {

    // 1. Build NeuroFlyer-specific input metadata from the ShipDesign.
    auto labels = build_input_labels(config.ship_design);
    auto nf_colors = build_input_colors(config.ship_design);
    auto order = build_display_order(config.ship_design);

    // 2. Map NodeStyle -> neuralnet_ui::NodeColor (layout-compatible, different namespace).
    std::vector<neuralnet_ui::NodeColor> input_colors;
    input_colors.reserve(nf_colors.size());
    for (const auto& c : nf_colors) {
        input_colors.push_back({c.r, c.g, c.b});
    }

    // 3. Build output labels: first ACTION_COUNT are actions, rest are memory.
    std::size_t output_size = config.network.output_size();
    std::vector<std::string> output_labels;
    output_labels.reserve(output_size);

    const char* action_names[] = {"UP", "DN", "LF", "RT", "SH"};
    for (std::size_t i = 0; i < ACTION_COUNT && i < output_size; ++i) {
        output_labels.push_back(action_names[i]);
    }
    for (std::size_t i = ACTION_COUNT; i < output_size; ++i) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "M%X",
                      static_cast<unsigned>(i - ACTION_COUNT));
        output_labels.push_back(buf);
    }

    // 4. Build output colors: green for actions, purple for memory.
    std::vector<neuralnet_ui::NodeColor> output_colors;
    output_colors.reserve(output_size);
    for (std::size_t i = 0; i < output_size; ++i) {
        if (i < ACTION_COUNT) {
            output_colors.push_back({0, 200, 110});  // green
        } else {
            output_colors.push_back({180, 120, 220}); // purple
        }
    }

    // 5. Assemble the generic NetRenderConfig.
    neuralnet_ui::NetRenderConfig result;
    result.renderer = config.renderer;
    result.network = &config.network;
    result.x = config.x;
    result.y = config.y;
    result.w = config.w;
    result.h = config.h;
    result.input_values = config.input_values;
    result.input_labels = std::move(labels);
    result.input_colors = std::move(input_colors);
    result.display_order = std::move(order);
    result.output_labels = std::move(output_labels);
    result.output_colors = std::move(output_colors);
    result.mouse_x = config.mouse_x;
    result.mouse_y = config.mouse_y;

    return result;
}

neuralnet_ui::NetRenderResult render_variant_net(const VariantNetConfig& config) {
    auto render_config = build_variant_net_config(config);
    return neuralnet_ui::render_neural_net(render_config);
}

} // namespace neuroflyer

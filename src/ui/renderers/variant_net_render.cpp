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

    // 1. Build input metadata based on net type.
    std::vector<std::string> labels;
    std::vector<neuralnet_ui::NodeColor> input_colors;
    std::vector<std::size_t> order;

    if (config.net_type == NetType::SquadLeader) {
        // Squad leader: fixed strategic inputs, no sensors.
        labels = build_squad_leader_input_labels();
        input_colors.reserve(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            input_colors.push_back({249, 202, 36});  // yellow (squad color)
        }
        // Identity order — no reordering needed for squad leader inputs.
        order.resize(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            order[i] = i;
        }
    } else {
        // Solo / Fighter: sensor-based labels from ShipDesign.
        labels = build_input_labels(config.ship_design);
        auto nf_colors = build_input_colors(config.ship_design);
        input_colors.reserve(nf_colors.size());
        for (const auto& c : nf_colors) {
            input_colors.push_back({c.r, c.g, c.b});
        }
        order = build_display_order(config.ship_design);
    }

    // 2. Build output labels based on net type.
    std::size_t output_size = config.network.output_size();
    std::vector<std::string> output_labels;
    output_labels.reserve(output_size);

    if (config.net_type == NetType::SquadLeader) {
        auto squad_outputs = build_squad_leader_output_labels();
        for (std::size_t i = 0; i < output_size && i < squad_outputs.size(); ++i) {
            output_labels.push_back(squad_outputs[i]);
        }
        // Any extra outputs beyond the expected 5 get generic labels.
        for (std::size_t i = squad_outputs.size(); i < output_size; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "O%zu", i);
            output_labels.push_back(buf);
        }
    } else {
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
    }

    // 3. Build output colors: green for actions/orders, purple for memory.
    std::vector<neuralnet_ui::NodeColor> output_colors;
    output_colors.reserve(output_size);
    if (config.net_type == NetType::SquadLeader) {
        // All squad leader outputs are tactical orders — use yellow.
        for (std::size_t i = 0; i < output_size; ++i) {
            output_colors.push_back({249, 202, 36});  // yellow (squad color)
        }
    } else {
        for (std::size_t i = 0; i < output_size; ++i) {
            if (i < ACTION_COUNT) {
                output_colors.push_back({0, 200, 110});  // green
            } else {
                output_colors.push_back({180, 120, 220}); // purple
            }
        }
    }

    // 4. Assemble the generic NetRenderConfig.
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

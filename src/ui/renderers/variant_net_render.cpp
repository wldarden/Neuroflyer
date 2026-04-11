#include <neuroflyer/renderers/variant_net_render.h>

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/ui/theme.h>

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

    switch (config.net_type) {
    case NetType::SquadLeader: {
        // Squad leader: fixed strategic inputs, no sensors.
        labels = build_squad_leader_input_labels();
        input_colors.reserve(labels.size());
        // Inputs 0-7: squad state (yellow), 8-12: NTM-derived (red), 13-16: battlefield state (yellow)
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (i >= 8 && i <= 12) {
                input_colors.push_back({255, 100, 100});  // red (NTM-derived)
            } else {
                input_colors.push_back({249, 202, 36});   // yellow (squad state)
            }
        }
        // Identity order — no reordering needed for squad leader inputs.
        order.resize(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) {
            order[i] = i;
        }
        break;
    }
    case NetType::Fighter: {
        // Arena fighter: sensor labels + squad leader inputs + memory.
        labels = build_arena_fighter_input_labels(config.ship_design);
        auto nf_colors = build_arena_fighter_input_colors(config.ship_design);
        input_colors.reserve(nf_colors.size());
        for (const auto& c : nf_colors) {
            input_colors.push_back({c.r, c.g, c.b});
        }
        order = build_arena_fighter_display_order(config.ship_design);
        break;
    }
    case NetType::NTM: {
        // NTM: minimal generic labels (threat features -> threat score).
        // NTM nets are small (7 inputs -> 1 output); use generic indexed labels.
        std::size_t input_count = config.network.input_size();
        labels.reserve(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "TF%zu", i);
            labels.push_back(buf);
        }
        input_colors.reserve(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            input_colors.push_back({255, 100, 100});  // red (threat color)
        }
        order.resize(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            order[i] = i;
        }
        break;
    }
    case NetType::Solo: {
        // Solo: sensor-based labels from ShipDesign.
        labels = build_input_labels(config.ship_design);
        auto nf_colors = build_input_colors(config.ship_design);
        input_colors.reserve(nf_colors.size());
        for (const auto& c : nf_colors) {
            input_colors.push_back({c.r, c.g, c.b});
        }
        order = build_display_order(config.ship_design);
        break;
    }
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
    } else if (config.net_type == NetType::NTM) {
        // NTM: single threat score output.
        if (output_size >= 1) output_labels.push_back("Threat");
        for (std::size_t i = 1; i < output_size; ++i) {
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
    } else if (config.net_type == NetType::NTM) {
        // NTM outputs are threat scores — use red.
        for (std::size_t i = 0; i < output_size; ++i) {
            output_colors.push_back({255, 100, 100});  // red (threat color)
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

std::vector<NodeStyle> build_input_colors(const ShipDesign& design) {
    std::vector<NodeStyle> colors;

    auto ns = [](const theme::Color& c) -> NodeStyle { return {c.r, c.g, c.b}; };

    for (const auto& s : design.sensors) {
        if (!s.is_full_sensor) {
            colors.push_back(ns(theme::node_sight));
        } else {
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
        }
    }

    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));

    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        colors.push_back(ns(theme::node_memory));
    }

    return colors;
}

std::vector<NodeStyle> build_arena_fighter_input_colors(const ShipDesign& design) {
    std::vector<NodeStyle> colors;

    auto ns = [](const theme::Color& c) -> NodeStyle { return {c.r, c.g, c.b}; };

    for (const auto& s : design.sensors) {
        if (!s.is_full_sensor) {
            colors.push_back(ns(theme::node_sight));
        } else {
            for (int j = 0; j < 5; ++j) {
                colors.push_back(ns(theme::node_sensor));
            }
        }
    }

    for (std::size_t i = 0; i < ArenaConfig::squad_leader_fighter_inputs; ++i) {
        colors.push_back({220, 180, 40});  // squad leader yellow
    }

    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        colors.push_back(ns(theme::node_memory));
    }

    return colors;
}

} // namespace neuroflyer

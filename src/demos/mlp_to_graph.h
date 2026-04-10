// src/demos/mlp_to_graph.h
#pragma once

#include <neuroflyer/snapshot.h>

#include <neuralnet/neural_node_props.h>
#include <evolve/graph_gene.h>

#include <random>

/// Extract hidden layer sizes from a snapshot topology (all layers except last).
inline std::vector<std::size_t> extract_hidden_sizes(
    const neuralnet::NetworkTopology& topo) {
    std::vector<std::size_t> sizes;
    if (topo.layers.size() > 1) {
        for (std::size_t l = 0; l + 1 < topo.layers.size(); ++l) {
            sizes.push_back(topo.layers[l].output_size);
        }
    }
    return sizes;
}

/// Build a dense NeuralGenome with the given layer dimensions.
/// Weights are randomized (this is for performance benchmarking, not behavior
/// cloning). Activation functions default to Tanh for all non-input nodes.
inline neuralnet::NeuralGenome build_dense_graph_genome(
    std::size_t num_inputs,
    const std::vector<std::size_t>& hidden_sizes,
    std::size_t num_outputs,
    std::mt19937& rng) {

    neuralnet::NeuralGenome genome;
    std::uniform_real_distribution<float> weight_dist(-1.0f, 1.0f);

    uint32_t node_id = 0;
    uint32_t innovation = 0;

    struct LayerRange { uint32_t first; uint32_t count; };
    std::vector<LayerRange> layer_ranges;

    // Input nodes
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_inputs)});
    for (std::size_t i = 0; i < num_inputs; ++i) {
        genome.nodes.push_back({
            .id = node_id++,
            .role = evolve::NodeRole::Input,
            .props = neuralnet::NeuralNodeProps{}
        });
    }

    // Hidden layers
    for (auto hs : hidden_sizes) {
        layer_ranges.push_back({node_id, static_cast<uint32_t>(hs)});
        for (std::size_t n = 0; n < hs; ++n) {
            genome.nodes.push_back({
                .id = node_id++,
                .role = evolve::NodeRole::Hidden,
                .props = neuralnet::NeuralNodeProps{
                    .activation = neuralnet::Activation::Tanh,
                    .type = neuralnet::NodeType::Stateless,
                    .bias = 0.0f,
                    .tau = 1.0f
                }
            });
        }
    }

    // Output layer
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_outputs)});
    for (std::size_t n = 0; n < num_outputs; ++n) {
        genome.nodes.push_back({
            .id = node_id++,
            .role = evolve::NodeRole::Output,
            .props = neuralnet::NeuralNodeProps{
                .activation = neuralnet::Activation::Tanh,
                .type = neuralnet::NodeType::Stateless,
                .bias = 0.0f,
                .tau = 1.0f
            }
        });
    }

    // Dense connections between adjacent layers
    for (std::size_t l = 0; l + 1 < layer_ranges.size(); ++l) {
        const auto& from_range = layer_ranges[l];
        const auto& to_range = layer_ranges[l + 1];
        for (uint32_t f = 0; f < from_range.count; ++f) {
            for (uint32_t t = 0; t < to_range.count; ++t) {
                genome.connections.push_back({
                    .from_node = from_range.first + f,
                    .to_node = to_range.first + t,
                    .weight = weight_dist(rng),
                    .enabled = true,
                    .innovation = innovation++
                });
            }
        }
    }

    return genome;
}

/// Convert an MLP snapshot's topology into an equivalent dense NeuralGenome.
/// Uses the snapshot's exact dimensions (input_size, layer sizes).
inline neuralnet::NeuralGenome mlp_snapshot_to_graph_genome(
    const neuroflyer::Snapshot& snap, std::mt19937& rng) {

    auto hidden = extract_hidden_sizes(snap.topology);
    std::size_t output_size = snap.topology.layers.empty()
        ? 0 : snap.topology.layers.back().output_size;
    return build_dense_graph_genome(snap.topology.input_size, hidden, output_size, rng);
}

/// Convert an MLP snapshot into a dense NeuralGenome, preserving actual weights,
/// biases, and per-node activations from the snapshot.
///
/// The snap.weights flat vector layout is:
///   [L0_weights (prev*out)][L0_biases (out)][L1_weights (prev*out)][L1_biases (out)]...
///
/// MLP weight layout is row-major: weight[output_node * input_count + input_node].
/// Graph connection (from_id, to_id) maps to MLP weight[to_local * prev_layer_size + from_local].
///
/// Optional target_input_size / target_output_size: if larger than the snapshot
/// dimensions, extra nodes get zero-weight connections (the MLP weights for the
/// original nodes are still preserved in their original positions).
inline neuralnet::NeuralGenome mlp_snapshot_to_graph_genome_with_weights(
    const neuroflyer::Snapshot& snap,
    std::size_t target_input_size = 0,
    std::size_t target_output_size = 0) {

    const auto& topo = snap.topology;
    std::size_t snap_input_size = topo.input_size;
    std::size_t snap_output_size = topo.layers.empty() ? 0 : topo.layers.back().output_size;

    // Use target sizes if specified and larger; otherwise use snapshot sizes.
    std::size_t num_inputs  = (target_input_size  > snap_input_size)  ? target_input_size  : snap_input_size;
    std::size_t num_outputs = (target_output_size > snap_output_size) ? target_output_size : snap_output_size;

    // Build layer sizes: hidden layers come from all but the last topology layer.
    auto hidden_sizes = extract_hidden_sizes(topo);

    neuralnet::NeuralGenome genome;
    uint32_t node_id = 0;
    uint32_t innovation = 0;

    struct LayerRange { uint32_t first; uint32_t count; };
    std::vector<LayerRange> layer_ranges;

    // Input nodes (no activation / bias needed for inputs).
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_inputs)});
    for (std::size_t i = 0; i < num_inputs; ++i) {
        genome.nodes.push_back({
            .id = node_id++,
            .role = evolve::NodeRole::Input,
            .props = neuralnet::NeuralNodeProps{}
        });
    }

    // Hidden layers — copy activations and biases from topology.
    for (std::size_t l = 0; l < hidden_sizes.size(); ++l) {
        std::size_t hs = hidden_sizes[l];
        layer_ranges.push_back({node_id, static_cast<uint32_t>(hs)});
        const auto& layer_def = topo.layers[l];
        for (std::size_t n = 0; n < hs; ++n) {
            // Per-node activation override if available, else layer default.
            neuralnet::Activation act = layer_def.node_activations.size() > n
                ? layer_def.node_activations[n]
                : layer_def.activation;
            genome.nodes.push_back({
                .id = node_id++,
                .role = evolve::NodeRole::Hidden,
                .props = neuralnet::NeuralNodeProps{
                    .activation = act,
                    .type = neuralnet::NodeType::Stateless,
                    .bias = 0.0f,  // biases filled in after connections
                    .tau = 1.0f
                }
            });
        }
    }

    // Output layer — copy activations and biases from last topology layer.
    layer_ranges.push_back({node_id, static_cast<uint32_t>(num_outputs)});
    {
        const auto& out_def = topo.layers.back();
        for (std::size_t n = 0; n < num_outputs; ++n) {
            neuralnet::Activation act = out_def.node_activations.size() > n
                ? out_def.node_activations[n]
                : out_def.activation;
            genome.nodes.push_back({
                .id = node_id++,
                .role = evolve::NodeRole::Output,
                .props = neuralnet::NeuralNodeProps{
                    .activation = act,
                    .type = neuralnet::NodeType::Stateless,
                    .bias = 0.0f,  // biases filled in below
                    .tau = 1.0f
                }
            });
        }
    }

    // Walk through snap.weights to build connections with actual weights and set biases.
    // Layer indices in layer_ranges: 0 = inputs, 1..H = hidden, H+1 = outputs.
    // Snap layers: 0..H-1 = hidden, H = output. layer_ranges[l+1] corresponds to snap topo layer l.
    std::size_t weight_offset = 0;
    for (std::size_t l = 0; l < topo.layers.size(); ++l) {
        const LayerRange& from_range = layer_ranges[l];      // previous layer (inputs or hidden)
        const LayerRange& to_range   = layer_ranges[l + 1];  // current layer (hidden or output)

        // Snap layer l has prev_size inputs from the MLP's perspective.
        // For l==0: prev_size = snap_input_size; else: snap hidden[l-1].
        std::size_t snap_prev = (l == 0) ? snap_input_size : topo.layers[l - 1].output_size;
        std::size_t snap_curr = topo.layers[l].output_size;

        // Number of weights = snap_prev * snap_curr; biases = snap_curr.
        std::size_t num_weights_in_layer = snap_prev * snap_curr;
        std::size_t num_biases_in_layer  = snap_curr;

        // Slice the weights and biases out of snap.weights.
        const float* w_ptr = (weight_offset < snap.weights.size())
            ? snap.weights.data() + weight_offset : nullptr;
        const float* b_ptr = (weight_offset + num_weights_in_layer < snap.weights.size())
            ? snap.weights.data() + weight_offset + num_weights_in_layer : nullptr;

        // Generate dense connections: all from_range nodes -> all to_range nodes.
        // We use the graph (to_local * snap_prev + from_local) index for the actual weight.
        // Nodes beyond snap_prev or snap_curr get zero weights.
        for (uint32_t t = 0; t < to_range.count; ++t) {
            for (uint32_t f = 0; f < from_range.count; ++f) {
                float weight = 0.0f;
                // Only copy if both from and to indices are within the original MLP dimensions.
                if (f < static_cast<uint32_t>(snap_prev) &&
                    t < static_cast<uint32_t>(snap_curr) &&
                    w_ptr != nullptr) {
                    weight = w_ptr[t * snap_prev + f];
                }
                genome.connections.push_back({
                    .from_node = from_range.first + f,
                    .to_node   = to_range.first + t,
                    .weight    = weight,
                    .enabled   = true,
                    .innovation = innovation++
                });
            }
        }

        // Copy biases into node props for the to_range nodes.
        if (b_ptr != nullptr) {
            for (uint32_t t = 0; t < static_cast<uint32_t>(snap_curr) && t < to_range.count; ++t) {
                // to_range nodes start at index (to_range.first) in genome.nodes.
                // genome.nodes layout: inputs first, then hidden/output in order.
                // to_range.first is the absolute node_id; nodes are added in id order.
                genome.nodes[to_range.first + t].props.bias = b_ptr[t];
            }
        }

        weight_offset += num_weights_in_layer + num_biases_in_layer;
    }

    return genome;
}

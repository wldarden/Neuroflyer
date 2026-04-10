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

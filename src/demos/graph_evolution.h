// src/demos/graph_evolution.h
//
// Evolution infrastructure for the GraphNetwork skirmish demo.
// Uses NEAT structural mutations (add_node, add_connection) directly —
// no speciation, simple tournament selection.
#pragma once

#include <neuralnet/graph_network.h>
#include <neuralnet/neural_node_props.h>
#include <neuralnet/neural_neat_policy.h>
#include <neuralnet/serialization.h>
#include <evolve/graph_gene.h>
#include <evolve/innovation.h>
#include <evolve/neat_operators.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <variant>
#include <vector>

namespace graph_demo {

// ── Config ────────────────────────────────────────────────────────────────────

struct EvolutionConfig {
    std::size_t elite_count           = 3;
    std::size_t tournament_size       = 3;
    float add_connection_rate         = 0.05f;
    float add_node_rate               = 0.03f;
    float disable_connection_rate     = 0.01f;
    float weight_mutate_rate          = 0.80f;
    float weight_perturb_strength     = 0.3f;
    float bias_mutate_rate            = 0.40f;
    float bias_perturb_strength       = 0.2f;
    float activation_mutate_rate      = 0.05f;
    int   save_interval               = 50;
};

// ── Population ────────────────────────────────────────────────────────────────

struct FighterPopulation {
    std::vector<neuralnet::NeuralGenome> genomes;
    std::vector<float>                  fitness;

    /// Assign n copies of seed and zero out fitness.
    void resize(std::size_t n, const neuralnet::NeuralGenome& seed) {
        genomes.assign(n, seed);
        fitness.assign(n, 0.0f);
    }

    /// Zero all fitness values for the next generation.
    void clear_fitness() {
        std::fill(fitness.begin(), fitness.end(), 0.0f);
    }
};

// ── Selection ─────────────────────────────────────────────────────────────────

/// Tournament selection: pick tournament_size random individuals, return the
/// index of the one with the highest fitness.
[[nodiscard]] inline std::size_t tournament_select(
    const FighterPopulation& pop,
    std::size_t tournament_size,
    std::mt19937& rng) {

    std::uniform_int_distribution<std::size_t> dist(0, pop.genomes.size() - 1);
    std::size_t best = dist(rng);
    for (std::size_t i = 1; i < tournament_size; ++i) {
        std::size_t candidate = dist(rng);
        if (pop.fitness[candidate] > pop.fitness[best]) {
            best = candidate;
        }
    }
    return best;
}

// ── Mutation ──────────────────────────────────────────────────────────────────

/// Apply NEAT mutations to a single genome.
inline void mutate_genome(
    neuralnet::NeuralGenome& genome,
    evolve::InnovationCounter& innovation,
    const evolve::NeatPolicy<neuralnet::NeuralNodeProps>& policy,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    evolve::NeatWeightConfig wc;
    wc.weight_mutate_rate      = config.weight_mutate_rate;
    wc.weight_perturb_strength = config.weight_perturb_strength;
    // weight_perturb_rate and weight_replace_range use library defaults

    neuralnet::NeuralMutationConfig nmc;
    nmc.bias_mutate_rate       = config.bias_mutate_rate;
    nmc.bias_perturb_strength  = config.bias_perturb_strength;
    nmc.activation_mutate_rate = config.activation_mutate_rate;

    // Weight perturbation — always applied
    evolve::mutate_weights(genome, wc, rng);

    // Structural mutations — applied probabilistically
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (chance(rng) < config.add_connection_rate) {
        evolve::add_connection(genome, innovation, rng);
    }
    if (chance(rng) < config.add_node_rate) {
        evolve::add_node(genome, innovation, policy, rng);
    }
    if (chance(rng) < config.disable_connection_rate) {
        evolve::disable_connection(genome, rng);
    }

    // Neural-property mutations (bias, activation)
    neuralnet::mutate_biases(genome, nmc, rng);
    neuralnet::mutate_activations(genome, nmc, rng);
}

// ── Evolution step ────────────────────────────────────────────────────────────

/// Evolve pop in-place:
///   1. Sort by fitness descending.
///   2. Preserve top elite_count unchanged.
///   3. Fill remaining slots via tournament selection + mutation.
///   4. Clear fitness for the next generation.
inline void evolve_fighters(
    FighterPopulation& pop,
    evolve::InnovationCounter& innovation,
    const evolve::NeatPolicy<neuralnet::NeuralNodeProps>& policy,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    const std::size_t n = pop.genomes.size();
    if (n == 0) return;

    // Sort indices by fitness descending
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) {
            return pop.fitness[a] > pop.fitness[b];
        });

    // Build next generation in a temporary buffer
    std::vector<neuralnet::NeuralGenome> next(n);

    const std::size_t elite_count = std::min(config.elite_count, n);
    for (std::size_t i = 0; i < elite_count; ++i) {
        next[i] = pop.genomes[order[i]];
    }

    // Advance the innovation epoch before mutating offspring
    innovation.new_generation();

    for (std::size_t i = elite_count; i < n; ++i) {
        const std::size_t parent = tournament_select(pop, config.tournament_size, rng);
        next[i] = pop.genomes[parent];
        mutate_genome(next[i], innovation, policy, config, rng);
    }

    pop.genomes = std::move(next);
    pop.clear_fitness();
}

// ── Serialization helpers ─────────────────────────────────────────────────────

/// Save a NeuralGenome to a file by wrapping it in a GraphNetwork.
inline void save_genome(const neuralnet::NeuralGenome& genome, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "save_genome: failed to open '" << path << "' for writing\n";
        return;
    }
    neuralnet::GraphNetwork net(genome);
    neuralnet::save(net, out);
}

/// Load a NeuralGenome from a file saved by save_genome.
[[nodiscard]] inline neuralnet::NeuralGenome load_genome(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("load_genome: failed to open '" + path + "'");
    }
    auto loaded = neuralnet::load(in);
    return std::get<neuralnet::GraphNetwork>(loaded).genome();
}

} // namespace graph_demo

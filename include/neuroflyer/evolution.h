#pragma once

#include <evolve/genome.h>
#include <evolve/structured_genome.h>
#include <neuralnet/network.h>
#include <neuroflyer/snapshot.h>

#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace neuroflyer {

struct EvolutionConfig {
    std::size_t population_size = 100;
    std::size_t elitism_count = 3;
    std::size_t tournament_size = 5;
    float weight_mutation_rate = 0.1f;
    float weight_mutation_strength = 0.3f;
    float add_node_chance = 0.01f;
    float remove_node_chance = 0.005f;
    float add_layer_chance = 0.002f;
    float remove_layer_chance = 0.002f;
};

/// Per-generation histogram of (hidden_layer_count, total_hidden_nodes) pairs.
struct StructuralHistogram {
    std::map<std::pair<int,int>, int> bins;
};

struct Individual {
    neuralnet::NetworkTopology topology;
    evolve::StructuredGenome genome;
    float fitness = 0.0f;

    /// Create a random individual from a ShipDesign + hidden layer sizes.
    static Individual from_design(const ShipDesign& design,
                                   const std::vector<std::size_t>& hidden_layers,
                                   std::mt19937& rng);

    /// Create a random individual (legacy, for backward compat).
    static Individual random(std::size_t input_size,
                              std::vector<std::size_t> hidden_layers,
                              std::size_t output_size,
                              std::mt19937& rng);

    /// Build a usable Network from this individual's topology + weight genes.
    [[nodiscard]] neuralnet::Network build_network() const;

    /// Get this individual's effective ship design (reads sensor genes).
    [[nodiscard]] ShipDesign effective_ship_design() const;
};

/// Build a StructuredGenome skeleton from a ShipDesign + NetworkTopology.
/// Weight genes are zero-initialized; caller should randomize or fill from snapshot.
[[nodiscard]] evolve::StructuredGenome build_genome_skeleton(
    const ShipDesign& design,
    const neuralnet::NetworkTopology& topology);

/// Hash an individual's weights to produce a unique ID for MRCA dedup.
[[nodiscard]] uint32_t individual_hash(const Individual& ind);

/// Convert a scroller-mode variant Individual to an arena-mode fighter.
/// Removes scroller position inputs, adds arena sensor extras and squad leader inputs.
/// New input weights are zero-initialized to preserve existing behavior.
[[nodiscard]] Individual convert_variant_to_fighter(
    const Individual& variant,
    const ShipDesign& design);

/// Check if two individuals have the same topology.
[[nodiscard]] bool same_topology(const Individual& a, const Individual& b);

/// Count the total number of weight gene values for a topology
/// (weights + biases + per-node activations).
[[nodiscard]] std::size_t count_weight_genes(const neuralnet::NetworkTopology& topo);

/// Weight mutation (delegates to evolve::mutate).
void mutate_individual(Individual& ind, const EvolutionConfig& config, std::mt19937& rng);

/// Topology mutations — modify topology and resize genome to match.
void add_node(Individual& ind, std::mt19937& rng);
void remove_node(Individual& ind, std::mt19937& rng);
void add_layer(Individual& ind, std::mt19937& rng);
void remove_layer(Individual& ind, std::mt19937& rng);

/// Apply all mutations (weight + rare topology).
void apply_mutations(Individual& ind, const EvolutionConfig& config, std::mt19937& rng);

/// Create an initial population.
[[nodiscard]] std::vector<Individual> create_population(
    std::size_t input_size,
    std::vector<std::size_t> hidden_layers,
    std::size_t output_size,
    const EvolutionConfig& config,
    std::mt19937& rng);

/// Evolve one generation. Returns next population.
[[nodiscard]] std::vector<Individual> evolve_population(
    std::vector<Individual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

/// Create a random Snapshot from a ShipDesign + hidden layer sizes.
[[nodiscard]] Snapshot create_random_snapshot(
    const std::string& name,
    const ShipDesign& design,
    const std::vector<std::size_t>& hidden_layers,
    std::mt19937& rng);

/// Extract the best individual from a population as a Snapshot.
[[nodiscard]] Snapshot best_as_snapshot(
    const std::string& name,
    const std::vector<Individual>& population,
    const ShipDesign& design,
    uint32_t generation);

/// Seed a population from a Snapshot.
/// First individual = exact copy of snapshot. Rest = mutated copies.
[[nodiscard]] std::vector<Individual> create_population_from_snapshot(
    const Snapshot& snapshot,
    std::size_t population_size,
    const EvolutionConfig& config,
    std::mt19937& rng);

} // namespace neuroflyer

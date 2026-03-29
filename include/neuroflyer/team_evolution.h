#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>

#include <random>
#include <vector>

namespace neuroflyer {

struct SquadNetConfig {
    std::size_t input_size = 8;
    std::vector<std::size_t> hidden_sizes = {6};
    std::size_t output_size = 4;
};

struct TeamIndividual {
    Individual squad_individual;
    Individual fighter_individual;
    float fitness = 0.0f;

    /// Create a random team with squad net + fighter net.
    static TeamIndividual create(
        const ShipDesign& fighter_design,
        const std::vector<std::size_t>& fighter_hidden,
        const SquadNetConfig& squad_config,
        std::mt19937& rng);

    /// Build networks from individuals.
    [[nodiscard]] neuralnet::Network build_squad_network() const;
    [[nodiscard]] neuralnet::Network build_fighter_network() const;
};

/// Create initial team population.
[[nodiscard]] std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const SquadNetConfig& squad_config,
    std::size_t population_size,
    std::mt19937& rng);

/// Evolve one generation of teams. Returns next population.
[[nodiscard]] std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

} // namespace neuroflyer

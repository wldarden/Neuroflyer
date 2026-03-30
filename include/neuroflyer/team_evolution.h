#pragma once

#include <neuroflyer/evolution.h>
#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/ship_design.h>

#include <random>
#include <vector>

namespace neuroflyer {

struct NtmNetConfig {
    std::size_t input_size = 7;
    std::vector<std::size_t> hidden_sizes = {4};
    std::size_t output_size = 1;  // threat_score only
};

struct SquadLeaderNetConfig {
    std::size_t input_size = 14;
    std::vector<std::size_t> hidden_sizes = {8};
    std::size_t output_size = 5;  // 2 spacing + 3 tactical
};

struct TeamIndividual {
    Individual ntm_individual;       // Near Threat Matrix sub-net (shared weights)
    Individual squad_individual;     // Squad leader net
    Individual fighter_individual;   // Fighter net
    float fitness = 0.0f;

    /// Create a random team with NTM + squad leader + fighter nets.
    static TeamIndividual create(
        const ShipDesign& fighter_design,
        const std::vector<std::size_t>& fighter_hidden,
        const NtmNetConfig& ntm_config,
        const SquadLeaderNetConfig& leader_config,
        std::mt19937& rng);

    /// Build networks from individuals.
    [[nodiscard]] neuralnet::Network build_ntm_network() const;
    [[nodiscard]] neuralnet::Network build_squad_network() const;
    [[nodiscard]] neuralnet::Network build_fighter_network() const;
};

/// Create initial team population.
[[nodiscard]] std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::size_t population_size,
    std::mt19937& rng);

/// Evolve one generation of teams. Returns next population.
[[nodiscard]] std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

/// Evolve only squad leader + NTM nets — fighter weights are frozen.
/// Used for squad-specific training with a fixed fighter variant.
[[nodiscard]] std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng);

} // namespace neuroflyer

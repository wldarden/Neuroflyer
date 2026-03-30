#include <neuroflyer/team_evolution.h>

#include <algorithm>

namespace neuroflyer {

TeamIndividual TeamIndividual::create(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::mt19937& rng) {

    TeamIndividual team;

    // NTM sub-net
    team.ntm_individual = Individual::random(
        ntm_config.input_size,
        ntm_config.hidden_sizes,
        ntm_config.output_size,
        rng);

    // Squad leader net
    team.squad_individual = Individual::random(
        leader_config.input_size,
        leader_config.hidden_sizes,
        leader_config.output_size,
        rng);

    // Fighter net: uses arena input size with 6 squad leader inputs (not 4 broadcasts)
    std::size_t arena_input = compute_arena_input_size(
        fighter_design, ArenaConfig::squad_leader_fighter_inputs);
    std::size_t arena_output = compute_output_size(fighter_design);
    team.fighter_individual = Individual::random(
        arena_input,
        fighter_hidden,
        arena_output,
        rng);

    return team;
}

neuralnet::Network TeamIndividual::build_ntm_network() const {
    return ntm_individual.build_network();
}

neuralnet::Network TeamIndividual::build_squad_network() const {
    return squad_individual.build_network();
}

neuralnet::Network TeamIndividual::build_fighter_network() const {
    return fighter_individual.build_network();
}

std::vector<TeamIndividual> create_team_population(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const NtmNetConfig& ntm_config,
    const SquadLeaderNetConfig& leader_config,
    std::size_t population_size,
    std::mt19937& rng) {

    std::vector<TeamIndividual> pop;
    pop.reserve(population_size);
    for (std::size_t i = 0; i < population_size; ++i) {
        pop.push_back(TeamIndividual::create(
            fighter_design, fighter_hidden, ntm_config, leader_config, rng));
    }
    return pop;
}

std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
    while (next.size() < population.size()) {
        std::size_t best = dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            std::size_t candidate = dist(rng);
            if (population[candidate].fitness > population[best].fitness) {
                best = candidate;
            }
        }

        TeamIndividual child = population[best];
        child.fitness = 0.0f;

        // Mutate all three nets independently
        apply_mutations(child.ntm_individual, config, rng);
        apply_mutations(child.squad_individual, config, rng);
        apply_mutations(child.fighter_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}

std::vector<TeamIndividual> evolve_squad_only(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
    while (next.size() < population.size()) {
        std::size_t best = dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            std::size_t candidate = dist(rng);
            if (population[candidate].fitness > population[best].fitness) {
                best = candidate;
            }
        }

        TeamIndividual child = population[best];
        child.fitness = 0.0f;

        // Only mutate NTM + squad leader — fighter stays frozen
        apply_mutations(child.ntm_individual, config, rng);
        apply_mutations(child.squad_individual, config, rng);

        next.push_back(std::move(child));
    }

    return next;
}

} // namespace neuroflyer

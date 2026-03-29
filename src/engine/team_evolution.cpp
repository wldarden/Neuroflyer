#include <neuroflyer/team_evolution.h>

#include <algorithm>

namespace neuroflyer {

TeamIndividual TeamIndividual::create(
    const ShipDesign& fighter_design,
    const std::vector<std::size_t>& fighter_hidden,
    const SquadNetConfig& squad_config,
    std::mt19937& rng) {

    TeamIndividual team;

    // Squad net
    team.squad_individual = Individual::random(
        squad_config.input_size,
        squad_config.hidden_sizes,
        squad_config.output_size,
        rng);

    // Fighter net: uses arena input size (sensors + nav + broadcast + memory)
    std::size_t arena_input = compute_arena_input_size(fighter_design, squad_config.output_size);
    std::size_t arena_output = compute_output_size(fighter_design);
    team.fighter_individual = Individual::random(
        arena_input,
        fighter_hidden,
        arena_output,
        rng);

    return team;
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
    const SquadNetConfig& squad_config,
    std::size_t population_size,
    std::mt19937& rng) {

    std::vector<TeamIndividual> pop;
    pop.reserve(population_size);
    for (std::size_t i = 0; i < population_size; ++i) {
        pop.push_back(TeamIndividual::create(fighter_design, fighter_hidden, squad_config, rng));
    }
    return pop;
}

std::vector<TeamIndividual> evolve_team_population(
    std::vector<TeamIndividual>& population,
    const EvolutionConfig& config,
    std::mt19937& rng) {

    // Sort by fitness descending
    std::sort(population.begin(), population.end(),
              [](const auto& a, const auto& b) { return a.fitness > b.fitness; });

    std::vector<TeamIndividual> next;
    next.reserve(population.size());

    // Elitism: copy top N
    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    // Tournament selection + mutation for the rest
    std::uniform_int_distribution<std::size_t> dist(0, population.size() - 1);
    while (next.size() < population.size()) {
        // Tournament select parent
        std::size_t best = dist(rng);
        for (std::size_t t = 1; t < config.tournament_size; ++t) {
            std::size_t candidate = dist(rng);
            if (population[candidate].fitness > population[best].fitness) {
                best = candidate;
            }
        }

        TeamIndividual child = population[best];
        child.fitness = 0.0f;

        // Mutate both nets independently
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

    // Elitism
    for (std::size_t i = 0; i < std::min(config.elitism_count, population.size()); ++i) {
        next.push_back(population[i]);
        next.back().fitness = 0.0f;
    }

    // Tournament selection + squad-only mutation
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

        // Only mutate squad net — fighter stays frozen
        apply_mutations(child.squad_individual, config, rng);
        // DO NOT call apply_mutations on child.fighter_individual

        next.push_back(std::move(child));
    }

    return next;
}

} // namespace neuroflyer

// src/demos/graph_skirmish_demo.cpp
//
// GraphNetwork evolution demo: NEAT structural mutations on fighter nets,
// trained squad brains from MLP seeds, SDL rendering with stats overlay.

#include "mlp_to_graph.h"
#include "graph_arena_tick.h"
#include "graph_evolution.h"

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/views/arena_game_view.h>

#include <neuralnet/graph_network.h>
#include <neuralnet/neural_neat_policy.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace nf = neuroflyer;
namespace fs = std::filesystem;

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 800;

// ── Arena config ────────────────────────────────────────────────────────────
static constexpr std::size_t NUM_TEAMS = 2;
static constexpr std::size_t NUM_SQUADS = 1;
static constexpr std::size_t FIGHTERS_PER_SQUAD = 8;
static constexpr float WORLD_SIZE = 4000.0f;
static constexpr std::size_t TOWER_COUNT = 50;
static constexpr std::size_t TOKEN_COUNT = 30;

// Total fighters = NUM_TEAMS * FIGHTERS_PER_SQUAD = 16 (all in one population)
static constexpr std::size_t POP_SIZE = NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD;

// ── Scoring ─────────────────────────────────────────────────────────────────
static constexpr float KILL_POINTS = 100.0f;
static constexpr float DEATH_PENALTY = 50.0f;
static constexpr float SURVIVAL_BONUS = 20.0f;

// ── Paths ───────────────────────────────────────────────────────────────────
static const std::string SEED_DIR = "dev_ui/data/seeds";
static const std::string EVOLVED_DIR = "dev_ui/data/evolved";

// ── Match setup ─────────────────────────────────────────────────────────────

struct GraphMatch {
    nf::ArenaConfig arena_config;
    std::unique_ptr<nf::ArenaSession> arena;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_ntm;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_leader;
    std::vector<std::vector<neuralnet::GraphNetwork>> team_fighter;
    std::vector<std::vector<float>> recurrent;
    std::vector<int> ship_teams;
    std::vector<nf::ShipAssignment> assignments;
};

static GraphMatch create_match(
    const graph_demo::FighterPopulation& pop,
    const neuralnet::NeuralGenome& squad_genome,
    const neuralnet::NeuralGenome& ntm_genome,
    const nf::ShipDesign& fighter_design,
    uint32_t seed) {

    GraphMatch m;
    nf::SkirmishConfig sk;
    sk.world.world_width = WORLD_SIZE;
    sk.world.world_height = WORLD_SIZE;
    sk.world.num_teams = NUM_TEAMS;
    sk.world.num_squads = NUM_SQUADS;
    sk.world.fighters_per_squad = FIGHTERS_PER_SQUAD;
    sk.world.tower_count = TOWER_COUNT;
    sk.world.token_count = TOKEN_COUNT;

    m.arena_config.world = sk.world;
    m.arena_config.time_limit_ticks = sk.time_limit_ticks;
    m.arena_config.sector_size = sk.sector_size;
    m.arena_config.ntm_sector_radius = sk.ntm_sector_radius;

    m.arena = std::make_unique<nf::ArenaSession>(m.arena_config, seed);

    m.team_ntm.resize(NUM_TEAMS);
    m.team_leader.resize(NUM_TEAMS);
    m.team_fighter.resize(NUM_TEAMS);

    // Squad brains: same trained weights for both teams (fixed, no evolution)
    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        m.team_ntm[t].emplace_back(ntm_genome);
        m.team_leader[t].emplace_back(squad_genome);
    }

    // Fighters: first 8 genomes → team 0, next 8 → team 1
    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        std::size_t base = t * FIGHTERS_PER_SQUAD;
        for (std::size_t f = 0; f < FIGHTERS_PER_SQUAD; ++f) {
            m.team_fighter[t].emplace_back(pop.genomes[base + f]);
        }
    }

    const std::size_t total_ships = m.arena->ships().size();
    m.recurrent.assign(total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));
    m.ship_teams.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) m.ship_teams[i] = m.arena->team_of(i);

    m.assignments.resize(total_ships);
    std::vector<std::size_t> team_counters(NUM_TEAMS, 0);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto team = static_cast<std::size_t>(m.ship_teams[i]);
        const std::size_t idx = team_counters[team]++;
        m.assignments[i].team_id = team;
        m.assignments[i].squad_index = 0;
        m.assignments[i].fighter_index = idx;
    }

    return m;
}

/// Score all fighters after a match. Maps ship index → population index.
static void score_fighters(
    graph_demo::FighterPopulation& pop,
    const GraphMatch& match) {

    const auto& arena = *match.arena;
    const auto& kills = arena.enemy_kills();
    const std::size_t total_ships = arena.ships().size();

    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto team = static_cast<std::size_t>(match.ship_teams[i]);
        const auto& assign = match.assignments[i];
        std::size_t pop_idx = team * FIGHTERS_PER_SQUAD + assign.fighter_index;
        if (pop_idx >= pop.fitness.size()) continue;

        float score = 0.0f;
        score += KILL_POINTS * static_cast<float>(kills[i]);
        if (arena.ships()[i].alive) {
            score += SURVIVAL_BONUS;
        } else {
            score -= DEATH_PENALTY;
        }
        pop.fitness[pop_idx] += score;
    }
}

// ── Seed management ─────────────────────────────────────────────────────────

static void convert_and_save_seeds() {
    std::cout << "Converting MLP snapshots to GraphNetwork seeds...\n";

    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    auto squad_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin");
    auto ntm_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin");

    // Fighter: use snapshot's own input/output sizes
    auto fighter_genome = mlp_snapshot_to_graph_genome_with_weights(fighter_snap);

    // Squad leader: snapshot has 14 inputs, current code expects 17
    constexpr std::size_t SL_INPUTS = 17;
    constexpr std::size_t SL_OUTPUTS = 5;
    auto squad_genome = mlp_snapshot_to_graph_genome_with_weights(
        squad_snap, SL_INPUTS, SL_OUTPUTS);

    // NTM: 7 inputs matches current code
    auto ntm_genome = mlp_snapshot_to_graph_genome_with_weights(ntm_snap);

    fs::create_directories(SEED_DIR);
    graph_demo::save_genome(fighter_genome, SEED_DIR + "/fighter.nnpk");
    graph_demo::save_genome(squad_genome, SEED_DIR + "/squad_leader.nnpk");
    graph_demo::save_genome(ntm_genome, SEED_DIR + "/ntm.nnpk");

    std::cout << "Seeds saved to " << SEED_DIR << "/\n";
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::unitbuf;

    // Load or convert seeds
    if (!fs::exists(SEED_DIR + "/fighter.nnpk")) {
        convert_and_save_seeds();
    }

    std::cout << "Loading seeds...\n";
    auto fighter_seed = graph_demo::load_genome(SEED_DIR + "/fighter.nnpk");
    auto squad_genome = graph_demo::load_genome(SEED_DIR + "/squad_leader.nnpk");
    auto ntm_genome = graph_demo::load_genome(SEED_DIR + "/ntm.nnpk");

    // Get fighter ShipDesign from snapshot (needed for sensor queries)
    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    const auto fighter_design = fighter_snap.ship_design;

    std::cout << "Fighter seed: " << fighter_seed.nodes.size() << " nodes, "
              << fighter_seed.connections.size() << " connections\n";

    // Initialize fighter population (all start as copies of the trained seed)
    graph_demo::FighterPopulation pop;
    pop.resize(POP_SIZE, fighter_seed);

    // Check for evolved genomes to resume from
    int start_gen = 0;
    if (fs::exists(EVOLVED_DIR)) {
        int latest = -1;
        for (const auto& entry : fs::directory_iterator(EVOLVED_DIR)) {
            if (!entry.is_directory()) continue;
            auto name = entry.path().filename().string();
            if (name.substr(0, 4) == "gen_") {
                int gen = std::stoi(name.substr(4));
                if (gen > latest) latest = gen;
            }
        }
        if (latest >= 0) {
            std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(latest);
            if (fs::exists(gen_dir + "/fighter.nnpk")) {
                std::cout << "Resuming from generation " << latest << "\n";
                auto evolved_fighter = graph_demo::load_genome(gen_dir + "/fighter.nnpk");
                pop.resize(POP_SIZE, evolved_fighter);
                start_gen = latest;
            }
        }
    }

    // Evolution state
    std::mt19937 rng(std::random_device{}());
    evolve::InnovationCounter innovation;
    for (const auto& c : fighter_seed.connections) {
        innovation.get_or_create(c.from_node, c.to_node);
    }
    auto policy = neuralnet::make_neural_neat_policy(neuralnet::NeuralMutationConfig{});
    graph_demo::EvolutionConfig evo_config;

    // Apply initial mutations so not all fighters are identical
    if (start_gen == 0) {
        for (std::size_t i = evo_config.elite_count; i < POP_SIZE; ++i) {
            graph_demo::mutate_genome(pop.genomes[i], innovation, policy, evo_config, rng);
        }
    }

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    auto* window = SDL_CreateWindow("GraphNetwork Evolution",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    nf::ArenaGameView arena_view(sdl_renderer);
    arena_view.set_bounds(0, 0, WIN_W, WIN_H);

    nf::Camera camera;
    camera.x = WORLD_SIZE / 2.0f;
    camera.y = WORLD_SIZE / 2.0f;
    camera.zoom = static_cast<float>(WIN_W) / WORLD_SIZE;

    // Create first match
    int generation = start_gen;
    pop.clear_fitness();
    auto match = create_match(pop, squad_genome, ntm_genome, fighter_design, rng());

    // Stats
    float best_fitness = 0;
    float avg_fitness = 0;
    int speed = 1;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_1: speed = 1; break;
                    case SDLK_2: speed = 5; break;
                    case SDLK_3: speed = 20; break;
                    case SDLK_4: speed = 100; break;
                    case SDLK_s: {
                        std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(generation);
                        fs::create_directories(gen_dir);
                        auto best_it = std::max_element(pop.fitness.begin(), pop.fitness.end());
                        auto best_idx = static_cast<std::size_t>(std::distance(pop.fitness.begin(), best_it));
                        graph_demo::save_genome(pop.genomes[best_idx], gen_dir + "/fighter.nnpk");
                        graph_demo::save_genome(squad_genome, gen_dir + "/squad_leader.nnpk");
                        graph_demo::save_genome(ntm_genome, gen_dir + "/ntm.nnpk");
                        break;
                    }
                    default: break;
                }
            }
        }

        // Tick simulation
        for (int s = 0; s < speed; ++s) {
            if (match.arena->is_over()) {
                // Score fighters from this match
                score_fighters(pop, match);

                // Compute stats
                best_fitness = *std::max_element(pop.fitness.begin(), pop.fitness.end());
                float total = 0;
                for (auto f : pop.fitness) total += f;
                avg_fitness = total / static_cast<float>(pop.fitness.size());

                // Auto-save
                if (generation > 0 && generation % evo_config.save_interval == 0) {
                    std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(generation);
                    fs::create_directories(gen_dir);
                    auto best_it = std::max_element(pop.fitness.begin(), pop.fitness.end());
                    auto best_idx = static_cast<std::size_t>(std::distance(pop.fitness.begin(), best_it));
                    graph_demo::save_genome(pop.genomes[best_idx], gen_dir + "/fighter.nnpk");
                    graph_demo::save_genome(squad_genome, gen_dir + "/squad_leader.nnpk");
                    graph_demo::save_genome(ntm_genome, gen_dir + "/ntm.nnpk");
                }

                // Evolve
                graph_demo::evolve_fighters(pop, innovation, policy, evo_config, rng);
                generation++;

                // Start next match with evolved population
                match = create_match(pop, squad_genome, ntm_genome, fighter_design, rng());
            }

            graph_demo::graph_tick_team_arena_match(
                *match.arena, match.arena_config, fighter_design,
                match.assignments, match.team_ntm, match.team_leader,
                match.team_fighter, match.recurrent, match.ship_teams);
        }

        // Render
        SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 20, 255);
        SDL_RenderClear(sdl_renderer);
        arena_view.render(*match.arena, camera, -1, match.ship_teams);

        // ImGui overlay
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("Evolution", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

        ImGui::Text("GraphNetwork Evolution");
        ImGui::Text("%zu teams x %zu fighters, pop %zu",
            NUM_TEAMS, FIGHTERS_PER_SQUAD, POP_SIZE);
        ImGui::Separator();
        ImGui::Text("Speed: %dx  (1-4)", speed);
        ImGui::Text("Generation: %d", generation);
        ImGui::Text("Tick: %u / %u",
            match.arena->current_tick(), match.arena_config.time_limit_ticks);
        ImGui::Separator();
        ImGui::Text("Best fitness: %.1f", best_fitness);
        ImGui::Text("Avg fitness:  %.1f", avg_fitness);
        ImGui::Text("Nodes (best): %zu", pop.genomes[0].nodes.size());
        ImGui::Text("Conns (best): %zu", pop.genomes[0].connections.size());
        ImGui::Separator();
        ImGui::Text("S: save  |  Esc: quit");
        ImGui::Text("Auto-save every %d gens", evo_config.save_interval);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
    }

    // Final save
    {
        std::string gen_dir = EVOLVED_DIR + "/gen_" + std::to_string(generation);
        fs::create_directories(gen_dir);
        auto best_it = std::max_element(pop.fitness.begin(), pop.fitness.end());
        auto best_idx = static_cast<std::size_t>(std::distance(pop.fitness.begin(), best_it));
        graph_demo::save_genome(pop.genomes[best_idx], gen_dir + "/fighter.nnpk");
        graph_demo::save_genome(squad_genome, gen_dir + "/squad_leader.nnpk");
        graph_demo::save_genome(ntm_genome, gen_dir + "/ntm.nnpk");
        std::cout << "Final save at generation " << generation << "\n";
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

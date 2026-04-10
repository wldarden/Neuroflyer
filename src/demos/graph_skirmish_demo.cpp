// src/demos/graph_skirmish_demo.cpp
//
// Standalone demo: team skirmish with GraphNetwork nets.
// Compares forward-pass performance of GraphNetwork vs MLP for equivalent
// dense topologies. No evolution, no net viewer, no config UI.

#include "mlp_to_graph.h"
#include "graph_arena_tick.h"

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/arena_session.h>
#include <neuroflyer/arena_tick.h>
#include <neuroflyer/camera.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/skirmish.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>
#include <neuroflyer/team_evolution.h>
#include <neuroflyer/team_skirmish.h>
#include <neuroflyer/ui/views/arena_game_view.h>

#include <neuralnet/graph_network.h>
#include <neuralnet/network.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace nf = neuroflyer;

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 800;

// ── Stress test knobs ───────────────────────────────────────────────────────
static constexpr std::size_t NUM_TEAMS = 4;
static constexpr std::size_t NUM_SQUADS = 5;
static constexpr std::size_t FIGHTERS_PER_SQUAD = 50;
// Total fighters: NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD = 1000
static constexpr float WORLD_SIZE = 10000.0f;  // bigger arena for more ships
static constexpr std::size_t TOWER_COUNT = 200;
static constexpr std::size_t TOKEN_COUNT = 100;

// ── MLP baseline benchmark ──────────────────────────────────────────────────

static double run_mlp_baseline(
    const nf::Snapshot& fighter_snap,
    const nf::Snapshot& squad_snap,
    const nf::Snapshot& ntm_snap,
    const nf::ShipDesign& fighter_design) {

    nf::SkirmishConfig sk_config;
    sk_config.world.world_width = WORLD_SIZE;
    sk_config.world.world_height = WORLD_SIZE;
    sk_config.world.num_teams = NUM_TEAMS;
    sk_config.world.num_squads = NUM_SQUADS;
    sk_config.world.fighters_per_squad = FIGHTERS_PER_SQUAD;
    sk_config.world.tower_count = TOWER_COUNT;
    sk_config.world.token_count = TOKEN_COUNT;

    nf::ArenaConfig arena_config;
    arena_config.world = sk_config.world;
    arena_config.time_limit_ticks = sk_config.time_limit_ticks;
    arena_config.sector_size = sk_config.sector_size;
    arena_config.ntm_sector_radius = sk_config.ntm_sector_radius;

    nf::ArenaSession arena(arena_config, 42);

    // Build random individuals with CURRENT expected input sizes (snapshots may
    // have stale input counts from older code).  Hidden layer sizes come from
    // the snapshots so the computational graph is equivalent.
    std::mt19937 baseline_rng(123);
    const std::size_t fighter_inputs = nf::compute_arena_input_size(fighter_design);
    const std::size_t fighter_outputs = nf::compute_output_size(fighter_design);
    constexpr std::size_t SL_INPUTS = 17;
    constexpr std::size_t SL_OUTPUTS = 5;
    constexpr std::size_t NTM_IN = 7;
    constexpr std::size_t NTM_OUT = 1;

    auto fighter_hidden = extract_hidden_sizes(fighter_snap.topology);
    auto squad_hidden = extract_hidden_sizes(squad_snap.topology);
    auto ntm_hidden = extract_hidden_sizes(ntm_snap.topology);

    auto fighter_ind = nf::Individual::random(fighter_inputs, fighter_hidden, fighter_outputs, baseline_rng);
    auto squad_ind = nf::Individual::random(SL_INPUTS, squad_hidden, SL_OUTPUTS, baseline_rng);
    auto ntm_ind = nf::Individual::random(NTM_IN, ntm_hidden, NTM_OUT, baseline_rng);

    std::vector<std::vector<neuralnet::Network>> team_ntm(NUM_TEAMS);
    std::vector<std::vector<neuralnet::Network>> team_leader(NUM_TEAMS);
    std::vector<std::vector<neuralnet::Network>> team_fighter(NUM_TEAMS);

    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        for (std::size_t sq = 0; sq < NUM_SQUADS; ++sq) {
            team_ntm[t].push_back(ntm_ind.build_network());
            team_leader[t].push_back(squad_ind.build_network());
        }
        for (std::size_t f = 0; f < NUM_SQUADS * FIGHTERS_PER_SQUAD; ++f) {
            team_fighter[t].push_back(fighter_ind.build_network());
        }
    }

    const std::size_t total_ships = arena.ships().size();
    std::vector<std::vector<float>> recurrent(total_ships,
        std::vector<float>(fighter_design.memory_slots, 0.0f));
    std::vector<int> ship_teams(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) ship_teams[i] = arena.team_of(i);

    // Build assignments — distribute fighters across squads
    std::vector<nf::ShipAssignment> assignments(total_ships);
    std::vector<std::size_t> team_ship_counter(NUM_TEAMS, 0);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto team = static_cast<std::size_t>(ship_teams[i]);
        const std::size_t idx_in_team = team_ship_counter[team]++;
        assignments[i].team_id = team;
        assignments[i].squad_index = idx_in_team / FIGHTERS_PER_SQUAD;
        assignments[i].fighter_index = idx_in_team;
    }

    constexpr int BASELINE_TICKS = 100;
    auto start = std::chrono::high_resolution_clock::now();
    for (int tick = 0; tick < BASELINE_TICKS && !arena.is_over(); ++tick) {
        nf::tick_team_arena_match(arena, arena_config, fighter_design,
            assignments, team_ntm, team_leader, team_fighter,
            recurrent, ship_teams);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / BASELINE_TICKS;
}

// ── GraphNetwork match setup ────────────────────────────────────────────────

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

static GraphMatch create_graph_match(
    const neuralnet::NeuralGenome& fighter_genome,
    const neuralnet::NeuralGenome& squad_genome,
    const neuralnet::NeuralGenome& ntm_genome,
    const nf::ShipDesign& fighter_design,
    uint32_t seed, std::mt19937& rng) {

    GraphMatch m;
    nf::SkirmishConfig sk_config;
    sk_config.world.world_width = WORLD_SIZE;
    sk_config.world.world_height = WORLD_SIZE;
    sk_config.world.num_teams = NUM_TEAMS;
    sk_config.world.num_squads = NUM_SQUADS;
    sk_config.world.fighters_per_squad = FIGHTERS_PER_SQUAD;
    sk_config.world.tower_count = TOWER_COUNT;
    sk_config.world.token_count = TOKEN_COUNT;

    m.arena_config.world = sk_config.world;
    m.arena_config.time_limit_ticks = sk_config.time_limit_ticks;
    m.arena_config.sector_size = sk_config.sector_size;
    m.arena_config.ntm_sector_radius = sk_config.ntm_sector_radius;

    m.arena = std::make_unique<nf::ArenaSession>(m.arena_config, seed);

    m.team_ntm.resize(NUM_TEAMS);
    m.team_leader.resize(NUM_TEAMS);
    m.team_fighter.resize(NUM_TEAMS);

    std::uniform_real_distribution<float> weight_dist(-1.0f, 1.0f);

    for (std::size_t t = 0; t < NUM_TEAMS; ++t) {
        for (std::size_t sq = 0; sq < NUM_SQUADS; ++sq) {
            auto ntm_copy = ntm_genome;
            for (auto& c : ntm_copy.connections) c.weight = weight_dist(rng);
            m.team_ntm[t].emplace_back(ntm_copy);

            auto sq_copy = squad_genome;
            for (auto& c : sq_copy.connections) c.weight = weight_dist(rng);
            m.team_leader[t].emplace_back(sq_copy);
        }
        for (std::size_t f = 0; f < NUM_SQUADS * FIGHTERS_PER_SQUAD; ++f) {
            auto f_copy = fighter_genome;
            for (auto& c : f_copy.connections) c.weight = weight_dist(rng);
            m.team_fighter[t].emplace_back(f_copy);
        }
    }

    const std::size_t total_ships = m.arena->ships().size();
    m.recurrent.assign(total_ships, std::vector<float>(fighter_design.memory_slots, 0.0f));
    m.ship_teams.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) m.ship_teams[i] = m.arena->team_of(i);

    // Distribute fighters across squads
    m.assignments.resize(total_ships);
    std::vector<std::size_t> team_counters(NUM_TEAMS, 0);
    for (std::size_t i = 0; i < total_ships; ++i) {
        const auto team = static_cast<std::size_t>(m.ship_teams[i]);
        const std::size_t idx = team_counters[team]++;
        m.assignments[i].team_id = team;
        m.assignments[i].squad_index = idx / FIGHTERS_PER_SQUAD;
        m.assignments[i].fighter_index = idx;
    }

    return m;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::unitbuf;  // flush after every output

    // Load reference snapshots
    std::cout << "Loading reference snapshots...\n";
    auto fighter_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/Ace Fighter-1.bin");
    auto squad_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1.bin");
    auto ntm_snap = nf::load_snapshot(
        "data/genomes/ArenaFighter/squad/ThousandYear-skirmish-g861-1-ntm.bin");

    const auto& fighter_design = fighter_snap.ship_design;

    std::cout << "Fighter topology: " << fighter_snap.topology.input_size << " inputs, "
              << fighter_snap.topology.layers.size() << " layers\n";
    std::cout << "Squad leader topology: " << squad_snap.topology.input_size << " inputs, "
              << squad_snap.topology.layers.size() << " layers\n";
    std::cout << "NTM topology: " << ntm_snap.topology.input_size << " inputs, "
              << ntm_snap.topology.layers.size() << " layers\n";

    // Build GraphNetwork genomes with the CURRENT expected input/output sizes
    // and the hidden layer structure from the reference snapshots.
    std::mt19937 rng(std::random_device{}());

    const std::size_t fighter_inputs = nf::compute_arena_input_size(fighter_design);
    const std::size_t fighter_outputs = nf::compute_output_size(fighter_design);
    constexpr std::size_t SQUAD_LEADER_INPUTS = 17;
    constexpr std::size_t SQUAD_LEADER_OUTPUTS = 5;
    constexpr std::size_t NTM_INPUTS = 7;
    constexpr std::size_t NTM_OUTPUTS = 1;

    auto fighter_hidden = extract_hidden_sizes(fighter_snap.topology);
    auto squad_hidden = extract_hidden_sizes(squad_snap.topology);
    auto ntm_hidden = extract_hidden_sizes(ntm_snap.topology);

    auto fighter_genome = build_dense_graph_genome(fighter_inputs, fighter_hidden, fighter_outputs, rng);
    auto squad_genome = build_dense_graph_genome(SQUAD_LEADER_INPUTS, squad_hidden, SQUAD_LEADER_OUTPUTS, rng);
    auto ntm_genome = build_dense_graph_genome(NTM_INPUTS, ntm_hidden, NTM_OUTPUTS, rng);

    std::cout << "Fighter graph: " << fighter_genome.nodes.size() << " nodes, "
              << fighter_genome.connections.size() << " connections\n";
    std::cout << "Squad leader graph: " << squad_genome.nodes.size() << " nodes, "
              << squad_genome.connections.size() << " connections\n";
    std::cout << "NTM graph: " << ntm_genome.nodes.size() << " nodes, "
              << ntm_genome.connections.size() << " connections\n";
    std::cout << "Config: " << NUM_TEAMS << " teams x " << NUM_SQUADS << " squads x "
              << FIGHTERS_PER_SQUAD << " fighters = "
              << NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD << " total fighters\n";

    // Run MLP baseline
    std::cout << "Running MLP baseline (100 ticks, "
              << NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD << " fighters)...\n";
    double mlp_ms = run_mlp_baseline(fighter_snap, squad_snap, ntm_snap, fighter_design);
    std::cout << "MLP baseline: " << mlp_ms << " ms/tick\n";

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    auto* window = SDL_CreateWindow("GraphNetwork Skirmish Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    // Arena view
    nf::ArenaGameView arena_view(sdl_renderer);
    arena_view.set_bounds(0, 0, WIN_W, WIN_H);

    // Camera centered on the world
    nf::Camera camera;
    camera.x = WORLD_SIZE / 2.0f;
    camera.y = WORLD_SIZE / 2.0f;
    camera.zoom = static_cast<float>(WIN_W) / WORLD_SIZE;

    // Create first match
    auto match = create_graph_match(fighter_genome, squad_genome, ntm_genome,
                                     fighter_design, 42, rng);

    // Timing state
    double graph_ms_per_tick = 0;
    double tick_accum_ms = 0;
    int tick_count = 0;
    int generation = 0;
    int speed = 1;

    bool running = true;
    while (running) {
        // Events
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
                    default: break;
                }
            }
        }

        // Tick simulation
        for (int s = 0; s < speed; ++s) {
            if (match.arena->is_over()) {
                generation++;
                match = create_graph_match(fighter_genome, squad_genome, ntm_genome,
                                            fighter_design, rng(), rng);
            }

            auto t0 = std::chrono::high_resolution_clock::now();
            graph_demo::graph_tick_team_arena_match(
                *match.arena, match.arena_config, fighter_design,
                match.assignments, match.team_ntm, match.team_leader,
                match.team_fighter, match.recurrent, match.ship_teams);
            auto t1 = std::chrono::high_resolution_clock::now();
            tick_accum_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            tick_count++;

            if (tick_count >= 60) {
                graph_ms_per_tick = tick_accum_ms / tick_count;
                tick_accum_ms = 0;
                tick_count = 0;
            }
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
        ImGui::Begin("Performance", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

        ImGui::Text("GraphNetwork Skirmish Demo");
        ImGui::Text("%zu teams x %zu squads x %zu fighters = %zu total",
            NUM_TEAMS, NUM_SQUADS, FIGHTERS_PER_SQUAD,
            NUM_TEAMS * NUM_SQUADS * FIGHTERS_PER_SQUAD);
        ImGui::Separator();
        ImGui::Text("Speed: %dx  (keys 1-4)", speed);
        ImGui::Text("Generation: %d", generation);
        ImGui::Text("Tick: %u / %u",
            match.arena->current_tick(), match.arena_config.time_limit_ticks);
        ImGui::Separator();
        ImGui::Text("MLP:          %.3f ms/tick  (%.0f ticks/s)",
            mlp_ms, mlp_ms > 0 ? 1000.0 / mlp_ms : 0);
        ImGui::Text("GraphNetwork: %.3f ms/tick  (%.0f ticks/s)",
            graph_ms_per_tick,
            graph_ms_per_tick > 0 ? 1000.0 / graph_ms_per_tick : 0);
        if (mlp_ms > 0 && graph_ms_per_tick > 0) {
            double ratio = graph_ms_per_tick / mlp_ms;
            ImGui::Text("Ratio:        %.2fx %s",
                ratio, ratio > 1.0 ? "(slower)" : "(faster)");
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
        SDL_RenderPresent(sdl_renderer);
    }

    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

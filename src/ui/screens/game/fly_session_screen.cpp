#include <neuroflyer/ui/screens/fly_session_screen.h>
#include <neuroflyer/ui/screens/pause_config_screen.h>
#include <neuroflyer/ui/views/net_viewer_view.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/genome_manager.h>
#include <neuroflyer/ray.h>
#include <neuroflyer/screens/game/fly_session.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/ship_design.h>
#include <neuroflyer/snapshot.h>
#include <neuroflyer/snapshot_io.h>

#include <imgui.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace neuroflyer {
namespace {

NetViewerViewState& get_fly_net_viewer_state() {
    static NetViewerViewState s_fly_net;
    return s_fly_net;
}

/// Hash an individual's weights to produce a unique ID for MRCA dedup.
uint32_t individual_hash(const Individual& ind) {
    uint32_t h = 0;
    auto flat = ind.genome.flatten_all();
    for (std::size_t i = 0; i < flat.size(); ++i) {
        uint32_t bits;
        std::memcpy(&bits, &flat[i], sizeof(bits));
        h ^= bits + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

/// Record elite lineage for MRCA tracking.
void record_mrca(FlySessionState& s) {
    std::vector<uint32_t> elite_ids;
    std::vector<neuralnet::NetworkTopology> elite_topos;
    std::vector<ShipDesign> elite_designs;
    std::vector<std::vector<float>> elite_weights;

    auto n_elites = std::min(s.evo_config.elitism_count, s.population.size());
    for (std::size_t e = 0; e < n_elites; ++e) {
        elite_ids.push_back(individual_hash(s.population[e]));
        elite_topos.push_back(s.population[e].topology);
        elite_designs.push_back(s.ship_design);
        elite_weights.push_back(s.population[e].genome.flatten("layer_"));
    }
    s.mrca_tracker.record_generation(static_cast<uint32_t>(s.generation),
                                     elite_ids, elite_topos, elite_designs, elite_weights);
}

/// Periodic snapshot auto-save.
void maybe_autosave(FlySessionState& s, const AppState& state) {
    if (!s.active_genome_dir.empty() && state.config.autosave_interval > 0
        && s.generation % static_cast<std::size_t>(state.config.autosave_interval) == 0) {
        auto snap = best_as_snapshot("~autosave", s.population, s.ship_design,
                                     static_cast<uint32_t>(s.generation));
        snap.parent_name = state.training_parent_name;
        write_autosave(s.active_genome_dir, snap);
    }
}

/// Create sessions, networks, and recurrent states from current population.
void rebuild_sessions(FlySessionState& s, const GameConfig& config) {
    s.sessions.clear();
    s.sessions.reserve(s.population.size());
    for (std::size_t i = 0; i < s.population.size(); ++i) {
        s.sessions.emplace_back(s.level_seed, s.game_w, s.game_h, config);
    }

    s.networks.clear();
    s.networks.reserve(s.population.size());
    for (auto& ind : s.population) {
        s.networks.push_back(ind.build_network());
    }

    s.recurrent_states.assign(
        s.population.size(), std::vector<float>(s.mem_slots, 0.0f));
    s.last_inputs.resize(s.population.size());
}

/// Compute fitness, record stats, evolve, track MRCA, autosave.
void do_evolution(FlySessionState& s, AppState& state) {
    // Assign fitness from sessions
    for (std::size_t i = 0; i < s.sessions.size(); ++i) {
        s.population[i].fitness = s.sessions[i].score();
    }

    float avg = 0.0f;
    float best = 0.0f;
    for (auto& ind : s.population) {
        avg += ind.fitness;
        best = std::max(best, ind.fitness);
    }
    avg /= static_cast<float>(s.population.size());

    float variance = 0.0f;
    for (auto& ind : s.population) {
        float diff = ind.fitness - avg;
        variance += diff * diff;
    }
    variance /= static_cast<float>(s.population.size());
    float stddev = std::sqrt(variance);

    s.gen_history.push_back({s.generation, best, avg, stddev});

    // Record structural histogram for this generation
    {
        StructuralHistogram hist;
        for (const auto& ind : s.population) {
            int layer_count = 0;
            int node_count = 0;
            // Exclude the last layer (output layer) -- count only hidden layers
            if (ind.topology.layers.size() > 1) {
                layer_count = static_cast<int>(ind.topology.layers.size()) - 1;
                for (std::size_t li = 0; li < ind.topology.layers.size() - 1; ++li) {
                    node_count += static_cast<int>(ind.topology.layers[li].output_size);
                }
            }
            hist.bins[{layer_count, node_count}]++;
        }
        s.structural_history.push_back(std::move(hist));
        if (s.structural_history.size() > 10000) s.structural_history.erase(s.structural_history.begin());
    }

    std::cout << "[Gen " << s.generation << "] Best: " << static_cast<int>(best)
              << " Avg: " << static_cast<int>(avg)
              << " Stddev: " << static_cast<int>(stddev) << "\n";

    s.population = evolve_population(s.population, s.evo_config, state.rng);

    record_mrca(s);
    maybe_autosave(s, state);
}

/// Initialize the fly session state from AppState.
void initialize_session(FlySessionState& s, AppState& state) {
    s.evo_config.population_size = state.config.population_size;
    s.evo_config.elitism_count = state.config.elitism_count;

    constexpr std::size_t action_count = ACTION_COUNT;

    // Use pending population if available, otherwise load from genome
    if (!state.pending_population.empty()) {
        s.population = std::move(state.pending_population);
        state.pending_population.clear();
        // ShipDesign comes from the source variant -- trust it entirely
        s.ship_design = state.pending_ship_design;
        state.pending_ship_design = ShipDesign{};
        std::cout << "Using pre-built population (" << s.population.size() << " individuals)\n";
    } else {
        // Set active_genome_dir for fly mode
        std::string active_genome_dir;
        if (!state.config.active_genome.empty()) {
            active_genome_dir = state.data_dir + "/genomes/" + state.config.active_genome;
        }

        bool loaded_from_snapshot = false;
        if (!active_genome_dir.empty()) {
            std::string genome_path = active_genome_dir + "/genome.bin";
            try {
                auto snap = load_snapshot(genome_path);
                s.population = create_population_from_snapshot(
                    snap, s.evo_config.population_size, s.evo_config, state.rng);
                // ShipDesign comes from the snapshot -- trust it entirely
                s.ship_design = snap.ship_design;
                loaded_from_snapshot = true;
                std::cout << "Loaded genome '" << snap.name
                          << "' and seeded " << s.population.size() << " individuals\n";
            } catch (const std::exception& e) {
                std::cerr << "Failed to load genome: " << e.what() << "\n";
            }
        }
        if (!loaded_from_snapshot) {
            std::cout << "Starting with random population\n";
            s.ship_design = create_legacy_ship_design(4);  // default 4 memory slots
            s.input_size = 31 + s.mem_slots;
            s.output_size = action_count + s.mem_slots;
            s.population = create_population(s.input_size, {12, 12}, s.output_size, s.evo_config, state.rng);
        }
    }

    // Derive INPUT_SIZE/OUTPUT_SIZE/MEM_SLOTS from actual network topology
    if (!s.population.empty()) {
        s.input_size = s.population[0].topology.input_size;
        auto& layers = s.population[0].topology.layers;
        s.output_size = layers.empty() ? action_count : layers.back().output_size;
        if (s.output_size > action_count) {
            s.mem_slots = s.output_size - action_count;
        } else {
            s.mem_slots = 0;
        }
    } else {
        s.input_size = 31 + s.mem_slots;
        s.output_size = action_count + s.mem_slots;
    }

    // Set active genome directory for autosave
    if (!state.config.active_genome.empty()) {
        s.active_genome_dir = state.data_dir + "/genomes/" + state.config.active_genome;
    } else {
        s.active_genome_dir.clear();
    }

    // Initialize MRCA tracker with config values
    s.mrca_tracker = MrcaTracker(
        s.evo_config.elitism_count,
        static_cast<std::size_t>(state.config.mrca_memory_limit_mb) * 1024 * 1024,
        static_cast<std::size_t>(state.config.mrca_prune_interval));

    // Game panel dimensions (live query)
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    s.game_w = display.x * 0.6f;
    s.game_h = display.y;

    s.generation = 0;
    s.gen_history.clear();
    s.view = ViewMode::Swarm;
    s.ticks_per_frame = 1;
    s.headless_remaining = 0;
    s.level_seed = static_cast<uint32_t>(state.rng());

    // Build initial sessions
    rebuild_sessions(s, state.config);

    // Advance to generation 1
    s.generation = 1;

    s.phase = FlySessionState::Phase::Running;
    s.initialized = true;
}

/// Tick one individual: sensor engine, forward network, decode outputs, update session.
void tick_individual(FlySessionState& s, std::size_t i, const GameConfig& config) {
    auto& sess = s.sessions[i];
    if (!sess.alive()) return;

    auto screen_towers = sess.towers_in_screen_coords();
    auto screen_tokens = sess.tokens_in_screen_coords();

    auto input = build_ship_input(s.ship_design, sess.triangle().x, sess.triangle().y,
        s.game_w, s.game_h, config.scroll_speed, config.pts_per_token,
        screen_towers, screen_tokens, s.recurrent_states[i]);

    auto output = s.networks[i].forward(input);

    // Store input for net panel display
    s.last_inputs[i] = input;

    // Decode actions + memory
    auto decoded = decode_output(output, s.mem_slots);
    sess.set_actions(decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);
    s.recurrent_states[i] = decoded.memory;

    sess.tick();
}

/// Render the game + net panel for the focused individual.
void render_fly(FlySessionState& s, const AppState& state, Renderer& renderer) {
    // Find focused individual
    std::size_t best_living_idx = 0;
    float best_living_score = -1e9f;
    float best_overall_score = -1e9f;
    float worst_score = 1e9f;
    std::size_t worst_idx = 0;
    std::size_t alive_count = 0;

    for (std::size_t i = 0; i < s.sessions.size(); ++i) {
        float sc = s.sessions[i].score();
        if (sc > best_overall_score) {
            best_overall_score = sc;
        }
        if (s.sessions[i].alive()) {
            ++alive_count;
            if (sc > best_living_score) {
                best_living_score = sc;
                best_living_idx = i;
            }
            if (sc < worst_score) {
                worst_score = sc;
                worst_idx = i;
            }
        }
    }

    std::size_t focused_idx = best_living_idx;
    bool hero_is_dead = (best_overall_score > best_living_score + 0.01f);

    if (s.view == ViewMode::Worst && alive_count > 0) {
        focused_idx = worst_idx;
    }

    // Query sensors for focused individual (for visualization)
    std::vector<RayEndpoint> focused_rays;
    if (focused_idx < s.sessions.size() && s.sessions[focused_idx].alive()) {
        auto viz_towers = s.sessions[focused_idx].towers_in_screen_coords();
        auto viz_tokens = s.sessions[focused_idx].tokens_in_screen_coords();
        auto sensor_endpoints = query_sensors_with_endpoints(
            s.ship_design,
            s.sessions[focused_idx].triangle().x,
            s.sessions[focused_idx].triangle().y,
            viz_towers, viz_tokens);
        focused_rays.reserve(sensor_endpoints.size());
        for (const auto& ep : sensor_endpoints) {
            focused_rays.push_back({ep.x, ep.y, ep.distance, ep.hit});
        }
    }

    // Derive vision_type from ShipDesign sensors
    bool has_occulus = false;
    for (const auto& sensor : s.ship_design.sensors) {
        if (sensor.type == SensorType::Occulus) { has_occulus = true; break; }
    }

    RenderState render_state{
        .view = s.view,
        .generation = s.generation,
        .alive_count = alive_count,
        .total_count = s.population.size(),
        .best_score = best_living_score,
        .glorious_hero_score = best_overall_score,
        .living_hero_score = best_living_score,
        .hero_is_dead = hero_is_dead,
        .ship_type = state.config.ship_type,
        .vision_type = has_occulus ? 1 : 0,
    };

    // Update game view bounds for current window size
    renderer.game_view.set_bounds(0, 0, renderer.game_w(), renderer.screen_h());

    // Render game panel (left side) via renderer
    renderer.render(s.sessions, focused_idx,
                    s.population[focused_idx], s.networks[focused_idx],
                    focused_rays, render_state, s.ship_design);

    // Render net panel (right side) via NetViewerView with editor_mode = false
    NetViewerViewState& fly_net_state = get_fly_net_viewer_state();
    fly_net_state.individual = &s.population[focused_idx];
    fly_net_state.network = &s.networks[focused_idx];
    fly_net_state.ship_design = s.ship_design;
    fly_net_state.input_values = (focused_idx < s.last_inputs.size())
        ? s.last_inputs[focused_idx] : std::vector<float>{};
    fly_net_state.render_x = renderer.game_w() + 10;
    fly_net_state.render_y = 10;
    fly_net_state.render_w = renderer.net_w() - 20;
    fly_net_state.render_h = renderer.screen_h() - 20;
    fly_net_state.editor_mode = false;
    draw_net_viewer_view(fly_net_state, renderer.renderer_);
}

/// Handle keyboard input. Returns true if navigation occurred (caller should return).
bool handle_input(FlySessionState& s, AppState& state, UIManager& ui) {
    // Use ImGui for single-press detection (avoids key repeat)
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        if (s.view == ViewMode::Swarm) s.view = ViewMode::Best;
        else if (s.view == ViewMode::Best) s.view = ViewMode::Worst;
        else s.view = ViewMode::Swarm;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        ui.push_screen(std::make_unique<PauseConfigScreen>());
        return true;
    }

    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Clean exit: delete autosave
        if (!s.active_genome_dir.empty()) {
            delete_autosave(s.active_genome_dir);
        }

        // Clear training context
        s.active_genome_dir.clear();
        state.training_parent_name.clear();

        // Invalidate all caches -- training may have produced autosaves
        state.invalidate_all();

        // Navigate back
        state.return_to_variant_view = false;
        ui.pop_screen();

        s.initialized = false;
        return true;
    }

    // Speed controls via keyboard state (repeatable)
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_1]) s.ticks_per_frame = 1;
    if (keys[SDL_SCANCODE_2]) s.ticks_per_frame = 5;
    if (keys[SDL_SCANCODE_3]) s.ticks_per_frame = 20;
    if (keys[SDL_SCANCODE_4]) s.ticks_per_frame = 100;

    if (ImGui::IsKeyPressed(ImGuiKey_H)) {
        std::cout << "\n=== SCORING RULES ===\n"
                  << "  Distance traveled:   +0.1 pts/px\n"
                  << "  Tower destroyed:     +50 pts\n"
                  << "  Token collected:     +500 pts\n"
                  << "  Bullet fired:        -30 pts\n"
                  << "  Hit a tower:         DEATH\n"
                  << "  Recurrent memory:    " << s.mem_slots << " memory slots\n"
                  << "\n=== CONTROLS ===\n"
                  << "  Tab       Cycle view (Swarm/Best/Worst)\n"
                  << "  Space     Pause/resume\n"
                  << "  1-4       Speed (1x/5x/20x/100x)\n"
                  << "  H         Show this help\n"
                  << "  Escape    Quit\n\n";
    }

    return false;
}

/// Draw a frosted-glass overlay showing headless run progress.
void draw_headless_overlay(const FlySessionState& s) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // Frosted glass background — semi-transparent white-grey
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.15f);
    ImGui::Begin("##HeadlessOverlay", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::End();

    // Content panel — centered, with opaque text
    float panel_w = 500.0f;
    float panel_h = std::min(420.0f, display.y * 0.7f);
    float px = (display.x - panel_w) * 0.5f;
    float py = (display.y - panel_h) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
    ImGui::Begin("##HeadlessPanel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    float title_w = ImGui::CalcTextSize("HEADLESS TRAINING").x;
    ImGui::SetCursorPosX((panel_w - title_w) * 0.5f);
    ImGui::Text("HEADLESS TRAINING");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Progress info
    int completed = s.headless_total - s.headless_remaining;
    int total = s.headless_total;

    ImGui::Text("Generation: %zu", s.generation);
    ImGui::Text("Progress: %d / %d generations", completed, total);
    ImGui::Spacing();

    // Progress bar
    float progress = total > 0 ? static_cast<float>(completed) / static_cast<float>(total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
    char progress_text[32];
    std::snprintf(progress_text, sizeof(progress_text), "%d%%",
        static_cast<int>(progress * 100.0f));
    ImGui::ProgressBar(progress, ImVec2(-1, 24), progress_text);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Stats table from headless runs
    if (!s.headless_stats.empty()) {
        ImGui::Text("Recent Headless Generations:");
        ImGui::Spacing();

        if (ImGui::BeginTable("##HeadlessStats", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY,
                ImVec2(0, panel_h - ImGui::GetCursorPosY() - 30.0f))) {
            ImGui::TableSetupColumn("Gen", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Best", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("StdDev", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // Show most recent first, limit to last ~50
            std::size_t start = s.headless_stats.size() > 50
                ? s.headless_stats.size() - 50 : 0;
            for (std::size_t i = s.headless_stats.size(); i > start; --i) {
                const auto& gs = s.headless_stats[i - 1];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", gs.generation);
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                    "%.0f", static_cast<double>(gs.best));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", static_cast<double>(gs.avg));
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f),
                    "%.0f", static_cast<double>(gs.stddev));
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
            "Running first generation...");
    }

    // Escape hint
    ImGui::SetCursorPosY(panel_h - 22.0f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f),
        "Press Escape to abort");

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

} // anonymous namespace

// ==================== FlySessionScreen ====================

void FlySessionScreen::on_draw(AppState& state, Renderer& renderer,
                                UIManager& ui) {
    auto& s = get_fly_session_state();

    // ---- Initialization ----
    if (!s.initialized) {
        s.reset();
        initialize_session(s, state);
    }

    // ---- Handle needs_reset (from pause config Apply) ----
    if (s.needs_reset) {
        s.needs_reset = false;

        // Sync evo config
        s.evo_config.population_size = state.config.population_size;
        s.evo_config.elitism_count = state.config.elitism_count;

        // New level seed for the restarted generation
        s.level_seed = static_cast<uint32_t>(state.rng());

        // Update game dimensions (live query)
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        s.game_w = display.x * 0.6f;
        s.game_h = display.y;

        rebuild_sessions(s, state.config);

        // Validate that the ship design's input count matches the network
        auto expected_inputs = compute_input_size(s.ship_design);
        if (expected_inputs != s.input_size) {
            std::cerr << "WARNING: ShipDesign input count (" << expected_inputs
                      << ") does not match network input size (" << s.input_size
                      << ") after reset\n";
        }
    }

    // ---- Phase dispatch ----
    switch (s.phase) {
    case FlySessionState::Phase::Running: {
        if (handle_input(s, state, ui)) return;

        // Run ticks_per_frame ticks
        for (int tick = 0; tick < s.ticks_per_frame; ++tick) {
            std::size_t alive_count = 0;
            for (std::size_t i = 0; i < s.sessions.size(); ++i) {
                if (!s.sessions[i].alive()) continue;
                ++alive_count;
                tick_individual(s, i, state.config);
            }
            if (alive_count == 0) {
                s.phase = FlySessionState::Phase::Evolving;
                break;
            }
        }

        render_fly(s, state, renderer);
        break;
    }

    case FlySessionState::Phase::Evolving: {
        do_evolution(s, state);

        // Next generation
        s.generation++;
        s.level_seed = static_cast<uint32_t>(state.rng());

        // Sync evo config
        s.evo_config.population_size = state.config.population_size;
        s.evo_config.elitism_count = state.config.elitism_count;

        rebuild_sessions(s, state.config);

        s.phase = FlySessionState::Phase::Running;

        // Render one frame so we don't have a blank
        render_fly(s, state, renderer);
        break;
    }

    case FlySessionState::Phase::HeadlessRunning: {
        if (!s.headless_overlay_drawn) {
            // Frame A: draw the overlay and return immediately.
            // The main loop will present this frame to screen.
            // Next frame we'll run the actual ticks.
            draw_headless_overlay(s);
            s.headless_overlay_drawn = true;
            break;
        }

        // Frame B: overlay is already on screen. Run the full generation
        // blocking. The frozen overlay stays visible during computation.
        s.headless_overlay_drawn = false;

        bool gen_done = false;
        int headless_tick_count = 0;
        while (!gen_done) {
            std::size_t alive = 0;
            for (std::size_t i = 0; i < s.sessions.size(); ++i) {
                if (!s.sessions[i].alive()) continue;
                ++alive;
                tick_individual(s, i, state.config);
            }
            if (alive == 0) gen_done = true;

            // Poll SDL events periodically so Escape can abort
            if (++headless_tick_count % 500 == 0) {
                SDL_Event evt;
                while (SDL_PollEvent(&evt)) {
                    if (evt.type == SDL_KEYDOWN &&
                        evt.key.keysym.sym == SDLK_ESCAPE) {
                        s.headless_remaining = 0;
                        s.phase = FlySessionState::Phase::Evolving;
                        gen_done = true;
                        break;
                    }
                }
            }
        }
        if (s.phase == FlySessionState::Phase::HeadlessRunning) {
            s.phase = FlySessionState::Phase::HeadlessEvolving;
        }
        break;
    }

    case FlySessionState::Phase::HeadlessEvolving: {
        do_evolution(s, state);

        // Collect stats for the headless progress overlay
        if (!s.gen_history.empty()) {
            s.headless_stats.push_back(s.gen_history.back());
        }

        s.headless_remaining--;

        if (s.headless_remaining <= 0) {
            std::cout << "Headless run complete.\n";
            // Prepare for next visible generation
            s.generation++;
            s.level_seed = static_cast<uint32_t>(state.rng());
            s.evo_config.population_size = state.config.population_size;
            s.evo_config.elitism_count = state.config.elitism_count;
            rebuild_sessions(s, state.config);
            s.phase = FlySessionState::Phase::Running;
        } else {
            // Next headless generation
            s.generation++;
            s.level_seed = static_cast<uint32_t>(state.rng());
            s.evo_config.population_size = state.config.population_size;
            s.evo_config.elitism_count = state.config.elitism_count;
            rebuild_sessions(s, state.config);
            s.phase = FlySessionState::Phase::HeadlessRunning;
        }
        break;
    }
    }
}

// ==================== FlySessionState engine functions ====================

FlySessionState::FlySessionState()
    : mrca_tracker(3, 64 * 1024 * 1024, 20) {}

void FlySessionState::reset() {
    initialized = false;
    phase = Phase::Running;
    generation = 0;
    ticks_per_frame = 1;
    needs_reset = false;
    population.clear();
    sessions.clear();
    networks.clear();
    recurrent_states.clear();
    last_inputs.clear();
    input_size = 0;
    output_size = 5;
    mem_slots = 0;
    gen_history.clear();
    structural_history.clear();
    headless_stats.clear();
    headless_total = 0;
    headless_overlay_drawn = false;
    view = ViewMode::Swarm;
    headless_remaining = 0;
    level_seed = 0;
    game_w = 0.0f;
    game_h = 0.0f;
    ship_design = ShipDesign{};
    active_genome_dir.clear();
}

FlySessionState& get_fly_session_state() {
    static FlySessionState s_fly;
    return s_fly;
}

void FlySessionScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(get_fly_net_viewer_state(), sdl_renderer);
}

} // namespace neuroflyer

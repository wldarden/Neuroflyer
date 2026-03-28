#include <neuroflyer/ui/screens/arena_game_screen.h>
#include <neuroflyer/ui/screens/pause_config_screen.h>
#include <neuroflyer/ui/views/arena_game_info_view.h>
#include <neuroflyer/ui/views/arena_game_view.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/sensor_engine.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <iostream>
#include <memory>

namespace neuroflyer {

// ==================== Construction ====================

ArenaGameScreen::ArenaGameScreen(const ArenaConfig& config)
    : config_(config) {}

// ==================== Lifecycle ====================

void ArenaGameScreen::on_enter() {
    initialized_ = false;
}

// ==================== Initialization ====================

void ArenaGameScreen::initialize(AppState& state) {
    // Population setup: use pending_population if available, else create random
    constexpr std::size_t action_count = ACTION_COUNT;
    std::size_t pop_size = config_.population_size();

    evo_config_.population_size = pop_size;

    if (!state.pending_population.empty()) {
        population_ = std::move(state.pending_population);
        state.pending_population.clear();
        ship_design_ = state.pending_ship_design;
        state.pending_ship_design = ShipDesign{};
        std::cout << "Arena: using pre-built population ("
                  << population_.size() << " individuals)\n";

        // If we got fewer individuals than needed, duplicate to fill
        std::size_t original_size = population_.size();
        while (population_.size() < pop_size && original_size > 0) {
            population_.push_back(population_[population_.size() % original_size]);
        }
        // If we got more, truncate
        if (population_.size() > pop_size) {
            population_.resize(pop_size);
        }
    } else {
        std::cout << "Arena: starting with random population ("
                  << pop_size << " ships)\n";
        ship_design_ = create_legacy_ship_design(4);
        std::size_t input_size = compute_input_size(ship_design_);
        std::size_t output_size = action_count + ship_design_.memory_slots;
        population_ = create_population(
            input_size, {12, 12}, output_size, evo_config_, state.rng);
    }

    // Compile networks from population
    networks_.clear();
    networks_.reserve(population_.size());
    for (auto& ind : population_) {
        networks_.push_back(ind.build_network());
    }

    // Initialize recurrent states
    recurrent_states_.assign(
        population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    // Create arena session
    uint32_t seed = static_cast<uint32_t>(state.rng());
    arena_ = std::make_unique<ArenaSession>(config_, seed);

    // Set camera to world center at zoom 0.5
    camera_.x = config_.world_width / 2.0f;
    camera_.y = config_.world_height / 2.0f;
    camera_.zoom = 0.5f;
    camera_.following = true;
    camera_.follow_index = 0;

    // Reset game state
    generation_ = 1;
    ticks_per_frame_ = 1;
    current_round_ = 0;
    cumulative_scores_.assign(population_.size(), 0.0f);
    selected_ship_ = 0;
    paused_ = false;

    initialized_ = true;
}

// ==================== Input handling ====================

void ArenaGameScreen::handle_input(UIManager& ui) {
    // Tab / Shift+Tab: cycle follow target
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        bool shift = ImGui::GetIO().KeyShift;
        int num_ships = static_cast<int>(arena_->ships().size());
        if (num_ships > 0) {
            if (shift) {
                // Find previous alive ship
                for (int attempts = 0; attempts < num_ships; ++attempts) {
                    selected_ship_ = (selected_ship_ - 1 + num_ships) % num_ships;
                    if (arena_->ships()[static_cast<std::size_t>(selected_ship_)].alive) break;
                }
            } else {
                // Find next alive ship
                for (int attempts = 0; attempts < num_ships; ++attempts) {
                    selected_ship_ = (selected_ship_ + 1) % num_ships;
                    if (arena_->ships()[static_cast<std::size_t>(selected_ship_)].alive) break;
                }
            }
            camera_.following = true;
            camera_.follow_index = selected_ship_;
        }
    }

    // Arrow keys: free camera
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float pan = Camera::PAN_SPEED / camera_.zoom;
    if (keys[SDL_SCANCODE_LEFT])  { camera_.x -= pan; camera_.following = false; }
    if (keys[SDL_SCANCODE_RIGHT]) { camera_.x += pan; camera_.following = false; }
    if (keys[SDL_SCANCODE_UP])    { camera_.y -= pan; camera_.following = false; }
    if (keys[SDL_SCANCODE_DOWN])  { camera_.y += pan; camera_.following = false; }

    // Mouse scroll: zoom
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        camera_.adjust_zoom(wheel * 0.05f);
    }

    // +/- keys: zoom
    if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) {
        camera_.adjust_zoom(0.02f);
    }
    if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) {
        camera_.adjust_zoom(-0.02f);
    }

    // Speed controls
    if (keys[SDL_SCANCODE_1]) ticks_per_frame_ = 1;
    if (keys[SDL_SCANCODE_2]) ticks_per_frame_ = 5;
    if (keys[SDL_SCANCODE_3]) ticks_per_frame_ = 20;
    if (keys[SDL_SCANCODE_4]) ticks_per_frame_ = 100;

    // Space: pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = !paused_;
    }

    // Escape: exit
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

// ==================== Tick ====================

void ArenaGameScreen::tick_arena(AppState& /*state*/) {
    if (!arena_ || arena_->is_over()) return;

    const auto& ships = arena_->ships();

    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        // Build neural net input using sensor engine
        // Arena mode uses world-relative positions; we pass world dimensions
        // and use 0 scroll_speed since arena is static
        auto input = build_ship_input(
            ship_design_,
            ships[i].x, ships[i].y,
            config_.world_width, config_.world_height,
            0.0f,  // no scroll speed in arena
            0.0f,  // pts_per_token not used for scoring here
            arena_->towers(), arena_->tokens(),
            recurrent_states_[i]);

        auto output = networks_[i].forward(input);

        auto decoded = decode_output(output, ship_design_.memory_slots);
        arena_->set_ship_actions(i, decoded.up, decoded.down,
                                 decoded.left, decoded.right, decoded.shoot);
        recurrent_states_[i] = decoded.memory;
    }

    arena_->tick();
}

// ==================== Evolution ====================

void ArenaGameScreen::do_arena_evolution(AppState& state) {
    // Assign fitness from cumulative scores
    for (std::size_t i = 0; i < population_.size(); ++i) {
        population_[i].fitness = cumulative_scores_[i];
    }

    // Log generation stats
    float avg = 0.0f;
    float best = 0.0f;
    for (auto& ind : population_) {
        avg += ind.fitness;
        best = std::max(best, ind.fitness);
    }
    avg /= static_cast<float>(population_.size());

    std::cout << "[Arena Gen " << generation_ << "] Best: "
              << static_cast<int>(best) << " Avg: "
              << static_cast<int>(avg) << "\n";

    // Evolve
    population_ = evolve_population(population_, evo_config_, state.rng);

    // Rebuild networks
    networks_.clear();
    networks_.reserve(population_.size());
    for (auto& ind : population_) {
        networks_.push_back(ind.build_network());
    }

    // Reset recurrent states
    recurrent_states_.assign(
        population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    // New arena session
    uint32_t seed = static_cast<uint32_t>(state.rng());
    arena_ = std::make_unique<ArenaSession>(config_, seed);

    generation_++;
    current_round_ = 0;
    cumulative_scores_.assign(population_.size(), 0.0f);
}

// ==================== Render ====================

void ArenaGameScreen::render_arena(Renderer& renderer) {
    if (!arena_) return;

    // Follow the selected ship if camera is in follow mode
    if (camera_.following && selected_ship_ >= 0
        && static_cast<std::size_t>(selected_ship_) < arena_->ships().size()) {
        const auto& ship = arena_->ships()[static_cast<std::size_t>(selected_ship_)];
        if (ship.alive) {
            camera_.x = ship.x;
            camera_.y = ship.y;
        }
    }

    // Clamp camera to world bounds
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();
    camera_.clamp_to_world(config_.world_width, config_.world_height,
                           game_panel_w, game_panel_h);

    // Build team assignments vector
    std::vector<int> team_assignments;
    team_assignments.reserve(arena_->ships().size());
    for (std::size_t i = 0; i < arena_->ships().size(); ++i) {
        team_assignments.push_back(arena_->team_of(i));
    }

    // Render game view (left panel)
    ArenaGameView view(renderer.renderer_);
    view.set_bounds(0, 0, game_panel_w, game_panel_h);
    view.render(*arena_, camera_, selected_ship_, team_assignments);

    // Divider line
    SDL_SetRenderDrawColor(renderer.renderer_, 60, 60, 80, 255);
    SDL_RenderDrawLine(renderer.renderer_,
                       game_panel_w, 0, game_panel_w, game_panel_h);

    // Right panel: ArenaGameInfoView (ImGui)
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;
    (void)info_w; // used implicitly by ImGui window positioning

    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(renderer.net_w() - 20),
               static_cast<float>(game_panel_h - 20)),
        ImGuiCond_Always);
    ImGui::Begin("##ArenaInfo", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    ArenaInfoState info;
    info.generation = generation_;
    info.current_tick = arena_->current_tick();
    info.time_limit_ticks = config_.time_limit_ticks;
    info.alive_count = arena_->alive_count();
    info.total_count = config_.population_size();
    info.teams_alive = arena_->teams_alive();
    info.num_teams = config_.num_teams;

    // Aggregate per-ship kills into per-team totals
    info.team_enemy_kills.assign(config_.num_teams, 0);
    info.team_ally_kills.assign(config_.num_teams, 0);
    const auto& ek = arena_->enemy_kills();
    const auto& ak = arena_->ally_kills();
    for (std::size_t i = 0; i < ek.size(); ++i) {
        auto t = static_cast<std::size_t>(arena_->team_of(i));
        info.team_enemy_kills[t] += ek[i];
        info.team_ally_kills[t] += ak[i];
    }
    info.team_scores = arena_->get_scores();

    draw_arena_game_info_view(info);

    ImGui::End();
}

// ==================== Main draw ====================

void ArenaGameScreen::on_draw(AppState& state, Renderer& renderer,
                               UIManager& ui) {
    // Initialize if needed
    if (!initialized_) {
        initialize(state);
    }

    // Handle input
    handle_input(ui);

    // Tick the arena (unless paused)
    if (!paused_) {
        for (int t = 0; t < ticks_per_frame_; ++t) {
            tick_arena(state);

            // Check if round is over
            if (arena_ && arena_->is_over()) {
                // Accumulate scores from this round
                auto scores = arena_->get_scores();
                // Arena scores are per-team; distribute to per-individual
                for (std::size_t i = 0; i < population_.size(); ++i) {
                    int team = arena_->team_of(i);
                    if (team >= 0 && static_cast<std::size_t>(team) < scores.size()) {
                        cumulative_scores_[i] += scores[static_cast<std::size_t>(team)];
                    }
                    // Individual survival bonus
                    if (arena_->ships()[i].alive) {
                        cumulative_scores_[i] += 10.0f;
                    }
                }

                current_round_++;

                if (current_round_ >= config_.rounds_per_generation) {
                    // All rounds done: evolve
                    do_arena_evolution(state);
                } else {
                    // Start next round with same population
                    recurrent_states_.assign(
                        population_.size(),
                        std::vector<float>(ship_design_.memory_slots, 0.0f));

                    uint32_t seed = static_cast<uint32_t>(state.rng());
                    arena_ = std::make_unique<ArenaSession>(config_, seed);
                }
                break;  // Don't tick further after reset
            }
        }
    }

    // Render
    render_arena(renderer);
}

void ArenaGameScreen::post_render(SDL_Renderer* /*sdl_renderer*/) {
    // No deferred SDL rendering needed for arena mode (no neural net panel)
}

} // namespace neuroflyer

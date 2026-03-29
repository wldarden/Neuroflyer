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
#include <cmath>
#include <iostream>
#include <limits>
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
    // Setup squad config
    squad_config_.input_size = 8;
    squad_config_.hidden_sizes = {6};
    squad_config_.output_size = config_.squad_broadcast_signals;

    // Create team population
    // If pending population exists, use a legacy ship design. Otherwise create fresh.
    if (!state.pending_population.empty()) {
        ship_design_ = state.pending_ship_design;
        state.pending_population.clear();
        state.pending_ship_design = ShipDesign{};
    } else {
        ship_design_ = create_legacy_ship_design(4);
    }

    std::size_t team_pop_size = 20;  // number of team genomes
    evo_config_.population_size = team_pop_size;
    evo_config_.elitism_count = 2;

    team_population_ = create_team_population(
        ship_design_, {8, 8}, squad_config_, team_pop_size, state.rng);

    // Start first match
    start_new_match(state);

    // Camera setup
    camera_.x = config_.world_width / 2.0f;
    camera_.y = config_.world_height / 2.0f;
    camera_.zoom = 0.5f;
    camera_.following = true;
    camera_.follow_index = 0;

    generation_ = 1;
    ticks_per_frame_ = 1;
    current_round_ = 0;
    team_fitness_.assign(team_population_.size(), 0.0f);
    selected_ship_ = 0;
    paused_ = false;
    initialized_ = true;
}

// ==================== Match setup ====================

void ArenaGameScreen::start_new_match(AppState& state) {
    uint32_t seed = static_cast<uint32_t>(state.rng());
    arena_ = std::make_unique<ArenaSession>(config_, seed);

    // For now, use team genomes 0 and 1
    // (In future: matchmaking picks different pairs per round)
    current_team_indices_ = {0, 1};

    // Compile networks
    squad_nets_.clear();
    fighter_nets_.clear();
    for (auto idx : current_team_indices_) {
        squad_nets_.push_back(team_population_[idx].build_squad_network());
        fighter_nets_.push_back(team_population_[idx].build_fighter_network());
    }

    // Build ship-to-team lookup
    std::size_t num_ships = arena_->ships().size();
    ship_teams_.resize(num_ships);
    for (std::size_t i = 0; i < num_ships; ++i) {
        ship_teams_[i] = arena_->team_of(i);
    }

    // Init recurrent states
    recurrent_states_.assign(
        num_ships,
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    // Init broadcasts
    team_broadcasts_.assign(
        config_.num_teams,
        std::vector<float>(squad_config_.output_size, 0.0f));
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

    // 1. Run squad net per team
    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        auto stats = arena_->compute_squad_stats(static_cast<int>(t), 0);

        float own_base_hp = arena_->bases()[t].hp_normalized();
        float enemy_base_hp = 0.0f;
        for (const auto& base : arena_->bases()) {
            if (base.team_id != static_cast<int>(t)) {
                enemy_base_hp = base.hp_normalized();
                break;
            }
        }

        std::vector<float> squad_input = {
            stats.alive_fraction,
            stats.avg_dist_to_enemy_base,
            stats.avg_dist_to_home,
            stats.centroid_dir_sin,
            stats.centroid_dir_cos,
            own_base_hp,
            enemy_base_hp,
            static_cast<float>(arena_->current_tick()) /
                static_cast<float>(config_.time_limit_ticks)
        };

        team_broadcasts_[t] = squad_nets_[t].forward(squad_input);
    }

    // 2. Run fighter nets
    for (std::size_t i = 0; i < arena_->ships().size(); ++i) {
        if (!arena_->ships()[i].alive) continue;

        int team = ship_teams_[i];
        auto t = static_cast<std::size_t>(team);

        // Find target (nearest enemy base) and home base
        float target_x = 0, target_y = 0;
        float min_dist_sq = std::numeric_limits<float>::max();
        for (const auto& base : arena_->bases()) {
            if (base.team_id == team) continue;
            float dx = base.x - arena_->ships()[i].x;
            float dy = base.y - arena_->ships()[i].y;
            float dist_sq = dx * dx + dy * dy;
            if (dist_sq < min_dist_sq) {
                min_dist_sq = dist_sq;
                target_x = base.x;
                target_y = base.y;
            }
        }

        float home_x = arena_->bases()[t].x;
        float home_y = arena_->bases()[t].y;
        float own_base_hp = arena_->bases()[t].hp_normalized();

        auto target_dr = compute_dir_range(
            arena_->ships()[i].x, arena_->ships()[i].y,
            target_x, target_y,
            config_.world_width, config_.world_height);
        auto home_dr = compute_dir_range(
            arena_->ships()[i].x, arena_->ships()[i].y,
            home_x, home_y,
            config_.world_width, config_.world_height);

        // Build query context (spans -- no copies)
        ArenaQueryContext ctx;
        ctx.ship_x = arena_->ships()[i].x;
        ctx.ship_y = arena_->ships()[i].y;
        ctx.ship_rotation = arena_->ships()[i].rotation;
        ctx.world_w = config_.world_width;
        ctx.world_h = config_.world_height;
        ctx.self_index = i;
        ctx.self_team = team;
        ctx.towers = arena_->towers();
        ctx.tokens = arena_->tokens();
        ctx.ships = arena_->ships();
        ctx.ship_teams = ship_teams_;
        ctx.bullets = arena_->bullets();

        auto input = build_arena_ship_input(
            ship_design_, ctx,
            target_dr.dir_sin, target_dr.dir_cos, target_dr.range,
            home_dr.dir_sin, home_dr.dir_cos, home_dr.range,
            own_base_hp,
            team_broadcasts_[t],
            recurrent_states_[i]);

        auto output = fighter_nets_[t].forward(input);
        auto decoded = decode_output(output, ship_design_.memory_slots);

        arena_->set_ship_actions(i, decoded.up, decoded.down,
                                 decoded.left, decoded.right, decoded.shoot);
        recurrent_states_[i] = decoded.memory;
    }

    arena_->tick();
}

// ==================== Evolution ====================

void ArenaGameScreen::do_arena_evolution(AppState& state) {
    // Log
    float best = 0.0f;
    for (const auto& t : team_population_) {
        best = std::max(best, t.fitness);
    }
    std::cout << "[Arena Gen " << generation_ << "] Best team fitness: "
              << static_cast<int>(best) << "\n";

    // Evolve
    team_population_ = evolve_team_population(team_population_, evo_config_, state.rng);

    generation_++;
    current_round_ = 0;
    team_fitness_.assign(team_population_.size(), 0.0f);

    // Start new match
    start_new_match(state);
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
        auto t_idx = static_cast<std::size_t>(arena_->team_of(i));
        info.team_enemy_kills[t_idx] += ek[i];
        info.team_ally_kills[t_idx] += ak[i];
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
                // Compute team fitness for this round
                for (std::size_t ti = 0; ti < config_.num_teams; ++ti) {
                    int team = static_cast<int>(ti);

                    // Damage dealt: average of (max_hp - hp) / max_hp for each enemy base
                    float damage_dealt = 0.0f;
                    for (const auto& base : arena_->bases()) {
                        if (base.team_id != team) {
                            damage_dealt += (base.max_hp - base.hp) / base.max_hp;
                        }
                    }
                    if (config_.num_teams > 1) {
                        damage_dealt /= static_cast<float>(config_.num_teams - 1);
                    }

                    // Own survival: own base HP normalized
                    float own_survival = arena_->bases()[ti].hp_normalized();

                    // Alive fraction and token count
                    float ships_alive_count = 0, ship_total = 0;
                    int team_tokens = 0;
                    const auto& tc = arena_->tokens_collected();
                    for (std::size_t i = 0; i < arena_->ships().size(); ++i) {
                        if (ship_teams_[i] == team) {
                            ship_total += 1.0f;
                            if (arena_->ships()[i].alive) ships_alive_count += 1.0f;
                            team_tokens += tc[i];
                        }
                    }
                    float alive_frac = ship_total > 0
                        ? ships_alive_count / ship_total : 0.0f;
                    float token_frac = config_.token_count > 0
                        ? static_cast<float>(team_tokens) / static_cast<float>(config_.token_count)
                        : 0.0f;

                    float score = config_.fitness_weight_base_damage * damage_dealt
                                + config_.fitness_weight_survival * own_survival
                                + config_.fitness_weight_ships_alive * alive_frac
                                + config_.fitness_weight_tokens * token_frac;

                    auto team_genome_idx = current_team_indices_[ti];
                    team_population_[team_genome_idx].fitness += score;
                }

                current_round_++;
                if (current_round_ >= config_.rounds_per_generation) {
                    do_arena_evolution(state);
                } else {
                    start_new_match(state);
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

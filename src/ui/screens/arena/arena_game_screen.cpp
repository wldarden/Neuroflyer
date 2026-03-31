#include <neuroflyer/ui/screens/arena_game_screen.h>
#include <neuroflyer/ui/screens/arena_pause_screen.h>
#include <neuroflyer/ui/screens/pause_config_screen.h>
#include <neuroflyer/ui/views/arena_game_info_view.h>
#include <neuroflyer/ui/views/arena_game_view.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/sector_grid.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>
#include <neuroflyer/squad_leader.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

namespace neuroflyer {

// ==================== Construction ====================

ArenaGameScreen::ArenaGameScreen(const ArenaConfig& config)
    : config_(config) {}

ArenaGameScreen::~ArenaGameScreen() {
    destroy_net_viewer_view(net_viewer_state_);
}

// ==================== Lifecycle ====================

void ArenaGameScreen::on_enter() {
    initialized_ = false;
}

// ==================== Initialization ====================

void ArenaGameScreen::initialize(AppState& state) {
    // Arena currently only supports exactly 2 teams
    assert(config_.num_teams == 2 && "Arena mode requires exactly 2 teams");

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
        ship_design_, {8, 8}, ntm_config_, leader_config_, team_pop_size, state.rng);

    // Check for squad training mode from AppState
    squad_training_mode_ = state.squad_training_mode;
    base_attack_mode_ = state.base_attack_mode;
    state.squad_training_mode = false;  // consume flags
    state.base_attack_mode = false;

    if (squad_training_mode_) {
        squad_paired_fighter_name_ = state.squad_paired_fighter_name;
        squad_genome_dir_ = state.squad_training_genome_dir;

        // Load the paired fighter snapshot
        // Try variant path first, fall back to genome.bin (root)
        std::string fighter_path = squad_genome_dir_ + "/" + squad_paired_fighter_name_ + ".bin";
        if (!std::filesystem::exists(fighter_path)) {
            fighter_path = squad_genome_dir_ + "/genome.bin";
        }
        paired_fighter_snapshot_ = load_snapshot(fighter_path);
        ship_design_ = paired_fighter_snapshot_.ship_design;

        // Override team population: create squad nets, freeze fighters
        auto fighter_ind = snapshot_to_individual(paired_fighter_snapshot_);

        // Load squad variant if one was selected (seed squad nets from it)
        const Individual* squad_seed = nullptr;
        Individual squad_ind;
        if (!state.squad_variant_name.empty()) {
            std::string sq_path = squad_genome_dir_ + "/squad/"
                + state.squad_variant_name + ".bin";
            try {
                auto sq_snap = load_snapshot(sq_path);
                squad_ind = snapshot_to_individual(sq_snap);
                squad_seed = &squad_ind;
                std::cout << "Loaded squad variant '" << state.squad_variant_name
                          << "' to seed population\n";
            } catch (const std::exception& e) {
                std::cerr << "Failed to load squad variant: " << e.what() << "\n";
            }
        }
        state.squad_variant_name.clear();  // consume

        team_pop_size = 20;
        evo_config_.population_size = team_pop_size;
        team_population_.clear();
        for (std::size_t i = 0; i < team_pop_size; ++i) {
            auto team = TeamIndividual::create(
                ship_design_, {8, 8}, ntm_config_, leader_config_, state.rng,
                &fighter_ind, squad_seed);
            team_population_.push_back(std::move(team));
        }

        std::cout << "Squad training mode: fighter=\"" << squad_paired_fighter_name_
                  << "\", evolving squad nets only\n";
    }

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

    // Random matchmaking: pick 2 distinct team genomes for this round
    {
        std::uniform_int_distribution<std::size_t> dist(0, team_population_.size() - 1);
        std::size_t a = dist(state.rng);
        std::size_t b = dist(state.rng);
        while (b == a && team_population_.size() > 1) {
            b = dist(state.rng);
        }
        current_team_indices_ = {a, b};
    }

    // Clear stale net viewer pointers before rebuilding networks
    net_viewer_state_.network = nullptr;
    net_viewer_state_.individual = nullptr;

    // Compile networks: NTM + squad leader + fighter per team
    ntm_nets_.clear();
    leader_nets_.clear();
    fighter_nets_.clear();
    for (auto idx : current_team_indices_) {
        ntm_nets_.push_back(team_population_[idx].build_ntm_network());
        leader_nets_.push_back(team_population_[idx].build_squad_network());
        fighter_nets_.push_back(team_population_[idx].build_fighter_network());
    }

    // Build ship-to-team lookup
    std::size_t num_ships = arena_->ships().size();
    ship_teams_.resize(num_ships);
    for (std::size_t i = 0; i < num_ships; ++i) {
        ship_teams_[i] = arena_->team_of(i);
    }

    // Base Attack: kill all team 1 fighters (undefended base)
    if (base_attack_mode_) {
        for (std::size_t i = 0; i < num_ships; ++i) {
            if (arena_->team_of(i) == 1) {
                arena_->ships()[i].alive = false;
            }
        }
    }

    // Init recurrent states
    recurrent_states_.assign(
        num_ships,
        std::vector<float>(ship_design_.memory_slots, 0.0f));

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

    // Space: open pause screen
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = true;
        ui.push_screen(std::make_unique<ArenaPauseScreen>(
            team_population_, generation_, ship_design_,
            squad_genome_dir_, squad_paired_fighter_name_, ntm_config_,
            [this]() { paused_ = false; }));
    }

    // Escape: exit
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

// ==================== Tick ====================

void ArenaGameScreen::tick_arena(AppState& /*state*/) {
    if (!arena_ || arena_->is_over()) return;

    std::size_t total_ships = arena_->ships().size();

    // 1. Build sector grid
    SectorGrid grid(config_.world_width, config_.world_height, config_.sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena_->ships()[i].alive) {
            grid.insert(i, arena_->ships()[i].x, arena_->ships()[i].y);
        }
    }
    for (std::size_t b = 0; b < arena_->bases().size(); ++b) {
        if (arena_->bases()[b].alive()) {
            grid.insert(total_ships + b, arena_->bases()[b].x, arena_->bases()[b].y);
        }
    }

    // 2. Per team: NTM + squad leader -> SquadLeaderOrder
    std::vector<SquadLeaderOrder> team_orders(config_.num_teams);
    std::vector<float> squad_center_xs(config_.num_teams, 0.0f);
    std::vector<float> squad_center_ys(config_.num_teams, 0.0f);

    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        int team = static_cast<int>(t);
        auto stats = arena_->compute_squad_stats(team, 0);
        squad_center_xs[t] = stats.centroid_x;
        squad_center_ys[t] = stats.centroid_y;

        if (stats.alive_fraction < 1e-6f) continue;

        auto threats = gather_near_threats(
            grid, stats.centroid_x, stats.centroid_y,
            config_.ntm_sector_radius, team,
            arena_->ships(), ship_teams_, arena_->bases());

        auto ntm = run_ntm_threat_selection(
            ntm_nets_[t], stats.centroid_x, stats.centroid_y,
            stats.alive_fraction, threats,
            config_.world_width, config_.world_height);

        float own_base_x = arena_->bases()[t].x, own_base_y = arena_->bases()[t].y;
        float own_base_hp = arena_->bases()[t].hp_normalized();
        float enemy_base_x = 0, enemy_base_y = 0;
        float min_dist_sq = std::numeric_limits<float>::max();
        for (const auto& base : arena_->bases()) {
            if (base.team_id == team) continue;
            float dx = stats.centroid_x - base.x, dy = stats.centroid_y - base.y;
            float dsq = dx * dx + dy * dy;
            if (dsq < min_dist_sq) { min_dist_sq = dsq; enemy_base_x = base.x; enemy_base_y = base.y; }
        }

        float world_diag = std::sqrt(config_.world_width * config_.world_width +
                                      config_.world_height * config_.world_height);
        float home_dx = own_base_x - stats.centroid_x, home_dy = own_base_y - stats.centroid_y;
        float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
        float home_distance = home_dist_raw / world_diag;
        float home_heading_sin = (home_dist_raw > 1e-6f) ? home_dx / home_dist_raw : 0.0f;
        float home_heading_cos = (home_dist_raw > 1e-6f) ? home_dy / home_dist_raw : 0.0f;
        float cmd_dx = enemy_base_x - stats.centroid_x, cmd_dy = enemy_base_y - stats.centroid_y;
        float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
        float cmd_heading_sin = (cmd_dist_raw > 1e-6f) ? cmd_dx / cmd_dist_raw : 0.0f;
        float cmd_heading_cos = (cmd_dist_raw > 1e-6f) ? cmd_dy / cmd_dist_raw : 0.0f;
        float cmd_target_distance = cmd_dist_raw / world_diag;

        team_orders[t] = run_squad_leader(
            leader_nets_[t], stats.alive_fraction,
            home_heading_sin, home_heading_cos, home_distance,
            own_base_hp, stats.squad_spacing,
            cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
            ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y);

        // Capture squad leader input for the followed ship's team
        if (camera_.following
            && selected_ship_ >= 0
            && static_cast<std::size_t>(selected_ship_) < arena_->ships().size()
            && ship_teams_[static_cast<std::size_t>(selected_ship_)] == static_cast<int>(t)) {
            last_leader_input_ = {
                stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp, stats.squad_spacing,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm.active ? 1.0f : 0.0f,
                ntm.active ? ntm.heading_sin : 0.0f,
                ntm.active ? ntm.heading_cos : 0.0f,
                ntm.active ? ntm.distance : 0.0f,
                ntm.active ? ntm.threat_score : 0.0f
            };
        }
    }

    // 3. Per fighter: compute squad leader fighter inputs + build arena input
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena_->ships()[i].alive) continue;

        int team = ship_teams_[i];
        auto t = static_cast<std::size_t>(team);

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena_->ships()[i].x, arena_->ships()[i].y,
            arena_->ships()[i].rotation,
            team_orders[t],
            squad_center_xs[t], squad_center_ys[t],
            config_.world_width, config_.world_height);

        auto ctx = ArenaQueryContext::for_ship(
            arena_->ships()[i], i, team,
            config_.world_width, config_.world_height,
            arena_->towers(), arena_->tokens(),
            arena_->ships(), ship_teams_, arena_->bullets());

        auto input = build_arena_ship_input(
            ship_design_, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states_[i]);

        // Capture fighter input for the followed ship
        if (camera_.following && i == static_cast<std::size_t>(selected_ship_)) {
            last_fighter_input_ = input;
        }

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

    // Clear stale individual pointer BEFORE population is replaced
    net_viewer_state_.individual = nullptr;
    net_viewer_state_.network = nullptr;

    // Evolve
    if (squad_training_mode_) {
        team_population_ = evolve_squad_only(team_population_, evo_config_, state.rng);
        // Refreeze fighter weights from paired snapshot (convert to arena format)
        auto fighter_ind = convert_variant_to_fighter(
            snapshot_to_individual(paired_fighter_snapshot_), ship_design_);
        for (auto& team : team_population_) {
            team.fighter_individual = fighter_ind;
        }
    } else {
        team_population_ = evolve_team_population(team_population_, evo_config_, state.rng);
    }

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

    // Right panel
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;

    // Determine if we should show the net viewer (follow mode with valid alive ship)
    bool show_net_viewer = camera_.following
        && selected_ship_ >= 0
        && static_cast<std::size_t>(selected_ship_) < arena_->ships().size()
        && arena_->ships()[static_cast<std::size_t>(selected_ship_)].alive;

    if (show_net_viewer) {
        // Net selector strip
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(info_w), 36.0f),
                                 ImGuiCond_Always);
        ImGui::Begin("##NetSelector", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        bool is_fighter = (follow_net_view_ == FollowNetView::Fighter);
        if (is_fighter) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
        if (ImGui::Button("Fighter", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::Fighter;
        }
        if (is_fighter) ImGui::PopStyleColor();

        ImGui::SameLine();

        bool is_leader = (follow_net_view_ == FollowNetView::SquadLeader);
        if (is_leader) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.4f, 0.2f, 1.0f));
        if (ImGui::Button("Squad Leader", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::SquadLeader;
        }
        if (is_leader) ImGui::PopStyleColor();

        ImGui::End();

        // Configure net viewer state
        int team = ship_teams_[static_cast<std::size_t>(selected_ship_)];
        auto t = static_cast<std::size_t>(team);
        auto team_genome_idx = current_team_indices_[t];

        if (follow_net_view_ == FollowNetView::Fighter) {
            net_viewer_state_.individual = &team_population_[team_genome_idx].fighter_individual;
            net_viewer_state_.network = &fighter_nets_[t];
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::Fighter;
            net_viewer_state_.input_values = last_fighter_input_;
        } else {
            net_viewer_state_.individual = &team_population_[team_genome_idx].squad_individual;
            net_viewer_state_.network = &leader_nets_[t];
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::SquadLeader;
            net_viewer_state_.input_values = last_leader_input_;
        }

        int net_y = 50;  // below selector strip
        net_viewer_state_.render_x = info_x;
        net_viewer_state_.render_y = net_y;
        net_viewer_state_.render_w = info_w;
        net_viewer_state_.render_h = game_panel_h - net_y - 10;

        draw_net_viewer_view(net_viewer_state_, renderer.renderer_);
    } else {
        // Info view (default when not following)
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(info_w),
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

void ArenaGameScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(net_viewer_state_, sdl_renderer);
}

} // namespace neuroflyer

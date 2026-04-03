#include <neuroflyer/ui/screens/skirmish_screen.h>
#include <neuroflyer/ui/screens/skirmish_pause_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <numbers>

namespace neuroflyer {

namespace {

// --- SDL drawing helpers (same as fighter_drill_screen.cpp) ---

void draw_rotated_triangle(SDL_Renderer* renderer,
                           float cx, float cy, float size,
                           float rotation,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float cos_r = std::cos(rotation);
    float sin_r = std::sin(rotation);

    auto rotate = [&](float lx, float ly, float& ox, float& oy) {
        ox = cx + lx * cos_r - ly * sin_r;
        oy = cy + lx * sin_r + ly * cos_r;
    };

    float x0, y0, x1, y1, x2, y2;
    rotate(0.0f, -size, x0, y0);
    rotate(-size, size, x1, y1);
    rotate(size, size, x2, y2);

    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderDrawLineF(renderer, x0, y0, x1, y1);
    SDL_RenderDrawLineF(renderer, x1, y1, x2, y2);
    SDL_RenderDrawLineF(renderer, x2, y2, x0, y0);
}

void draw_filled_circle(SDL_Renderer* renderer,
                        float cx, float cy, float radius,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    int ir = static_cast<int>(radius);
    for (int dy = -ir; dy <= ir; ++dy) {
        int dx = static_cast<int>(
            std::sqrt(static_cast<float>(ir * ir - dy * dy)));
        SDL_RenderDrawLine(renderer,
            static_cast<int>(cx) - dx, static_cast<int>(cy) + dy,
            static_cast<int>(cx) + dx, static_cast<int>(cy) + dy);
    }
}

void draw_circle_outline(SDL_Renderer* renderer,
                         float cx, float cy, float radius,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    int ir = static_cast<int>(radius);
    int x = ir, y = 0;
    int icx = static_cast<int>(cx), icy = static_cast<int>(cy);
    int err = 1 - ir;
    while (x >= y) {
        SDL_RenderDrawPoint(renderer, icx + x, icy + y);
        SDL_RenderDrawPoint(renderer, icx - x, icy + y);
        SDL_RenderDrawPoint(renderer, icx + x, icy - y);
        SDL_RenderDrawPoint(renderer, icx - x, icy - y);
        SDL_RenderDrawPoint(renderer, icx + y, icy + x);
        SDL_RenderDrawPoint(renderer, icx - y, icy + x);
        SDL_RenderDrawPoint(renderer, icx + y, icy - x);
        SDL_RenderDrawPoint(renderer, icx - y, icy - x);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

const char* camera_mode_str(int mode) {
    switch (mode) {
    case 0: return "SWARM";
    case 1: return "FOLLOW";
    }
    return "?";
}

} // anonymous namespace

// ==================== Construction ====================

SkirmishScreen::SkirmishScreen(Snapshot squad_snapshot,
                               Snapshot fighter_snapshot,
                               std::string genome_dir,
                               std::string variant_name,
                               SkirmishConfig config)
    : squad_snapshot_(std::move(squad_snapshot))
    , fighter_snapshot_(std::move(fighter_snapshot))
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name))
    , config_(std::move(config)) {}

SkirmishScreen::~SkirmishScreen() {
    destroy_net_viewer_view(net_viewer_state_);
}

// ==================== Initialization ====================

void SkirmishScreen::initialize(AppState& state) {
    ship_design_ = fighter_snapshot_.ship_design;

    rng_.seed(static_cast<uint32_t>(state.rng()));

    // Convert snapshots to Individuals
    auto squad_ind = snapshot_to_individual(squad_snapshot_);
    auto fighter_ind = snapshot_to_individual(fighter_snapshot_);
    if (fighter_snapshot_.net_type != NetType::Fighter) {
        fighter_ind = convert_variant_to_fighter(fighter_ind, ship_design_);
    }

    // Evolution config
    evo_config_.population_size = config_.population_size;
    evo_config_.elitism_count = 2;

    // Create population of TeamIndividuals seeded from the squad + fighter variants
    population_.clear();
    population_.reserve(config_.population_size);
    for (std::size_t i = 0; i < config_.population_size; ++i) {
        auto team = TeamIndividual::create(
            ship_design_, {8},
            NtmNetConfig{}, SquadLeaderNetConfig{},
            rng_, &fighter_ind, &squad_ind);
        population_.push_back(std::move(team));
    }

    // Diversify all but the first individual
    for (std::size_t i = 1; i < population_.size(); ++i) {
        apply_mutations(population_[i].ntm_individual, evo_config_, rng_);
        apply_mutations(population_[i].squad_individual, evo_config_, rng_);
    }

    // Create first tournament
    uint32_t seed = static_cast<uint32_t>(rng_());
    tournament_ = std::make_unique<SkirmishTournament>(
        config_, ship_design_, population_, seed);

    // Camera at world center
    camera_.x = config_.world_width / 2.0f;
    camera_.y = config_.world_height / 2.0f;
    camera_.zoom = 0.15f;
    camera_.following = false;
    camera_mode_ = CameraMode::Swarm;

    generation_ = 1;
    ticks_per_frame_ = 1;
    selected_ship_ = 0;
    paused_ = false;
    initialized_ = true;

    std::cout << "[SquadSkirmish] Initialized with " << population_.size()
              << " variants from '" << variant_name_ << "'\n";
}

// ==================== Input handling ====================

bool SkirmishScreen::handle_input(UIManager& ui) {
    // Tab: cycle through fighters (Swarm → first alive → next alive → ... → Swarm)
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        const auto* arena = tournament_ ? tournament_->current_arena() : nullptr;
        if (arena && !arena->ships().empty()) {
            const auto& ships = arena->ships();
            int total = static_cast<int>(ships.size());
            if (camera_mode_ == CameraMode::Swarm) {
                // Enter follow mode: find first alive ship
                for (int i = 0; i < total; ++i) {
                    if (ships[static_cast<std::size_t>(i)].alive) {
                        selected_ship_ = i;
                        camera_mode_ = CameraMode::Follow;
                        break;
                    }
                }
            } else {
                // Cycle to next alive ship, wrap to Swarm after last
                int start = selected_ship_ + 1;
                bool found = false;
                for (int i = 0; i < total; ++i) {
                    int idx = (start + i) % total;
                    if (ships[static_cast<std::size_t>(idx)].alive) {
                        selected_ship_ = idx;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    camera_mode_ = CameraMode::Swarm;
                    selected_ship_ = -1;
                }
            }
        }
    }

    // F: toggle fighter/squad leader net view in follow mode
    if (ImGui::IsKeyPressed(ImGuiKey_F) && camera_mode_ == CameraMode::Follow) {
        follow_net_view_ = (follow_net_view_ == FollowNetView::Fighter)
            ? FollowNetView::SquadLeader : FollowNetView::Fighter;
    }

    // Escape from follow → swarm first
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && camera_mode_ == CameraMode::Follow) {
        camera_mode_ = CameraMode::Swarm;
        selected_ship_ = -1;
        return false;  // don't exit screen
    }

    // Arrow keys: free camera
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float pan = Camera::PAN_SPEED / camera_.zoom;
    if (keys[SDL_SCANCODE_LEFT])  { camera_.x -= pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_RIGHT]) { camera_.x += pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_UP])    { camera_.y -= pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_DOWN])  { camera_.y += pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }

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
        ui.push_screen(std::make_unique<SkirmishPauseScreen>(
            population_, generation_, ship_design_,
            genome_dir_, variant_name_, evo_config_,
            [this](const EvolutionConfig& updated_config) {
                evo_config_ = updated_config;
                paused_ = false;
            }));
    }

    // Escape: exit
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
        return true;
    }

    return false;
}

// ==================== Evolution ====================

void SkirmishScreen::evolve_generation() {
    const auto& scores = tournament_->variant_scores();
    for (std::size_t i = 0; i < population_.size() && i < scores.size(); ++i) {
        population_[i].fitness = scores[i];
    }

    // Log generation stats
    float best = 0.0f;
    float avg = 0.0f;
    for (const auto& ind : population_) {
        best = std::max(best, ind.fitness);
        avg += ind.fitness;
    }
    avg /= static_cast<float>(population_.size());

    std::cout << "[SquadSkirmish Gen " << generation_ << "] Best: "
              << static_cast<int>(best) << "  Avg: "
              << static_cast<int>(avg) << "\n";

    // Clear stale pointers before population is replaced
    net_viewer_state_.individual = nullptr;
    net_viewer_state_.network = nullptr;

    // Evolve squad leader + NTM only (fighter weights frozen)
    population_ = evolve_squad_only(population_, evo_config_, rng_);
    generation_++;

    // Create new tournament
    tournament_ = std::make_unique<SkirmishTournament>(
        config_, ship_design_, population_, rng_());
}

// ==================== Render: World (SDL) ====================

void SkirmishScreen::render_world(Renderer& renderer) {
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();

    const ArenaSession* arena = tournament_ ? tournament_->current_arena() : nullptr;

    // Update camera based on mode
    if (arena) {
        const auto& ships = arena->ships();
        switch (camera_mode_) {
        case CameraMode::Swarm: {
            float min_x = config_.world_width;
            float max_x = 0.0f;
            float min_y = config_.world_height;
            float max_y = 0.0f;
            int alive_count = 0;

            for (const auto& ship : ships) {
                if (!ship.alive) continue;
                min_x = std::min(min_x, ship.x);
                max_x = std::max(max_x, ship.x);
                min_y = std::min(min_y, ship.y);
                max_y = std::max(max_y, ship.y);
                ++alive_count;
            }

            if (alive_count > 0) {
                float cx = (min_x + max_x) / 2.0f;
                float cy = (min_y + max_y) / 2.0f;
                float span_x = max_x - min_x + 200.0f;
                float span_y = max_y - min_y + 200.0f;

                camera_.x = cx;
                camera_.y = cy;

                float zoom_x = static_cast<float>(game_panel_w) / span_x;
                float zoom_y = static_cast<float>(game_panel_h) / span_y;
                float target_zoom = std::min(zoom_x, zoom_y);
                target_zoom = std::clamp(target_zoom, Camera::MIN_ZOOM, Camera::MAX_ZOOM);
                camera_.zoom += (target_zoom - camera_.zoom) * 0.05f;
            }
            break;
        }
        case CameraMode::Follow: {
            // If selected ship died, find next alive
            if (selected_ship_ >= 0 &&
                static_cast<std::size_t>(selected_ship_) < ships.size() &&
                !ships[static_cast<std::size_t>(selected_ship_)].alive) {
                bool found = false;
                for (std::size_t i = 0; i < ships.size(); ++i) {
                    if (ships[i].alive) {
                        selected_ship_ = static_cast<int>(i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    camera_mode_ = CameraMode::Swarm;
                    selected_ship_ = -1;
                }
            }
            if (selected_ship_ >= 0 &&
                static_cast<std::size_t>(selected_ship_) < ships.size() &&
                ships[static_cast<std::size_t>(selected_ship_)].alive) {
                camera_.x = ships[static_cast<std::size_t>(selected_ship_)].x;
                camera_.y = ships[static_cast<std::size_t>(selected_ship_)].y;
            }
            break;
        }
        }
    }

    camera_.clamp_to_world(config_.world_width, config_.world_height,
                           game_panel_w, game_panel_h);

    SDL_Renderer* sdl = renderer.renderer_;

    // Clip to game panel
    SDL_Rect clip = {0, 0, game_panel_w, game_panel_h};
    SDL_RenderSetClipRect(sdl, &clip);

    // Background
    SDL_SetRenderDrawColor(sdl, 10, 10, 20, 255);
    SDL_RenderFillRect(sdl, &clip);

    if (!arena) {
        SDL_RenderSetClipRect(sdl, nullptr);
        return;
    }

    // Towers
    for (const auto& tower : arena->towers()) {
        if (!tower.alive) continue;
        auto [sx, sy] = camera_.world_to_screen(tower.x, tower.y,
                                                 game_panel_w, game_panel_h);
        float sr = tower.radius * camera_.zoom;
        if (sx + sr < 0 || sx - sr > game_panel_w ||
            sy + sr < 0 || sy - sr > game_panel_h) continue;
        draw_filled_circle(sdl, sx, sy, sr, 120, 120, 130, 200);
    }

    // Tokens
    for (const auto& token : arena->tokens()) {
        if (!token.alive) continue;
        auto [sx, sy] = camera_.world_to_screen(token.x, token.y,
                                                 game_panel_w, game_panel_h);
        float sr = token.radius * camera_.zoom;
        if (sx + sr < 0 || sx - sr > game_panel_w ||
            sy + sr < 0 || sy - sr > game_panel_h) continue;
        draw_filled_circle(sdl, sx, sy, sr, 255, 200, 50, 220);
    }

    // Bases
    for (const auto& base : arena->bases()) {
        if (!base.alive()) continue;
        auto [sx, sy] = camera_.world_to_screen(base.x, base.y,
                                                 game_panel_w, game_panel_h);
        float sr = base.radius * camera_.zoom;

        // Team colors: 0 = blue, 1 = red
        uint8_t br, bg, bb;
        if (base.team_id == 0) {
            br = 100; bg = 180; bb = 255;
        } else {
            br = 255; bg = 100; bb = 100;
        }

        draw_filled_circle(sdl, sx, sy, sr, br, bg, bb, 60);
        draw_circle_outline(sdl, sx, sy, sr, br, bg, bb, 200);

        // HP bar
        float bar_w = sr * 2.0f;
        float bar_h = 4.0f;
        float bar_x = sx - sr;
        float bar_y = sy - sr - 10.0f;
        float hp_frac = base.hp_normalized();

        SDL_SetRenderDrawColor(sdl, 40, 40, 40, 200);
        SDL_FRect bg_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_RenderFillRectF(sdl, &bg_rect);

        uint8_t r = static_cast<uint8_t>((1.0f - hp_frac) * 255);
        uint8_t g = static_cast<uint8_t>(hp_frac * 255);
        SDL_SetRenderDrawColor(sdl, r, g, 0, 230);
        SDL_FRect hp_rect = {bar_x, bar_y, bar_w * hp_frac, bar_h};
        SDL_RenderFillRectF(sdl, &hp_rect);
    }

    // Bullets
    {
        float bullet_size = std::max(2.0f, 3.0f * camera_.zoom);
        for (const auto& bullet : arena->bullets()) {
            if (!bullet.alive) continue;
            auto [sx, sy] = camera_.world_to_screen(bullet.x, bullet.y,
                                                     game_panel_w, game_panel_h);
            if (sx < -bullet_size || sx > game_panel_w + bullet_size ||
                sy < -bullet_size || sy > game_panel_h + bullet_size) continue;
            SDL_SetRenderDrawColor(sdl, 255, 255, 80, 230);
            SDL_FRect rect = {
                sx - bullet_size / 2.0f,
                sy - bullet_size / 2.0f,
                bullet_size,
                bullet_size
            };
            SDL_RenderFillRectF(sdl, &rect);
        }
    }

    // Ships - colored by team
    {
        const auto& ship_teams = tournament_->current_ship_teams();
        float ship_screen_size = Triangle::SIZE * camera_.zoom;
        for (std::size_t i = 0; i < arena->ships().size(); ++i) {
            const auto& ship = arena->ships()[i];
            if (!ship.alive) continue;
            auto [sx, sy] = camera_.world_to_screen(ship.x, ship.y,
                                                     game_panel_w, game_panel_h);
            if (sx + ship_screen_size < 0 || sx - ship_screen_size > game_panel_w ||
                sy + ship_screen_size < 0 || sy - ship_screen_size > game_panel_h) continue;

            // Team 0 = blue, Team 1 = red
            int team = (i < ship_teams.size()) ? ship_teams[i] : 0;
            uint8_t cr, cg, cb;
            if (team == 0) {
                cr = 100; cg = 180; cb = 255;
            } else {
                cr = 255; cg = 100; cb = 100;
            }

            bool is_selected = (static_cast<int>(i) == selected_ship_);
            uint8_t alpha = is_selected ? 255 : 140;
            draw_rotated_triangle(sdl, sx, sy, ship_screen_size,
                                  ship.rotation, cr, cg, cb, alpha);
        }

        // Follow indicator on selected ship
        if (camera_mode_ != CameraMode::Swarm &&
            selected_ship_ >= 0 &&
            static_cast<std::size_t>(selected_ship_) < arena->ships().size() &&
            arena->ships()[static_cast<std::size_t>(selected_ship_)].alive) {
            const auto& ship = arena->ships()[static_cast<std::size_t>(selected_ship_)];
            auto [sx, sy] = camera_.world_to_screen(ship.x, ship.y,
                                                     game_panel_w, game_panel_h);
            float radius = (Triangle::SIZE + 6.0f) * camera_.zoom;
            draw_circle_outline(sdl, sx, sy, radius, 255, 255, 255, 180);
        }
    }

    // Divider line
    SDL_SetRenderDrawColor(sdl, 60, 60, 80, 255);
    SDL_RenderDrawLine(sdl, game_panel_w, 0, game_panel_w, game_panel_h);

    SDL_RenderSetClipRect(sdl, nullptr);
}

// ==================== Render: HUD (ImGui) ====================

void SkirmishScreen::render_hud(Renderer& renderer) {
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;

    const ArenaSession* arena = tournament_ ? tournament_->current_arena() : nullptr;

    // Determine follow mode
    bool in_follow = (camera_mode_ == CameraMode::Follow &&
        selected_ship_ >= 0 && arena &&
        static_cast<std::size_t>(selected_ship_) < arena->ships().size() &&
        arena->ships()[static_cast<std::size_t>(selected_ship_)].alive);

    if (in_follow) {
        // ---- Follow mode: compact HUD + net viewer ----

        // Determine which team this ship belongs to for net viewer
        const auto& ship_teams = tournament_->current_ship_teams();
        int ship_team = (selected_ship_ >= 0 && static_cast<std::size_t>(selected_ship_) < ship_teams.size())
            ? ship_teams[static_cast<std::size_t>(selected_ship_)] : 0;
        auto [matchup_a, matchup_b] = tournament_->current_matchup();
        std::size_t variant_idx = (ship_team == 0) ? matchup_a : matchup_b;

        // Set up net viewer for selected fighter or squad leader
        auto team_idx = static_cast<std::size_t>(ship_team);
        if (follow_net_view_ == FollowNetView::Fighter) {
            net_viewer_state_.individual = &population_[variant_idx].fighter_individual;
            net_viewer_state_.network = tournament_->fighter_net(team_idx);
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::Fighter;
            net_viewer_state_.input_values = last_fighter_input_;
        } else {
            net_viewer_state_.individual = &population_[variant_idx].squad_individual;
            net_viewer_state_.network = tournament_->leader_net(team_idx);
            net_viewer_state_.ship_design = ship_design_;
            net_viewer_state_.net_type = NetType::SquadLeader;
            net_viewer_state_.input_values = last_leader_input_;
        }

        // Compact HUD
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(info_w), 140.0f),
                                 ImGuiCond_Always);
        ImGui::Begin("##SkirmishHUD", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "SQUAD SKIRMISH");
        ImGui::Text("Gen %zu  |  Ship #%d (Team %d, Var %zu)",
                    generation_, selected_ship_, ship_team, variant_idx);
        if (tournament_) {
            ImGui::Text("Round %zu/%zu  Match %zu/%zu  Seed %zu/%zu",
                        tournament_->current_round() + 1, tournament_->total_rounds(),
                        tournament_->current_match() + 1, tournament_->matches_in_round(),
                        tournament_->current_seed() + 1, tournament_->seeds_for_current_round());
        }
        ImGui::Text("Speed: %dx  |  Tab: next ship  |  F: toggle net",
                    ticks_per_frame_);

        // Fighter / Squad Leader toggle buttons
        ImGui::Spacing();
        bool is_fighter = (follow_net_view_ == FollowNetView::Fighter);
        if (is_fighter) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
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

        // Net viewer below HUD
        int net_y = 160;
        net_viewer_state_.render_x = info_x;
        net_viewer_state_.render_y = net_y;
        net_viewer_state_.render_w = info_w;
        net_viewer_state_.render_h = game_panel_h - net_y - 10;

        draw_net_viewer_view(net_viewer_state_, renderer.renderer_);

    } else {
        // ---- Swarm mode: full info panel ----
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(info_w),
                   static_cast<float>(game_panel_h - 20)),
            ImGuiCond_Always);
        ImGui::Begin("##SkirmishInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "SQUAD SKIRMISH");
        ImGui::Separator();

        ImGui::Text("Generation:  %zu", generation_);

        if (tournament_) {
            ImGui::Text("Round:       %zu / %zu",
                        tournament_->current_round() + 1,
                        tournament_->total_rounds());
            ImGui::Text("Match:       %zu / %zu",
                        tournament_->current_match() + 1,
                        tournament_->matches_in_round());
            ImGui::Text("Seed:        %zu / %zu",
                        tournament_->current_seed() + 1,
                        tournament_->seeds_for_current_round());

            auto [a_idx, b_idx] = tournament_->current_matchup();
            ImGui::Text("Matchup:     Var %zu vs Var %zu", a_idx, b_idx);
        }

        ImGui::Text("Speed:       %dx", ticks_per_frame_);
        ImGui::Text("Camera:      %s", camera_mode_str(static_cast<int>(camera_mode_)));

        if (arena) {
            int alive = 0;
            for (const auto& s : arena->ships()) if (s.alive) ++alive;
            ImGui::Text("Alive:       %d / %zu", alive, arena->ships().size());
            ImGui::Text("Tick:        %u", arena->current_tick());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Leaderboard
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Leaderboard");
        if (tournament_) {
            const auto& scores = tournament_->variant_scores();
            std::vector<std::size_t> sorted_idx(scores.size());
            for (std::size_t i = 0; i < sorted_idx.size(); ++i) sorted_idx[i] = i;
            std::sort(sorted_idx.begin(), sorted_idx.end(),
                [&](std::size_t a, std::size_t b) {
                    return scores[a] > scores[b];
                });

            for (std::size_t rank = 0; rank < std::min<std::size_t>(10, sorted_idx.size()); ++rank) {
                std::size_t idx = sorted_idx[rank];
                char label[64];
                std::snprintf(label, sizeof(label), "#%zu: Var %zu  (%d pts)",
                    rank + 1, idx, static_cast<int>(scores[idx]));
                ImGui::Text("%s", label);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (paused_) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "** PAUSED **");
            ImGui::Spacing();
        }

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Controls");
        ImGui::Text("Tab:   Cycle fighters");
        ImGui::Text("F:     Toggle Fighter/Squad net");
        ImGui::Text("1-4:   Speed (1x/5x/20x/100x)");
        ImGui::Text("Space: Pause");
        ImGui::Text("Esc:   Back");

        ImGui::End();
    }
}

// ==================== Main draw ====================

void SkirmishScreen::on_draw(AppState& state, Renderer& renderer,
                              UIManager& ui) {
    if (!initialized_) {
        initialize(state);
    }

    if (handle_input(ui)) return;  // screen was popped

    // Tick (unless paused)
    if (!paused_) {
        for (int t = 0; t < ticks_per_frame_; ++t) {
            if (tournament_ && tournament_->step()) {
                evolve_generation();
                break;
            }
        }
    }

    render_world(renderer);
    render_hud(renderer);
}

void SkirmishScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(net_viewer_state_, sdl_renderer);
}

} // namespace neuroflyer

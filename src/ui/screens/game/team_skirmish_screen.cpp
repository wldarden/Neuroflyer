#include <neuroflyer/ui/screens/team_skirmish_screen.h>
#include <neuroflyer/ui/screens/team_skirmish_pause_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/evolution.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/snapshot_io.h>
#include <neuroflyer/snapshot_utils.h>
#include <neuroflyer/team_evolution.h>

#include <imgui.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <numbers>

namespace neuroflyer {

namespace {

// ---- Team colors (8 teams max) ----
struct TeamColor { uint8_t r, g, b; };
constexpr TeamColor k_team_colors[] = {
    {100, 150, 255},  // team 0: blue
    {255, 100, 100},  // team 1: red
    {100, 255, 100},  // team 2: green
    {255, 255, 100},  // team 3: yellow
    {100, 255, 255},  // team 4: cyan
    {255, 100, 255},  // team 5: magenta
    {255, 180, 100},  // team 6: orange
    {220, 220, 220},  // team 7: white
};
constexpr std::size_t k_num_team_colors =
    sizeof(k_team_colors) / sizeof(k_team_colors[0]);

// Squad interior colors: subtle tints per squad index
struct SquadColor { uint8_t r, g, b; };
constexpr SquadColor k_squad_colors[] = {
    {60, 120, 180},   // squad 0: steel blue
    {180, 100, 60},   // squad 1: burnt orange
    {60, 160, 80},    // squad 2: forest green
    {160, 60, 160},   // squad 3: purple
    {160, 160, 60},   // squad 4: olive
    {60, 160, 160},   // squad 5: teal
    {160, 100, 100},  // squad 6: dusty rose
    {100, 100, 160},  // squad 7: slate
};
constexpr std::size_t k_num_squad_colors =
    sizeof(k_squad_colors) / sizeof(k_squad_colors[0]);

// --- SDL drawing helpers ---

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

void draw_filled_triangle(SDL_Renderer* renderer,
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
    float min_y = std::min({y0, y1, y2});
    float max_y = std::max({y0, y1, y2});
    for (int sy = static_cast<int>(min_y); sy <= static_cast<int>(max_y); ++sy) {
        float fy = static_cast<float>(sy) + 0.5f;
        float xs[3];
        int count = 0;
        auto edge = [&](float ax, float ay, float bx, float by) {
            if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) {
                float t = (fy - ay) / (by - ay);
                xs[count++] = ax + t * (bx - ax);
            }
        };
        edge(x0, y0, x1, y1);
        edge(x1, y1, x2, y2);
        edge(x2, y2, x0, y0);
        if (count >= 2) {
            float left = std::min(xs[0], xs[1]);
            float right = std::max(xs[0], xs[1]);
            SDL_RenderDrawLineF(renderer, left, fy, right, fy);
        }
    }
}

void draw_damage_effects(SDL_Renderer* renderer,
                         float cx, float cy, float size,
                         float rotation, int damage_level) {
    if (damage_level <= 0) return;

    float cos_r = std::cos(rotation);
    float sin_r = std::sin(rotation);
    auto rotate = [&](float lx, float ly, float& ox, float& oy) {
        ox = cx + lx * cos_r - ly * sin_r;
        oy = cy + lx * sin_r + ly * cos_r;
    };

    SDL_SetRenderDrawColor(renderer, 255, 200, 50, 200);
    float ax, ay, bx, by, mx, my;
    rotate(-size * 0.4f, -size * 0.3f, ax, ay);
    rotate(size * 0.3f, size * 0.2f, bx, by);
    SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
    rotate(size * 0.3f, -size * 0.5f, ax, ay);
    rotate(0.0f, 0.0f, mx, my);
    SDL_RenderDrawLineF(renderer, ax, ay, mx, my);
    rotate(-size * 0.5f, size * 0.4f, bx, by);
    SDL_RenderDrawLineF(renderer, mx, my, bx, by);
    rotate(-size * 0.15f, -size * 0.6f, ax, ay);
    rotate(size * 0.2f, -size * 0.1f, bx, by);
    SDL_RenderDrawLineF(renderer, ax, ay, bx, by);

    if (damage_level >= 2) {
        float flame_len = size * 1.2f;
        SDL_SetRenderDrawColor(renderer, 255, 140, 30, 180);
        rotate(0.0f, size * 0.6f, ax, ay);
        rotate(0.0f, size * 0.6f + flame_len, bx, by);
        SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
        SDL_SetRenderDrawColor(renderer, 255, 80, 20, 160);
        rotate(-size * 0.3f, size * 0.5f, ax, ay);
        rotate(-size * 0.15f, size * 0.5f + flame_len * 0.7f, bx, by);
        SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
        rotate(size * 0.3f, size * 0.5f, ax, ay);
        rotate(size * 0.15f, size * 0.5f + flame_len * 0.7f, bx, by);
        SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
        SDL_SetRenderDrawColor(renderer, 255, 230, 80, 200);
        rotate(0.0f, size * 0.7f, ax, ay);
        rotate(0.0f, size * 0.7f + flame_len * 0.5f, bx, by);
        SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
    }
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

} // anonymous namespace

// ==================== Construction ====================

TeamSkirmishScreen::TeamSkirmishScreen(TeamSkirmishConfig config,
                                        EvolutionConfig evo_config)
    : config_(std::move(config))
    , evo_config_(std::move(evo_config)) {}

TeamSkirmishScreen::~TeamSkirmishScreen() {
    destroy_net_viewer_view(net_viewer_state_);
}

// ==================== Initialization ====================

void TeamSkirmishScreen::initialize(AppState& state) {
    rng_.seed(static_cast<uint32_t>(state.rng()));

    // Use the first team's fighter snapshot for ship design
    if (!config_.team_seeds.empty()) {
        ship_design_ = config_.team_seeds[0].fighter_snapshot.ship_design;
    }

    // Build team pools
    team_pools_.clear();
    team_pools_.reserve(config_.team_seeds.size());

    auto target_squad_ids = build_squad_leader_input_labels();

    for (auto& seed : config_.team_seeds) {
        auto squad_ind = snapshot_to_individual(seed.squad_snapshot);
        auto fighter_ind = snapshot_to_individual(seed.fighter_snapshot);

        // Convert to fighter if needed
        if (seed.fighter_snapshot.net_type != NetType::Fighter) {
            fighter_ind = convert_variant_to_fighter(fighter_ind, ship_design_);
        }

        // Adapt squad net if topology doesn't match current expected inputs
        if (squad_ind.topology.input_size != target_squad_ids.size() ||
            (!squad_ind.topology.input_ids.empty() &&
             squad_ind.topology.input_ids != target_squad_ids)) {
            if (squad_ind.topology.input_ids.empty()) {
                squad_ind.topology.input_ids = {
                    "Sqd HP", "Home Sin", "Home Cos", "Home Dst", "Home HP",
                    "Spacing", "Cmd Sin", "Cmd Cos", "Cmd Dst",
                    "Threat?", "Thr Sin", "Thr Cos", "Thr Dst", "Thr Scr"
                };
                squad_ind.topology.output_ids = build_squad_leader_output_labels();
            }
            auto [adapted, report] = adapt_individual_inputs(
                squad_ind, target_squad_ids, ship_design_, rng_);
            if (report.needed()) {
                std::cout << "[TeamSkirmish] Adapted squad net: "
                          << report.message() << "\n";
            }
            squad_ind = adapted;
        }

        // Create population for this team
        TeamPool pool;
        pool.seed = seed;
        pool.squad_population.clear();
        pool.squad_population.reserve(evo_config_.population_size);

        for (std::size_t i = 0; i < evo_config_.population_size; ++i) {
            auto team = TeamIndividual::create(
                ship_design_, {8},
                NtmNetConfig{}, SquadLeaderNetConfig{},
                rng_, &fighter_ind, &squad_ind);
            pool.squad_population.push_back(std::move(team));
        }

        // Diversify all but the first
        for (std::size_t i = 1; i < pool.squad_population.size(); ++i) {
            apply_mutations(pool.squad_population[i].ntm_individual,
                            evo_config_, rng_);
            apply_mutations(pool.squad_population[i].squad_individual,
                            evo_config_, rng_);
        }

        // Fighter population (separate from squad evolution)
        pool.fighter_population.clear();
        pool.fighter_population.reserve(evo_config_.population_size);
        for (std::size_t i = 0; i < evo_config_.population_size; ++i) {
            Individual fi = fighter_ind;
            if (i > 0) apply_mutations(fi, evo_config_, rng_);
            pool.fighter_population.push_back(std::move(fi));
        }

        pool.squad_scores.assign(evo_config_.population_size, 0.0f);
        pool.fighter_scores.assign(evo_config_.population_size, 0.0f);

        team_pools_.push_back(std::move(pool));
    }

    // Camera at world center
    camera_.x = config_.arena.world.world_width / 2.0f;
    camera_.y = config_.arena.world.world_height / 2.0f;
    camera_.zoom = 0.15f;
    camera_.following = false;
    camera_mode_ = CameraMode::Swarm;

    // Create first session
    uint32_t seed = static_cast<uint32_t>(rng_());
    session_ = std::make_unique<TeamSkirmishSession>(
        config_, ship_design_, team_pools_, seed);

    generation_ = 1;
    ticks_per_frame_ = 1;
    selected_ship_ = -1;
    paused_ = false;
    initialized_ = true;

    std::cout << "[TeamSkirmish] Initialized with " << team_pools_.size()
              << " teams, " << evo_config_.population_size
              << " variants each\n";
}

// ==================== Input ====================

bool TeamSkirmishScreen::handle_input(UIManager& ui) {
    // Tab: cycle through fighters
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        const auto* arena = session_ ? session_->current_arena() : nullptr;
        if (arena && !arena->ships().empty()) {
            const auto& ships = arena->ships();
            int total = static_cast<int>(ships.size());
            if (camera_mode_ == CameraMode::Swarm) {
                for (int i = 0; i < total; ++i) {
                    if (ships[static_cast<std::size_t>(i)].alive) {
                        selected_ship_ = i;
                        camera_mode_ = CameraMode::Follow;
                        break;
                    }
                }
            } else {
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

    // F: toggle fighter/squad leader net view
    if (ImGui::IsKeyPressed(ImGuiKey_F) && camera_mode_ == CameraMode::Follow) {
        follow_net_view_ = (follow_net_view_ == FollowNetView::Fighter)
            ? FollowNetView::SquadLeader : FollowNetView::Fighter;
    }

    // Escape from follow → swarm first, then exit
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (camera_mode_ == CameraMode::Follow) {
            camera_mode_ = CameraMode::Swarm;
            selected_ship_ = -1;
            return false;
        }
        ui.pop_screen();
        return true;
    }

    // Arrow keys: free camera pan
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float pan = Camera::PAN_SPEED / camera_.zoom;
    if (keys[SDL_SCANCODE_LEFT])  { camera_.x -= pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_RIGHT]) { camera_.x += pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_UP])    { camera_.y -= pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }
    if (keys[SDL_SCANCODE_DOWN])  { camera_.y += pan; camera_mode_ = CameraMode::Swarm; selected_ship_ = -1; }

    // Mouse scroll: zoom
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) camera_.adjust_zoom(wheel * 0.05f);

    // Speed controls
    if (keys[SDL_SCANCODE_1]) ticks_per_frame_ = 1;
    if (keys[SDL_SCANCODE_2]) ticks_per_frame_ = 5;
    if (keys[SDL_SCANCODE_3]) ticks_per_frame_ = 20;
    if (keys[SDL_SCANCODE_4]) ticks_per_frame_ = 100;

    // Space: open pause screen
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = true;
        ui.push_screen(std::make_unique<TeamSkirmishPauseScreen>(
            team_pools_, generation_, ship_design_, evo_config_,
            [this](const EvolutionConfig& updated_config) {
                evo_config_ = updated_config;
                paused_ = false;
            }));
    }

    return false;
}

// ==================== Evolution ====================

void TeamSkirmishScreen::evolve_generation() {
    // Pull updated pools from session (scores accumulated during matches)
    team_pools_ = std::move(session_->team_pools());

    // Log generation stats
    float best_sq = 0.0f, avg_sq = 0.0f;
    std::size_t total_sq = 0;
    for (auto& pool : team_pools_) {
        for (float s : pool.squad_scores) {
            best_sq = std::max(best_sq, s);
            avg_sq += s;
            ++total_sq;
        }
    }
    if (total_sq > 0) avg_sq /= static_cast<float>(total_sq);

    std::cout << "[TeamSkirmish Gen " << generation_
              << "] Best squad: " << static_cast<int>(best_sq)
              << "  Avg squad: " << static_cast<int>(avg_sq) << "\n";

    // Clear stale net viewer pointers before population replacement
    net_viewer_state_.individual = nullptr;
    net_viewer_state_.network = nullptr;

    // Evolve each team's pools
    for (auto& pool : team_pools_) {
        // Assign squad scores to squad population fitness
        for (std::size_t i = 0; i < pool.squad_population.size() &&
                                  i < pool.squad_scores.size(); ++i) {
            pool.squad_population[i].fitness = pool.squad_scores[i];
        }
        // Evolve squad leader + NTM (fighter weights frozen)
        pool.squad_population = evolve_squad_only(
            pool.squad_population, evo_config_, rng_);

        // Assign fighter scores and evolve fighter population
        for (std::size_t i = 0; i < pool.fighter_population.size() &&
                                  i < pool.fighter_scores.size(); ++i) {
            pool.fighter_population[i].fitness = pool.fighter_scores[i];
        }
        pool.fighter_population = evolve_population(
            pool.fighter_population, evo_config_, rng_);

        // Reset scores for next generation
        pool.squad_scores.assign(pool.squad_population.size(), 0.0f);
        pool.fighter_scores.assign(pool.fighter_population.size(), 0.0f);
    }

    ++generation_;

    // Create new session with evolved pools
    session_ = std::make_unique<TeamSkirmishSession>(
        config_, ship_design_, team_pools_, static_cast<uint32_t>(rng_()));
}

// ==================== Render: World (SDL) ====================

void TeamSkirmishScreen::render_world(Renderer& renderer) {
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();

    const ArenaSession* arena = session_ ? session_->current_arena() : nullptr;

    // Update camera based on mode
    if (arena) {
        const auto& ships = arena->ships();
        switch (camera_mode_) {
        case CameraMode::Swarm: {
            float min_x = config_.arena.world.world_width;
            float max_x = 0.0f;
            float min_y = config_.arena.world.world_height;
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
                float target_zoom = std::clamp(
                    std::min(zoom_x, zoom_y), Camera::MIN_ZOOM, Camera::MAX_ZOOM);
                camera_.zoom += (target_zoom - camera_.zoom) * 0.05f;
            }
            break;
        }
        case CameraMode::Follow: {
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

    camera_.clamp_to_world(config_.arena.world.world_width, config_.arena.world.world_height,
                           game_panel_w, game_panel_h);

    SDL_Renderer* sdl = renderer.renderer_;

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

    // Bases — colored by team
    for (const auto& base : arena->bases()) {
        if (!base.alive()) continue;
        auto [sx, sy] = camera_.world_to_screen(base.x, base.y,
                                                 game_panel_w, game_panel_h);
        float sr = base.radius * camera_.zoom;

        auto team_idx = static_cast<std::size_t>(base.team_id)
                         % k_num_team_colors;
        const auto& tc = k_team_colors[team_idx];

        draw_filled_circle(sdl, sx, sy, sr, tc.r, tc.g, tc.b, 60);
        draw_circle_outline(sdl, sx, sy, sr, tc.r, tc.g, tc.b, 200);

        // HP bar
        float bar_w = sr * 2.0f;
        float bar_h = 4.0f;
        float bar_x = sx - sr;
        float bar_y = sy - sr - 10.0f;
        float hp_frac = base.hp_normalized();

        SDL_SetRenderDrawColor(sdl, 40, 40, 40, 200);
        SDL_FRect bg_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_RenderFillRectF(sdl, &bg_rect);

        uint8_t hr = static_cast<uint8_t>((1.0f - hp_frac) * 255);
        uint8_t hg = static_cast<uint8_t>(hp_frac * 255);
        SDL_SetRenderDrawColor(sdl, hr, hg, 0, 230);
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
                bullet_size, bullet_size
            };
            SDL_RenderFillRectF(sdl, &rect);
        }
    }

    // Ships — colored by team
    {
        const auto& ship_teams = session_->ship_teams();
        float ship_screen_size = Triangle::SIZE * camera_.zoom;

        for (std::size_t i = 0; i < arena->ships().size(); ++i) {
            const auto& ship = arena->ships()[i];
            if (!ship.alive) continue;
            auto [sx, sy] = camera_.world_to_screen(ship.x, ship.y,
                                                     game_panel_w, game_panel_h);
            if (sx + ship_screen_size < 0 || sx - ship_screen_size > game_panel_w ||
                sy + ship_screen_size < 0 || sy - ship_screen_size > game_panel_h) continue;

            // Team color from ship_teams
            int team = (i < ship_teams.size()) ? ship_teams[i] : 0;
            auto team_idx = static_cast<std::size_t>(team) % k_num_team_colors;
            const auto& tc = k_team_colors[team_idx];

            // Squad interior color
            int squad = arena->squad_of(i);
            auto sq_idx = static_cast<std::size_t>(squad) % k_num_squad_colors;
            const auto& sqc = k_squad_colors[sq_idx];

            float inner_size = ship_screen_size * 0.7f;
            bool is_selected = (static_cast<int>(i) == selected_ship_);
            uint8_t fill_alpha = is_selected ? 120 : 60;
            draw_filled_triangle(sdl, sx, sy, inner_size,
                                 ship.rotation, sqc.r, sqc.g, sqc.b, fill_alpha);

            uint8_t outline_alpha = is_selected ? 255 : 140;
            draw_rotated_triangle(sdl, sx, sy, ship_screen_size,
                                  ship.rotation, tc.r, tc.g, tc.b, outline_alpha);
            draw_damage_effects(sdl, sx, sy, ship_screen_size,
                                ship.rotation, ship.damage_level());
        }

        // Follow indicator on selected ship
        if (camera_mode_ == CameraMode::Follow &&
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

void TeamSkirmishScreen::render_hud(Renderer& renderer) {
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;

    const ArenaSession* arena = session_ ? session_->current_arena() : nullptr;

    bool in_follow = (camera_mode_ == CameraMode::Follow &&
        selected_ship_ >= 0 && arena &&
        static_cast<std::size_t>(selected_ship_) < arena->ships().size() &&
        arena->ships()[static_cast<std::size_t>(selected_ship_)].alive);

    if (in_follow) {
        // ---- Follow mode: compact HUD + net viewer ----

        const auto& ship_teams = session_->ship_teams();
        int ship_team = (selected_ship_ >= 0 &&
            static_cast<std::size_t>(selected_ship_) < ship_teams.size())
            ? ship_teams[static_cast<std::size_t>(selected_ship_)] : 0;

        auto si = static_cast<std::size_t>(selected_ship_);

        // Determine which team pool to use for the individual
        auto team_pool_idx = static_cast<std::size_t>(ship_team);
        if (team_pool_idx < team_pools_.size()) {
            auto& pool = team_pools_[team_pool_idx];
            // Use assignment to find which squad/fighter index this ship corresponds to
            const auto& assignments = session_->ship_assignments();
            if (si < assignments.size()) {
                const auto& asgn = assignments[si];
                if (follow_net_view_ == FollowNetView::Fighter) {
                    if (asgn.fighter_index < pool.squad_population.size()) {
                        net_viewer_state_.individual =
                            &pool.squad_population[asgn.fighter_index].fighter_individual;
                    }
                    net_viewer_state_.network = session_->fighter_net(si);
                    net_viewer_state_.ship_design = ship_design_;
                    net_viewer_state_.net_type = NetType::Fighter;
                    const auto& fi = session_->last_fighter_inputs();
                    if (si < fi.size()) net_viewer_state_.input_values = fi[si];
                } else {
                    if (asgn.squad_index < pool.squad_population.size()) {
                        net_viewer_state_.individual =
                            &pool.squad_population[asgn.squad_index].squad_individual;
                    }
                    net_viewer_state_.network = session_->leader_net(si);
                    net_viewer_state_.ship_design = ship_design_;
                    net_viewer_state_.net_type = NetType::SquadLeader;
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(info_w), 120.0f),
                                 ImGuiCond_Always);
        ImGui::Begin("##TeamSkirmishHUD", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "TEAM SKIRMISH");
        ImGui::Text("Gen %zu  |  Ship #%d (Team %d)",
                    generation_, selected_ship_, ship_team);
        if (session_) {
            ImGui::Text("Match %zu / %zu  |  Speed: %dx",
                        session_->current_match_index() + 1,
                        session_->total_matches(),
                        ticks_per_frame_);
        }

        ImGui::Spacing();
        bool is_fighter = (follow_net_view_ == FollowNetView::Fighter);
        if (is_fighter) ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
        if (ImGui::Button("Fighter", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::Fighter;
        }
        if (is_fighter) ImGui::PopStyleColor();

        ImGui::SameLine();
        bool is_leader = (follow_net_view_ == FollowNetView::SquadLeader);
        if (is_leader) ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(0.5f, 0.4f, 0.2f, 1.0f));
        if (ImGui::Button("Squad Leader", ImVec2(info_w / 2.0f - 8.0f, 0))) {
            follow_net_view_ = FollowNetView::SquadLeader;
        }
        if (is_leader) ImGui::PopStyleColor();

        ImGui::End();

        // Net viewer below HUD
        int net_y = 140;
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
        ImGui::Begin("##TeamSkirmishInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "TEAM SKIRMISH");
        ImGui::Separator();

        ImGui::Text("Generation:  %zu", generation_);

        if (session_) {
            ImGui::Text("Match:       %zu / %zu",
                        session_->current_match_index() + 1,
                        session_->total_matches());
        }

        ImGui::Text("Speed:       %dx", ticks_per_frame_);

        if (arena) {
            int alive = 0;
            for (const auto& s : arena->ships()) if (s.alive) ++alive;
            ImGui::Text("Alive:       %d / %zu", alive, arena->ships().size());
            ImGui::Text("Tick:        %u", arena->current_tick());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Per-team scores
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Team Scores");
        const auto& ship_teams = session_ ? session_->ship_teams() : std::vector<int>{};
        for (std::size_t t = 0; t < team_pools_.size(); ++t) {
            const auto& tc = k_team_colors[t % k_num_team_colors];
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f, 1.0f));

            // Count alive ships for this team
            int team_alive = 0;
            for (std::size_t si = 0; si < ship_teams.size(); ++si) {
                if (ship_teams[si] == static_cast<int>(t) &&
                    arena && si < arena->ships().size() &&
                    arena->ships()[si].alive) {
                    ++team_alive;
                }
            }

            float best_squad = 0.0f;
            for (float s : team_pools_[t].squad_scores) {
                best_squad = std::max(best_squad, s);
            }

            ImGui::Text("Team %zu: %d alive, best squad: %d pts",
                t + 1, team_alive, static_cast<int>(best_squad));
            ImGui::PopStyleColor();
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

void TeamSkirmishScreen::on_draw(AppState& state, Renderer& renderer,
                                  UIManager& ui) {
    if (!initialized_) {
        initialize(state);
    }

    if (handle_input(ui)) return;

    if (!paused_ && session_) {
        auto frame_start = std::chrono::steady_clock::now();

        for (int t = 0; t < ticks_per_frame_; ++t) {
            if (session_->step()) {
                evolve_generation();
                break;
            }
        }

        // Fill remaining frame time with background work
        constexpr int kFrameBudgetMs = 33;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - frame_start);
        int remaining = kFrameBudgetMs - static_cast<int>(elapsed.count());
        if (remaining > 0 && session_) {
            session_->run_background_work(remaining);
        }
    }

    render_world(renderer);
    render_hud(renderer);
}

void TeamSkirmishScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(net_viewer_state_, sdl_renderer);
}

} // namespace neuroflyer

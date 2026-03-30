#include <neuroflyer/ui/screens/fighter_drill_screen.h>
#include <neuroflyer/ui/ui_manager.h>

#include <neuroflyer/app_state.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/sensor_engine.h>
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

// --- SDL drawing helpers (same as arena_game_view.cpp) ---

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

const char* phase_name(DrillPhase phase) {
    switch (phase) {
    case DrillPhase::Expand:   return "Expand";
    case DrillPhase::Contract: return "Contract";
    case DrillPhase::Attack:   return "Attack";
    case DrillPhase::Done:     return "Done";
    }
    return "?";
}

} // anonymous namespace

namespace {

const char* camera_mode_str(int mode) {
    switch (mode) {
    case 0: return "SWARM";
    case 1: return "BEST";
    case 2: return "WORST";
    }
    return "?";
}

} // anonymous namespace

// ==================== Construction ====================

FighterDrillScreen::FighterDrillScreen(Snapshot source_snapshot,
                                       std::string genome_dir,
                                       std::string variant_name)
    : source_snapshot_(std::move(source_snapshot))
    , genome_dir_(std::move(genome_dir))
    , variant_name_(std::move(variant_name)) {}

FighterDrillScreen::~FighterDrillScreen() {
    destroy_net_viewer_view(net_viewer_state_);
}

// ==================== Lifecycle ====================

void FighterDrillScreen::on_enter() {
    initialized_ = false;
}

// ==================== Initialization ====================

void FighterDrillScreen::initialize(AppState& state) {
    ship_design_ = source_snapshot_.ship_design;

    // Seed local RNG from app state
    rng_.seed(static_cast<uint32_t>(state.rng()));

    // Convert source snapshot to fighter format if needed
    auto source_ind = snapshot_to_individual(source_snapshot_);
    if (source_snapshot_.net_type != NetType::Fighter) {
        source_ind = convert_variant_to_fighter(source_ind, ship_design_);
    }

    // Rebuild a snapshot that has fighter topology for population seeding
    Snapshot fighter_snap = source_snapshot_;
    fighter_snap.topology = source_ind.topology;
    fighter_snap.weights = source_ind.genome.flatten("layer_");
    fighter_snap.net_type = NetType::Fighter;

    // Create population from fighter snapshot
    evo_config_.population_size = drill_config_.population_size;
    evo_config_.elitism_count = 3;
    population_ = create_population_from_snapshot(
        fighter_snap, drill_config_.population_size, evo_config_, rng_);

    // Build networks
    nets_.clear();
    nets_.reserve(population_.size());
    for (const auto& ind : population_) {
        nets_.push_back(ind.build_network());
    }

    // Init recurrent states
    recurrent_states_.assign(
        population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    // Create first session
    uint32_t seed = static_cast<uint32_t>(rng_());
    session_ = std::make_unique<FighterDrillSession>(drill_config_, seed);

    // Camera setup
    camera_.x = drill_config_.world_width / 2.0f;
    camera_.y = drill_config_.world_height / 2.0f;
    camera_.zoom = 0.3f;
    camera_.following = false;
    camera_mode_ = CameraMode::Swarm;

    generation_ = 1;
    ticks_per_frame_ = 1;
    selected_ship_ = 0;
    paused_ = false;
    initialized_ = true;

    std::cout << "[FighterDrill] Initialized with " << population_.size()
              << " fighters from variant '" << variant_name_ << "'\n";
}

// ==================== Input handling ====================

void FighterDrillScreen::handle_input(UIManager& ui) {
    // Tab: cycle camera mode
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        switch (camera_mode_) {
        case CameraMode::Swarm:  camera_mode_ = CameraMode::Best;  break;
        case CameraMode::Best:   camera_mode_ = CameraMode::Worst; break;
        case CameraMode::Worst:  camera_mode_ = CameraMode::Swarm; break;
        }
    }

    // Arrow keys: free camera
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float pan = Camera::PAN_SPEED / camera_.zoom;
    if (keys[SDL_SCANCODE_LEFT])  { camera_.x -= pan; camera_mode_ = CameraMode::Swarm; }
    if (keys[SDL_SCANCODE_RIGHT]) { camera_.x += pan; camera_mode_ = CameraMode::Swarm; }
    if (keys[SDL_SCANCODE_UP])    { camera_.y -= pan; camera_mode_ = CameraMode::Swarm; }
    if (keys[SDL_SCANCODE_DOWN])  { camera_.y += pan; camera_mode_ = CameraMode::Swarm; }

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

    // Space: pause (placeholder — Task 7 will wire real pause screen)
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        paused_ = !paused_;
    }

    // Escape: exit
    if (!ui.input_blocked() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ui.pop_screen();
    }
}

// ==================== Tick ====================

void FighterDrillScreen::run_tick() {
    if (!session_ || session_->is_over()) return;

    DrillPhase phase = session_->phase();
    const auto& ships = session_->ships();

    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        // Compute scripted squad inputs per phase
        float spacing = 0.0f;
        float aggression = 0.0f;
        float squad_target_heading = 0.0f;
        float squad_target_distance = 0.0f;

        switch (phase) {
        case DrillPhase::Expand:
            spacing = 1.0f;
            aggression = 0.0f;
            squad_target_heading = 0.0f;
            squad_target_distance = 0.0f;
            break;
        case DrillPhase::Contract:
            spacing = -1.0f;
            aggression = 0.0f;
            squad_target_heading = 0.0f;
            squad_target_distance = 0.0f;
            break;
        case DrillPhase::Attack: {
            spacing = 0.0f;
            aggression = 1.0f;
            // Compute heading/distance to starbase
            const auto& starbase = session_->starbase();
            auto dr = compute_dir_range(
                ships[i].x, ships[i].y,
                starbase.x, starbase.y,
                drill_config_.world_width, drill_config_.world_height);
            // Convert to relative heading (same math as compute_squad_leader_fighter_inputs)
            float abs_heading = std::atan2(dr.dir_sin, dr.dir_cos);
            float rel = abs_heading - ships[i].rotation;
            // Normalize to [-pi, pi]
            while (rel > std::numbers::pi_v<float>) rel -= 2.0f * std::numbers::pi_v<float>;
            while (rel < -std::numbers::pi_v<float>) rel += 2.0f * std::numbers::pi_v<float>;
            squad_target_heading = rel / std::numbers::pi_v<float>;  // normalize to [-1, 1]
            squad_target_distance = dr.range;
            break;
        }
        case DrillPhase::Done:
            break;
        }

        // Compute center heading/distance
        float squad_center_heading = 0.0f;
        float squad_center_distance = 0.0f;
        {
            auto dr = compute_dir_range(
                ships[i].x, ships[i].y,
                session_->squad_center_x(), session_->squad_center_y(),
                drill_config_.world_width, drill_config_.world_height);
            float abs_heading = std::atan2(dr.dir_sin, dr.dir_cos);
            float rel = abs_heading - ships[i].rotation;
            while (rel > std::numbers::pi_v<float>) rel -= 2.0f * std::numbers::pi_v<float>;
            while (rel < -std::numbers::pi_v<float>) rel += 2.0f * std::numbers::pi_v<float>;
            squad_center_heading = rel / std::numbers::pi_v<float>;
            squad_center_distance = dr.range;
        }

        // Build arena sensor input context
        // The drill world has no enemy ships or teams — use empty ship_teams
        ArenaQueryContext ctx;
        ctx.ship_x = ships[i].x;
        ctx.ship_y = ships[i].y;
        ctx.ship_rotation = ships[i].rotation;
        ctx.world_w = drill_config_.world_width;
        ctx.world_h = drill_config_.world_height;
        ctx.self_index = i;
        ctx.self_team = 0;
        ctx.towers = session_->towers();
        ctx.tokens = session_->tokens();
        ctx.ships = session_->ships();
        // All ships on same team (0) — no friendly/enemy distinction needed
        static const std::vector<int> empty_teams;
        ctx.ship_teams = std::span<const int>();
        ctx.bullets = session_->bullets();

        auto input = build_arena_ship_input(
            ship_design_, ctx,
            squad_target_heading, squad_target_distance,
            squad_center_heading, squad_center_distance,
            aggression, spacing,
            recurrent_states_[i]);

        // Capture input for followed ship
        if (static_cast<int>(i) == selected_ship_) {
            last_input_ = input;
        }

        auto output = nets_[i].forward(input);
        auto decoded = decode_output(output, ship_design_.memory_slots);

        session_->set_ship_actions(i, decoded.up, decoded.down,
                                   decoded.left, decoded.right, decoded.shoot);
        recurrent_states_[i] = decoded.memory;
    }

    session_->tick();
}

// ==================== Evolution ====================

void FighterDrillScreen::evolve_generation(AppState& /*state*/) {
    // Assign fitness from session scores
    auto scores = session_->get_scores();
    for (std::size_t i = 0; i < population_.size() && i < scores.size(); ++i) {
        population_[i].fitness = scores[i];
    }

    float best = 0.0f;
    float avg = 0.0f;
    for (const auto& ind : population_) {
        best = std::max(best, ind.fitness);
        avg += ind.fitness;
    }
    avg /= static_cast<float>(population_.size());

    std::cout << "[FighterDrill Gen " << generation_ << "] Best: "
              << static_cast<int>(best) << "  Avg: "
              << static_cast<int>(avg) << "\n";

    // Clear stale individual pointer BEFORE population is replaced
    net_viewer_state_.individual = nullptr;
    net_viewer_state_.network = nullptr;

    // Evolve
    population_ = evolve_population(population_, evo_config_, rng_);

    // Rebuild networks
    nets_.clear();
    nets_.reserve(population_.size());
    for (const auto& ind : population_) {
        nets_.push_back(ind.build_network());
    }

    // Reset recurrent states
    recurrent_states_.assign(
        population_.size(),
        std::vector<float>(ship_design_.memory_slots, 0.0f));

    generation_++;

    // Create new session
    uint32_t seed = static_cast<uint32_t>(rng_());
    session_ = std::make_unique<FighterDrillSession>(drill_config_, seed);
}

// ==================== Render: World (SDL) ====================

void FighterDrillScreen::render_world(Renderer& renderer) {
    if (!session_) return;

    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();

    // Update camera based on mode
    switch (camera_mode_) {
    case CameraMode::Swarm: {
        // Zoom to fit bounding box of all alive ships
        float min_x = drill_config_.world_width;
        float max_x = 0.0f;
        float min_y = drill_config_.world_height;
        float max_y = 0.0f;
        int alive_count = 0;

        for (const auto& ship : session_->ships()) {
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
            float span_x = max_x - min_x + 200.0f;  // margin
            float span_y = max_y - min_y + 200.0f;

            camera_.x = cx;
            camera_.y = cy;

            float zoom_x = static_cast<float>(game_panel_w) / span_x;
            float zoom_y = static_cast<float>(game_panel_h) / span_y;
            float target_zoom = std::min(zoom_x, zoom_y);
            target_zoom = std::clamp(target_zoom, Camera::MIN_ZOOM, Camera::MAX_ZOOM);
            // Smooth zoom
            camera_.zoom += (target_zoom - camera_.zoom) * 0.05f;
        }
        break;
    }
    case CameraMode::Best: {
        float best_score = -std::numeric_limits<float>::max();
        int best_idx = 0;
        auto scores = session_->get_scores();
        for (std::size_t i = 0; i < session_->ships().size(); ++i) {
            if (!session_->ships()[i].alive) continue;
            float s = (i < scores.size()) ? scores[i] : 0.0f;
            if (s > best_score) {
                best_score = s;
                best_idx = static_cast<int>(i);
            }
        }
        selected_ship_ = best_idx;
        if (best_idx >= 0 &&
            static_cast<std::size_t>(best_idx) < session_->ships().size() &&
            session_->ships()[static_cast<std::size_t>(best_idx)].alive) {
            camera_.x = session_->ships()[static_cast<std::size_t>(best_idx)].x;
            camera_.y = session_->ships()[static_cast<std::size_t>(best_idx)].y;
        }
        break;
    }
    case CameraMode::Worst: {
        float worst_score = std::numeric_limits<float>::max();
        int worst_idx = 0;
        auto scores = session_->get_scores();
        for (std::size_t i = 0; i < session_->ships().size(); ++i) {
            if (!session_->ships()[i].alive) continue;
            float s = (i < scores.size()) ? scores[i] : 0.0f;
            if (s < worst_score) {
                worst_score = s;
                worst_idx = static_cast<int>(i);
            }
        }
        selected_ship_ = worst_idx;
        if (worst_idx >= 0 &&
            static_cast<std::size_t>(worst_idx) < session_->ships().size() &&
            session_->ships()[static_cast<std::size_t>(worst_idx)].alive) {
            camera_.x = session_->ships()[static_cast<std::size_t>(worst_idx)].x;
            camera_.y = session_->ships()[static_cast<std::size_t>(worst_idx)].y;
        }
        break;
    }
    }

    // Clamp camera to world bounds
    camera_.clamp_to_world(drill_config_.world_width, drill_config_.world_height,
                           game_panel_w, game_panel_h);

    SDL_Renderer* sdl = renderer.renderer_;

    // Clip to game panel
    SDL_Rect clip = {0, 0, game_panel_w, game_panel_h};
    SDL_RenderSetClipRect(sdl, &clip);

    // Background
    SDL_SetRenderDrawColor(sdl, 10, 10, 20, 255);
    SDL_RenderFillRect(sdl, &clip);

    // Towers
    for (const auto& tower : session_->towers()) {
        if (!tower.alive) continue;
        auto [sx, sy] = camera_.world_to_screen(tower.x, tower.y,
                                                 game_panel_w, game_panel_h);
        float sr = tower.radius * camera_.zoom;
        if (sx + sr < 0 || sx - sr > game_panel_w ||
            sy + sr < 0 || sy - sr > game_panel_h) continue;
        draw_filled_circle(sdl, sx, sy, sr, 120, 120, 130, 200);
    }

    // Tokens
    for (const auto& token : session_->tokens()) {
        if (!token.alive) continue;
        auto [sx, sy] = camera_.world_to_screen(token.x, token.y,
                                                 game_panel_w, game_panel_h);
        float sr = token.radius * camera_.zoom;
        if (sx + sr < 0 || sx - sr > game_panel_w ||
            sy + sr < 0 || sy - sr > game_panel_h) continue;
        draw_filled_circle(sdl, sx, sy, sr, 255, 200, 50, 220);
    }

    // Starbase
    {
        const auto& base = session_->starbase();
        if (base.alive()) {
            auto [sx, sy] = camera_.world_to_screen(base.x, base.y,
                                                     game_panel_w, game_panel_h);
            float sr = base.radius * camera_.zoom;
            // Red-tinted enemy starbase
            draw_filled_circle(sdl, sx, sy, sr, 255, 80, 80, 60);
            draw_circle_outline(sdl, sx, sy, sr, 255, 80, 80, 200);

            // HP bar
            float bar_w = sr * 2.0f;
            float bar_h = 4.0f;
            float bar_x = sx - sr;
            float bar_y = sy - sr - 10.0f;
            float hp_frac = base.hp_normalized();

            SDL_SetRenderDrawColor(sdl, 40, 40, 40, 200);
            SDL_FRect bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_RenderFillRectF(sdl, &bg);

            uint8_t r = static_cast<uint8_t>((1.0f - hp_frac) * 255);
            uint8_t g = static_cast<uint8_t>(hp_frac * 255);
            SDL_SetRenderDrawColor(sdl, r, g, 0, 230);
            SDL_FRect hp_rect = {bar_x, bar_y, bar_w * hp_frac, bar_h};
            SDL_RenderFillRectF(sdl, &hp_rect);
        }
    }

    // Bullets
    {
        float bullet_size = std::max(2.0f, 3.0f * camera_.zoom);
        for (const auto& bullet : session_->bullets()) {
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

    // Ships
    {
        float ship_screen_size = Triangle::SIZE * camera_.zoom;
        for (std::size_t i = 0; i < session_->ships().size(); ++i) {
            const auto& ship = session_->ships()[i];
            if (!ship.alive) continue;
            auto [sx, sy] = camera_.world_to_screen(ship.x, ship.y,
                                                     game_panel_w, game_panel_h);
            if (sx + ship_screen_size < 0 || sx - ship_screen_size > game_panel_w ||
                sy + ship_screen_size < 0 || sy - ship_screen_size > game_panel_h) continue;

            bool is_selected = (static_cast<int>(i) == selected_ship_);
            // All ships are blue (same team), brighter if selected
            uint8_t alpha = is_selected ? 255 : 100;
            draw_rotated_triangle(sdl, sx, sy, ship_screen_size,
                                  ship.rotation,
                                  100, 180, 255, alpha);
        }

        // Follow indicator on selected ship
        if (camera_mode_ != CameraMode::Swarm &&
            selected_ship_ >= 0 &&
            static_cast<std::size_t>(selected_ship_) < session_->ships().size() &&
            session_->ships()[static_cast<std::size_t>(selected_ship_)].alive) {
            const auto& ship = session_->ships()[static_cast<std::size_t>(selected_ship_)];
            auto [sx, sy] = camera_.world_to_screen(ship.x, ship.y,
                                                     game_panel_w, game_panel_h);
            float radius = (Triangle::SIZE + 6.0f) * camera_.zoom;
            draw_circle_outline(sdl, sx, sy, radius, 255, 255, 255, 180);
        }
    }

    // Squad center marker (small cross)
    {
        auto [sx, sy] = camera_.world_to_screen(
            session_->squad_center_x(), session_->squad_center_y(),
            game_panel_w, game_panel_h);
        SDL_SetRenderDrawColor(sdl, 200, 200, 200, 120);
        float cs = 8.0f;
        SDL_RenderDrawLineF(sdl, sx - cs, sy, sx + cs, sy);
        SDL_RenderDrawLineF(sdl, sx, sy - cs, sx, sy + cs);
    }

    // Divider line
    SDL_SetRenderDrawColor(sdl, 60, 60, 80, 255);
    SDL_RenderDrawLine(sdl, game_panel_w, 0, game_panel_w, game_panel_h);

    // Clear clip
    SDL_RenderSetClipRect(sdl, nullptr);
}

// ==================== Render: HUD (ImGui) ====================

void FighterDrillScreen::render_hud(Renderer& renderer) {
    int game_panel_w = renderer.game_w();
    int game_panel_h = renderer.screen_h();
    int info_x = game_panel_w + 10;
    int info_w = renderer.net_w() - 20;

    // Determine if we should show net viewer (follow mode)
    bool show_net_viewer = camera_mode_ != CameraMode::Swarm
        && selected_ship_ >= 0
        && static_cast<std::size_t>(selected_ship_) < session_->ships().size()
        && session_->ships()[static_cast<std::size_t>(selected_ship_)].alive;

    if (show_net_viewer) {
        // Configure net viewer state
        net_viewer_state_.individual = &population_[static_cast<std::size_t>(selected_ship_)];
        net_viewer_state_.network = &nets_[static_cast<std::size_t>(selected_ship_)];
        net_viewer_state_.ship_design = ship_design_;
        net_viewer_state_.net_type = NetType::Fighter;
        net_viewer_state_.input_values = last_input_;

        // HUD info above net viewer
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(info_w), 100.0f),
                                 ImGuiCond_Always);
        ImGui::Begin("##DrillHUD", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "FIGHTER DRILL");
        ImGui::Text("Gen %zu  |  Phase: %s  |  %s",
                    generation_, phase_name(session_->phase()),
                    camera_mode_str(static_cast<int>(camera_mode_)));

        // Phase progress bar
        float phase_progress = 1.0f;
        if (session_->phase() != DrillPhase::Done) {
            uint32_t remaining = session_->phase_ticks_remaining();
            phase_progress = 1.0f - static_cast<float>(remaining) /
                static_cast<float>(drill_config_.phase_duration_ticks);
        }
        ImGui::ProgressBar(phase_progress, ImVec2(-1, 14));

        // Alive count
        int alive = 0;
        for (const auto& s : session_->ships()) if (s.alive) ++alive;
        ImGui::Text("Alive: %d / %zu  |  Tick: %u  |  Speed: %dx",
                    alive, session_->ships().size(),
                    session_->current_tick(), ticks_per_frame_);

        ImGui::End();

        // Net viewer below HUD
        int net_y = 120;
        net_viewer_state_.render_x = info_x;
        net_viewer_state_.render_y = net_y;
        net_viewer_state_.render_w = info_w;
        net_viewer_state_.render_h = game_panel_h - net_y - 10;

        draw_net_viewer_view(net_viewer_state_, renderer.renderer_);
    } else {
        // Full info panel in swarm mode
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(info_x), 10.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(info_w),
                   static_cast<float>(game_panel_h - 20)),
            ImGuiCond_Always);
        ImGui::Begin("##DrillInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "FIGHTER DRILL");
        ImGui::Separator();

        ImGui::Text("Generation:   %zu", generation_);
        ImGui::Text("Phase:        %s", phase_name(session_->phase()));

        // Phase progress bar
        float phase_progress = 1.0f;
        if (session_->phase() != DrillPhase::Done) {
            uint32_t remaining = session_->phase_ticks_remaining();
            phase_progress = 1.0f - static_cast<float>(remaining) /
                static_cast<float>(drill_config_.phase_duration_ticks);
        }
        ImGui::ProgressBar(phase_progress, ImVec2(-1, 14));

        ImGui::Text("Tick:         %u", session_->current_tick());
        ImGui::Text("Speed:        %dx", ticks_per_frame_);
        ImGui::Text("Camera:       %s", camera_mode_str(static_cast<int>(camera_mode_)));

        int alive = 0;
        for (const auto& s : session_->ships()) if (s.alive) ++alive;
        ImGui::Text("Alive:        %d / %zu", alive, session_->ships().size());

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Starbase");
        ImGui::Text("HP:           %.0f / %.0f",
                    session_->starbase().hp, session_->starbase().max_hp);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Top Fighters");
        auto scores = session_->get_scores();

        // Sort indices by score
        std::vector<std::size_t> sorted_idx(scores.size());
        for (std::size_t i = 0; i < sorted_idx.size(); ++i) sorted_idx[i] = i;
        std::sort(sorted_idx.begin(), sorted_idx.end(),
            [&](std::size_t a, std::size_t b) {
                return scores[a] > scores[b];
            });

        for (std::size_t rank = 0; rank < std::min<std::size_t>(10, sorted_idx.size()); ++rank) {
            std::size_t idx = sorted_idx[rank];
            bool alive_ship = session_->ships()[idx].alive;
            char label[64];
            std::snprintf(label, sizeof(label), "#%zu: %d%s",
                rank + 1, static_cast<int>(scores[idx]),
                alive_ship ? "" : " (dead)");
            if (alive_ship) {
                ImGui::Text("%s", label);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", label);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Controls");
        ImGui::Text("Tab:   Cycle camera");
        ImGui::Text("1-4:   Speed (1x/5x/20x/100x)");
        ImGui::Text("Space: Pause");
        ImGui::Text("Esc:   Exit");
        if (paused_) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "** PAUSED **");
        }

        ImGui::End();
    }
}

// ==================== Main draw ====================

void FighterDrillScreen::on_draw(AppState& state, Renderer& renderer,
                                  UIManager& ui) {
    if (!initialized_) {
        initialize(state);
    }

    handle_input(ui);

    // Tick (unless paused)
    if (!paused_) {
        for (int t = 0; t < ticks_per_frame_; ++t) {
            run_tick();

            if (session_ && session_->is_over()) {
                evolve_generation(state);
                break;
            }
        }
    }

    // Render world (SDL) and HUD (ImGui)
    render_world(renderer);
    render_hud(renderer);
}

void FighterDrillScreen::post_render(SDL_Renderer* sdl_renderer) {
    flush_net_viewer_view(net_viewer_state_, sdl_renderer);
}

} // namespace neuroflyer

#include <neuroflyer/fighter_drill_session.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace neuroflyer {

FighterDrillSession::FighterDrillSession(const FighterDrillConfig& config, uint32_t seed)
    : config_(config)
    , starbase_(0.0f, 0.0f, config.starbase_radius, config.starbase_hp, 1)  // team 1 = enemy
    , squad_center_x_(config.world_width / 2.0f)
    , squad_center_y_(config.world_height / 2.0f)
    , rng_(seed)
{
    scores_.resize(config_.population_size, 0.0f);
    tokens_collected_.resize(config_.population_size, 0);
    spawn_ships();
    spawn_obstacles();
    spawn_starbase();
}

void FighterDrillSession::spawn_ships() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);

    for (std::size_t i = 0; i < config_.population_size; ++i) {
        Triangle ship(squad_center_x_, squad_center_y_);
        ship.rotation = angle_dist(rng_);
        ship.rotation_speed = config_.rotation_speed;
        ships_.push_back(ship);
    }
}

void FighterDrillSession::spawn_obstacles() {
    std::uniform_real_distribution<float> x_dist(0.0f, config_.world_width);
    std::uniform_real_distribution<float> y_dist(0.0f, config_.world_height);
    std::uniform_real_distribution<float> radius_dist(15.0f, 35.0f);

    for (std::size_t i = 0; i < config_.tower_count; ++i) {
        Tower t;
        t.x = x_dist(rng_);
        t.y = y_dist(rng_);
        t.radius = radius_dist(rng_);
        t.alive = true;
        towers_.push_back(t);
    }
    for (std::size_t i = 0; i < config_.token_count; ++i) {
        Token tok;
        tok.x = x_dist(rng_);
        tok.y = y_dist(rng_);
        tok.alive = true;
        tokens_.push_back(tok);
    }
}

void FighterDrillSession::spawn_starbase() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);
    float angle = angle_dist(rng_);
    starbase_ = Base(
        squad_center_x_ + config_.starbase_distance * std::cos(angle),
        squad_center_y_ + config_.starbase_distance * std::sin(angle),
        config_.starbase_radius,
        config_.starbase_hp,
        1);  // team 1 = enemy
}

uint32_t FighterDrillSession::phase_ticks_remaining() const noexcept {
    if (phase_ == DrillPhase::Done) return 0;
    return config_.phase_duration_ticks - phase_tick_;
}

void FighterDrillSession::set_ship_actions(
    std::size_t idx, bool up, bool down, bool left, bool right, bool shoot) {
    if (idx < ships_.size() && ships_[idx].alive) {
        ships_[idx].apply_arena_actions(up, down, left, right, shoot);
    }
}

void FighterDrillSession::tick() {
    // Stub — implemented in Task 3
}

} // namespace neuroflyer

#include <neuroflyer/fighter_drill_session.h>
#include <neuroflyer/collision.h>

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
    if (phase_ == DrillPhase::Done) return;

    // 1. Update ship positions
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        ship.x += ship.dx;
        ship.y += ship.dy;
    }

    // 2. Apply boundary wrapping (toroidal)
    apply_boundary_rules();

    // 3. Spawn bullets from ships that want to shoot
    spawn_bullets_from_ships();

    // 4. Update bullets (directional movement + despawn at max_range)
    update_bullets();

    // 5. Collision resolution
    resolve_ship_tower_collisions();
    resolve_ship_token_collisions();
    resolve_bullet_starbase_collisions();
    resolve_bullet_tower_collisions();

    // 6. Phase-based movement scoring
    compute_phase_scores();

    // 7. Decrement shoot cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 8. Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // 9. Advance tick counters
    ++tick_count_;
    ++phase_tick_;

    // 10. Phase transitions
    if (phase_tick_ >= config_.phase_duration_ticks) {
        phase_tick_ = 0;
        switch (phase_) {
            case DrillPhase::Expand:   phase_ = DrillPhase::Contract; break;
            case DrillPhase::Contract: phase_ = DrillPhase::Attack;   break;
            case DrillPhase::Attack:   phase_ = DrillPhase::Done;     break;
            case DrillPhase::Done:     break;
        }
    }
}

void FighterDrillSession::apply_boundary_rules() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;

        if (config_.wrap_ew) {
            if (ship.x < 0.0f) ship.x += config_.world_width;
            else if (ship.x > config_.world_width) ship.x -= config_.world_width;
        } else {
            ship.x = std::clamp(ship.x, 0.0f, config_.world_width);
        }

        if (config_.wrap_ns) {
            if (ship.y < 0.0f) ship.y += config_.world_height;
            else if (ship.y > config_.world_height) ship.y -= config_.world_height;
        } else {
            ship.y = std::clamp(ship.y, 0.0f, config_.world_height);
        }
    }
}

void FighterDrillSession::spawn_bullets_from_ships() {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        auto& ship = ships_[i];
        if (!ship.alive) continue;
        if (ship.wants_shoot && ship.shoot_cooldown <= 0) {
            Bullet b;
            float fx = std::sin(ship.rotation);
            float fy = -std::cos(ship.rotation);
            b.x = ship.x + fx * Triangle::SIZE;
            b.y = ship.y + fy * Triangle::SIZE;
            b.dir_x = fx;
            b.dir_y = fy;
            b.alive = true;
            b.owner_index = static_cast<int>(i);
            b.distance_traveled = 0.0f;
            b.max_range = config_.bullet_max_range;
            bullets_.push_back(b);
            ship.shoot_cooldown = ship.fire_cooldown;
        }
    }
}

void FighterDrillSession::update_bullets() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        b.update_directional();
    }
}

void FighterDrillSession::resolve_ship_tower_collisions() {
    // Implemented in Task 4
}

void FighterDrillSession::resolve_ship_token_collisions() {
    // Implemented in Task 4
}

void FighterDrillSession::resolve_bullet_starbase_collisions() {
    // Implemented in Task 4
}

void FighterDrillSession::resolve_bullet_tower_collisions() {
    // Implemented in Task 4
}

void FighterDrillSession::compute_phase_scores() {
    // Implemented in Task 5
}

} // namespace neuroflyer

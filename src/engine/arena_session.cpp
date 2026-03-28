#include <neuroflyer/arena_session.h>
#include <neuroflyer/collision.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace neuroflyer {

ArenaSession::ArenaSession(const ArenaConfig& config, uint32_t seed)
    : config_(config), rng_(seed) {
    std::size_t pop = config.population_size();

    // Build team assignments: ship i belongs to team i / team_size
    team_assignments_.resize(pop);
    for (std::size_t i = 0; i < pop; ++i) {
        team_assignments_[i] = static_cast<int>(i / config.team_size);
    }

    survival_ticks_.resize(pop, 0.0f);

    spawn_ships();
    spawn_obstacles();
}

void ArenaSession::spawn_ships() {
    float center_x = config_.world_width / 2.0f;
    float center_y = config_.world_height / 2.0f;
    float radius = std::min(config_.world_width, config_.world_height) / 2.0f;
    float inner = radius / 3.0f;
    float outer = radius * 2.0f / 3.0f;

    std::size_t num_teams = config_.num_teams;
    float slice_angle = 2.0f * static_cast<float>(M_PI) / static_cast<float>(num_teams);

    for (std::size_t i = 0; i < config_.population_size(); ++i) {
        int team = team_assignments_[i];
        float team_start = static_cast<float>(team) * slice_angle;

        // Random angle within this team's slice
        std::uniform_real_distribution<float> angle_dist(team_start, team_start + slice_angle);
        std::uniform_real_distribution<float> radius_dist(inner, outer);

        float angle = angle_dist(rng_);
        float r = radius_dist(rng_);

        float sx = center_x + r * std::cos(angle);
        float sy = center_y + r * std::sin(angle);

        Triangle ship(sx, sy);
        ship.rotation_speed = config_.rotation_speed;

        // Face toward center: rotation = atan2(to_center_x, -to_center_y)
        float to_center_x = center_x - sx;
        float to_center_y = center_y - sy;
        ship.rotation = std::atan2(to_center_x, -to_center_y);

        ships_.push_back(ship);
    }
}

void ArenaSession::spawn_obstacles() {
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

void ArenaSession::tick() {
    if (over_) return;

    // 1. Update ship positions
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        ship.x += ship.dx;
        ship.y += ship.dy;
    }

    // 2. Apply boundary rules
    apply_boundary_rules();

    // 3. Spawn bullets from ships that want to shoot
    spawn_bullets_from_ships();

    // 4. Update bullets
    update_bullets();

    // 5. Resolve bullet-ship collisions
    resolve_bullet_ship_collisions();

    // 6. Resolve bullet-tower collisions
    resolve_bullet_tower_collisions();

    // 7. Resolve ship-tower collisions
    resolve_ship_tower_collisions();

    // 8. Resolve ship-token collisions
    resolve_ship_token_collisions();

    // 9. Increment survival ticks for alive ships
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            survival_ticks_[i] += 1.0f;
        }
    }

    // 10. Decrement shoot cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 11. Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // 12. Increment tick count
    ++tick_count_;

    // 13. Check end conditions
    check_end_conditions();
}

void ArenaSession::set_ship_actions(std::size_t idx,
                                     bool up, bool down, bool left, bool right,
                                     bool shoot) {
    if (idx < ships_.size() && ships_[idx].alive) {
        ships_[idx].apply_arena_actions(up, down, left, right, shoot);
    }
}

void ArenaSession::apply_boundary_rules() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;

        // East-West
        if (config_.wrap_ew) {
            if (ship.x < 0.0f) ship.x += config_.world_width;
            else if (ship.x > config_.world_width) ship.x -= config_.world_width;
        } else {
            ship.x = std::clamp(ship.x, 0.0f, config_.world_width);
        }

        // North-South
        if (config_.wrap_ns) {
            if (ship.y < 0.0f) ship.y += config_.world_height;
            else if (ship.y > config_.world_height) ship.y -= config_.world_height;
        } else {
            ship.y = std::clamp(ship.y, 0.0f, config_.world_height);
        }
    }
}

void ArenaSession::spawn_bullets_from_ships() {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        auto& ship = ships_[i];
        if (!ship.alive) continue;
        if (ship.wants_shoot && ship.shoot_cooldown <= 0) {
            Bullet b;
            // Spawn bullet at ship's nose (facing direction)
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

void ArenaSession::update_bullets() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        b.update_directional();

        // Destroy at world boundary
        if (b.x < 0.0f || b.x > config_.world_width ||
            b.y < 0.0f || b.y > config_.world_height) {
            b.alive = false;
        }
    }
}

void ArenaSession::resolve_bullet_ship_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (!ships_[i].alive) continue;
            // Skip self-hits
            if (b.owner_index == static_cast<int>(i)) continue;
            // Check vertex-based collision plus center proximity.
            // bullet_triangle_collision uses fixed (non-rotated) vertex offsets,
            // so we also check distance to the ship center for robustness.
            bool hit = bullet_triangle_collision(b.x, b.y, ships_[i]);
            if (!hit) {
                float dx = b.x - ships_[i].x;
                float dy = b.y - ships_[i].y;
                hit = (dx * dx + dy * dy) < (Triangle::SIZE * Triangle::SIZE);
            }
            if (hit) {
                ships_[i].alive = false;
                b.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_bullet_tower_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& tower : towers_) {
            if (!tower.alive) continue;
            if (bullet_circle_collision(b.x, b.y, tower.x, tower.y, tower.radius)) {
                tower.alive = false;
                b.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_ship_tower_collisions() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        for (const auto& tower : towers_) {
            if (!tower.alive) continue;
            if (triangle_circle_collision(ship, tower.x, tower.y, tower.radius)) {
                ship.alive = false;
                break;
            }
        }
    }
}

void ArenaSession::resolve_ship_token_collisions() {
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        for (auto& tok : tokens_) {
            if (!tok.alive) continue;
            float dx = ship.x - tok.x;
            float dy = ship.y - tok.y;
            float dist_sq = dx * dx + dy * dy;
            float hit_r = tok.radius + Triangle::SIZE;
            if (dist_sq < hit_r * hit_r) {
                tok.alive = false;
            }
        }
    }
}

void ArenaSession::add_bullet(const Bullet& b) {
    bullets_.push_back(b);
}

bool ArenaSession::is_over() const noexcept {
    return over_;
}

int ArenaSession::team_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < team_assignments_.size()) {
        return team_assignments_[ship_idx];
    }
    return -1;
}

std::vector<float> ArenaSession::get_scores() const {
    std::vector<float> scores(config_.num_teams, 0.0f);
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        int team = team_assignments_[i];
        // 1 point per second at 60fps: survival_ticks / 60
        scores[static_cast<std::size_t>(team)] += survival_ticks_[i] / 60.0f;
    }
    return scores;
}

std::size_t ArenaSession::alive_count() const noexcept {
    std::size_t count = 0;
    for (const auto& ship : ships_) {
        if (ship.alive) ++count;
    }
    return count;
}

std::size_t ArenaSession::teams_alive() const {
    std::set<int> alive_teams;
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            alive_teams.insert(team_assignments_[i]);
        }
    }
    return alive_teams.size();
}

void ArenaSession::check_end_conditions() {
    // Time limit reached
    if (tick_count_ >= config_.time_limit_ticks) {
        over_ = true;
        return;
    }

    // Only 1 or 0 teams alive
    if (teams_alive() <= 1) {
        over_ = true;
    }
}

} // namespace neuroflyer

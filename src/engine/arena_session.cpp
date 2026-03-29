#include <neuroflyer/arena_session.h>
#include <neuroflyer/collision.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace neuroflyer {

ArenaSession::ArenaSession(const ArenaConfig& config, uint32_t seed)
    : config_(config), rng_(seed) {
    std::size_t pop = config.population_size();

    // Build team assignments: ship i belongs to team i / ships_per_team
    std::size_t ships_per_team = config.num_squads * config.fighters_per_squad;
    team_assignments_.resize(pop);
    for (std::size_t i = 0; i < pop; ++i) {
        team_assignments_[i] = static_cast<int>(i / ships_per_team);
    }

    squad_assignments_.resize(pop);
    for (std::size_t i = 0; i < pop; ++i) {
        std::size_t within_team = i % (config.num_squads * config.fighters_per_squad);
        squad_assignments_[i] = static_cast<int>(within_team / config.fighters_per_squad);
    }

    survival_ticks_.resize(pop, 0.0f);
    tokens_collected_.resize(pop, 0);
    enemy_kills_.resize(pop, 0);
    ally_kills_.resize(pop, 0);

    spawn_ships();
    spawn_bases();
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

void ArenaSession::spawn_bases() {
    float center_x = config_.world_width / 2.0f;
    float center_y = config_.world_height / 2.0f;
    float radius = std::min(config_.world_width, config_.world_height) / 2.0f;
    float base_ring = radius * 0.5f;  // bases at 50% of arena radius
    float slice_angle = 2.0f * static_cast<float>(M_PI) / static_cast<float>(config_.num_teams);

    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        float angle = static_cast<float>(t) * slice_angle + slice_angle / 2.0f;
        float bx = center_x + base_ring * std::cos(angle);
        float by = center_y + base_ring * std::sin(angle);
        bases_.emplace_back(bx, by, config_.base_radius, config_.base_hp, static_cast<int>(t));
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

    // 6. Resolve bullet-base collisions
    resolve_bullet_base_collisions();

    // 7. Resolve bullet-tower collisions
    resolve_bullet_tower_collisions();

    // 8. Resolve ship-tower collisions
    resolve_ship_tower_collisions();

    // 9. Resolve ship-token collisions
    resolve_ship_token_collisions();

    // 10. Increment survival ticks for alive ships
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            survival_ticks_[i] += 1.0f;
        }
    }

    // 11. Decrement shoot cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 12. Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // 13. Increment tick count
    ++tick_count_;

    // 14. Check end conditions
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
                // Track kill: compare killer's team to victim's team
                auto killer = static_cast<std::size_t>(b.owner_index);
                if (team_assignments_[killer] == team_assignments_[i]) {
                    ally_kills_[killer]++;
                } else {
                    enemy_kills_[killer]++;
                }
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

void ArenaSession::resolve_bullet_base_collisions() {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& base : bases_) {
            if (!base.alive()) continue;
            // Skip friendly bullets
            if (b.owner_index >= 0) {
                int shooter_team = team_assignments_[static_cast<std::size_t>(b.owner_index)];
                if (shooter_team == base.team_id) continue;
            }
            if (bullet_circle_collision(b.x, b.y, base.x, base.y, base.radius)) {
                base.take_damage(config_.base_bullet_damage);
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
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;
        for (auto& tok : tokens_) {
            if (!tok.alive) continue;
            float dx = ships_[i].x - tok.x;
            float dy = ships_[i].y - tok.y;
            float dist_sq = dx * dx + dy * dy;
            float hit_r = tok.radius + Triangle::SIZE;
            if (dist_sq < hit_r * hit_r) {
                tok.alive = false;
                tokens_collected_[i]++;
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

int ArenaSession::squad_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < squad_assignments_.size()) {
        return squad_assignments_[ship_idx];
    }
    return -1;
}

SquadStats ArenaSession::compute_squad_stats(int team, int squad) const {
    SquadStats stats;
    float count = 0, alive_count = 0;
    float sum_x = 0, sum_y = 0;

    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (team_assignments_[i] != team || squad_assignments_[i] != squad) continue;
        count += 1.0f;
        if (!ships_[i].alive) continue;
        alive_count += 1.0f;
        sum_x += ships_[i].x;
        sum_y += ships_[i].y;
    }

    if (count == 0.0f) return stats;
    stats.alive_fraction = alive_count / count;

    if (alive_count > 0.0f) {
        stats.centroid_x = sum_x / alive_count;
        stats.centroid_y = sum_y / alive_count;

        auto t = static_cast<std::size_t>(team);
        if (t < bases_.size()) {
            float dx_home = stats.centroid_x - bases_[t].x;
            float dy_home = stats.centroid_y - bases_[t].y;
            float dist_home = std::sqrt(dx_home * dx_home + dy_home * dy_home);
            float diag = std::sqrt(config_.world_width * config_.world_width +
                                    config_.world_height * config_.world_height);
            stats.avg_dist_to_home = dist_home / diag;
            if (dist_home > 0.0f) {
                stats.centroid_dir_sin = dx_home / dist_home;
                stats.centroid_dir_cos = dy_home / dist_home;
            }

            float min_enemy_dist = diag;
            for (const auto& base : bases_) {
                if (base.team_id == team) continue;
                float dx_e = stats.centroid_x - base.x;
                float dy_e = stats.centroid_y - base.y;
                min_enemy_dist = std::min(min_enemy_dist,
                    std::sqrt(dx_e * dx_e + dy_e * dy_e));
            }
            stats.avg_dist_to_enemy_base = min_enemy_dist / diag;
        }
    }

    return stats;
}

std::vector<float> ArenaSession::get_scores() const {
    std::vector<float> scores(config_.num_teams, 0.0f);
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        int team = team_assignments_[i];
        auto t = static_cast<std::size_t>(team);
        // 1 point per second at 60fps: survival_ticks / 60
        scores[t] += survival_ticks_[i] / 60.0f;
        // +1000 per enemy kill, -1000 per ally kill
        scores[t] += static_cast<float>(enemy_kills_[i]) * 1000.0f;
        scores[t] -= static_cast<float>(ally_kills_[i]) * 1000.0f;
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
    if (tick_count_ >= config_.time_limit_ticks) {
        over_ = true;
        return;
    }

    // Count teams with alive bases
    std::size_t bases_alive = 0;
    for (const auto& base : bases_) {
        if (base.alive()) ++bases_alive;
    }
    if (bases_alive <= 1) {
        over_ = true;
        return;
    }

    // Also end if only 1 or 0 teams have alive ships
    if (teams_alive() <= 1) {
        over_ = true;
    }
}

} // namespace neuroflyer

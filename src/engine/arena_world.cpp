#include <neuroflyer/arena_world.h>
#include <neuroflyer/collision.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace neuroflyer {

ArenaWorld::ArenaWorld(const ArenaWorldConfig& config, uint32_t seed)
    : config_(config), rng_(seed) {
    const std::size_t pop = config.population_size();

    // Build team assignments: ship i belongs to team i / ships_per_team
    const std::size_t ships_per_team = config.num_squads * config.fighters_per_squad;
    team_assignments_.resize(pop);
    for (std::size_t i = 0; i < pop; ++i) {
        team_assignments_[i] = static_cast<int>(i / ships_per_team);
    }

    squad_assignments_.resize(pop);
    for (std::size_t i = 0; i < pop; ++i) {
        const std::size_t within_team = i % (config.num_squads * config.fighters_per_squad);
        squad_assignments_[i] = static_cast<int>(within_team / config.fighters_per_squad);
    }

    enemy_kills_.resize(pop, 0);
    ally_kills_.resize(pop, 0);

    spawn_ships();
    spawn_bases();
    spawn_obstacles();
}

void ArenaWorld::spawn_ships() {
    const float center_x = config_.world_width / 2.0f;
    const float center_y = config_.world_height / 2.0f;
    const float radius = std::min(config_.world_width, config_.world_height) / 2.0f;

    const std::size_t num_teams = config_.num_teams;
    const std::size_t num_squads = config_.num_squads;
    const float slice_angle = 2.0f * std::numbers::pi_v<float> / static_cast<float>(num_teams);

    const float squad_radius = radius * 0.5f;
    const float cluster_radius = std::min(radius * 0.08f, 80.0f);

    for (std::size_t i = 0; i < config_.population_size(); ++i) {
        const int team = team_assignments_[i];
        const int squad = squad_assignments_[i];
        const float team_start = static_cast<float>(team) * slice_angle;

        // Squad center: evenly spaced within team's angular slice
        const float squad_frac = (static_cast<float>(squad) + 0.5f) / static_cast<float>(num_squads);
        const float squad_angle = team_start + squad_frac * slice_angle;
        const float squad_cx = center_x + squad_radius * std::cos(squad_angle);
        const float squad_cy = center_y + squad_radius * std::sin(squad_angle);

        // Fighter: random offset within cluster_radius of squad center
        std::uniform_real_distribution<float> offset_angle(
            0.0f, 2.0f * std::numbers::pi_v<float>);
        std::uniform_real_distribution<float> offset_dist(0.0f, cluster_radius);
        const float oa = offset_angle(rng_);
        const float od = offset_dist(rng_);

        const float sx = squad_cx + od * std::cos(oa);
        const float sy = squad_cy + od * std::sin(oa);

        Triangle ship(sx, sy);
        ship.rotation_speed = config_.rotation_speed;
        ship.hp = config_.ship_hp;
        ship.max_hp = config_.ship_hp;

        // Face toward center
        const float to_center_x = center_x - sx;
        const float to_center_y = center_y - sy;
        ship.rotation = std::atan2(to_center_x, -to_center_y);

        ships_.push_back(ship);
    }
}

void ArenaWorld::spawn_bases() {
    const float center_x = config_.world_width / 2.0f;
    const float center_y = config_.world_height / 2.0f;
    const float radius = std::min(config_.world_width, config_.world_height) / 2.0f;
    const float base_ring = radius * 0.5f;
    const float slice_angle = 2.0f * std::numbers::pi_v<float> / static_cast<float>(config_.num_teams);

    for (std::size_t t = 0; t < config_.num_teams; ++t) {
        const float angle = static_cast<float>(t) * slice_angle + slice_angle / 2.0f;
        const float bx = center_x + base_ring * std::cos(angle);
        const float by = center_y + base_ring * std::sin(angle);
        bases_.emplace_back(bx, by, config_.base_radius, config_.base_hp, static_cast<int>(t));
    }
}

void ArenaWorld::spawn_obstacles() {
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

TickEvents ArenaWorld::tick() {
    TickEvents events;

    // 1. Update ship positions
    for (auto& ship : ships_) {
        if (!ship.alive) continue;
        ship.x += ship.dx;
        ship.y += ship.dy;
    }

    // 2. Apply boundary rules
    apply_boundary_rules();

    // 3. Spawn bullets from ships that want to shoot
    spawn_bullets_from_ships(events);

    // 4. Update bullets
    update_bullets();

    // 5. Resolve bullet-ship collisions
    resolve_bullet_ship_collisions(events);

    // 6. Resolve bullet-base collisions
    resolve_bullet_base_collisions(events);

    // 7. Resolve bullet-tower collisions
    resolve_bullet_tower_collisions(events);

    // 8. Resolve ship-tower collisions
    resolve_ship_tower_collisions(events);

    // 9. Resolve ship-token collisions
    resolve_ship_token_collisions(events);

    // 10. Decrement shoot cooldowns
    for (auto& ship : ships_) {
        if (ship.shoot_cooldown > 0) --ship.shoot_cooldown;
    }

    // 11. Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // 12. Increment tick count
    ++tick_count_;

    return events;
}

void ArenaWorld::set_ship_actions(std::size_t idx,
                                  bool up, bool down, bool left, bool right,
                                  bool shoot) {
    if (idx < ships_.size() && ships_[idx].alive) {
        ships_[idx].apply_arena_actions(up, down, left, right, shoot);
    }
}

void ArenaWorld::apply_boundary_rules() {
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

void ArenaWorld::spawn_bullets_from_ships(TickEvents& events) {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        auto& ship = ships_[i];
        if (!ship.alive) continue;
        if (ship.wants_shoot && ship.shoot_cooldown <= 0) {
            Bullet b;
            // Spawn bullet at ship's nose (facing direction)
            const float fx = std::sin(ship.rotation);
            const float fy = -std::cos(ship.rotation);
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
            events.bullets_fired.push_back({i});
        }
    }
}

void ArenaWorld::update_bullets() {
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

void ArenaWorld::resolve_bullet_ship_collisions(TickEvents& events) {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (!ships_[i].alive) continue;
            // Skip self-hits
            if (b.owner_index == static_cast<int>(i)) continue;
            // Skip friendly fire when disabled
            if (!config_.friendly_fire && b.owner_index >= 0) {
                const auto killer = static_cast<std::size_t>(b.owner_index);
                if (killer < team_assignments_.size() &&
                    team_assignments_[killer] == team_assignments_[i]) {
                    continue;
                }
            }
            // Check rotation-aware vertex-based collision plus center proximity
            bool hit = bullet_triangle_collision_rotated(b.x, b.y, ships_[i]);
            if (!hit) {
                const float dx = b.x - ships_[i].x;
                const float dy = b.y - ships_[i].y;
                hit = (dx * dx + dy * dy) < (Triangle::SIZE * Triangle::SIZE);
            }
            if (hit) {
                ships_[i].take_damage(config_.bullet_ship_damage);
                b.alive = false;
                // Track kill only when the ship actually dies
                if (!ships_[i].alive) {
                    const auto killer = static_cast<std::size_t>(b.owner_index);
                    const bool is_friendly = (killer < team_assignments_.size() &&
                                              team_assignments_[killer] == team_assignments_[i]);
                    if (killer < team_assignments_.size()) {
                        if (is_friendly) {
                            ally_kills_[killer]++;
                        } else {
                            enemy_kills_[killer]++;
                        }
                    }
                    events.ship_kills.push_back({i, killer, is_friendly});
                }
                break;
            }
        }
    }
}

void ArenaWorld::resolve_bullet_tower_collisions(TickEvents& events) {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t t = 0; t < towers_.size(); ++t) {
            if (!towers_[t].alive) continue;
            if (bullet_circle_collision(b.x, b.y, towers_[t].x, towers_[t].y, towers_[t].radius)) {
                towers_[t].alive = false;
                b.alive = false;
                events.towers_destroyed.push_back({t});
                break;
            }
        }
    }
}

void ArenaWorld::resolve_bullet_base_collisions(TickEvents& events) {
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (std::size_t bi = 0; bi < bases_.size(); ++bi) {
            if (!bases_[bi].alive()) continue;
            // Skip friendly bullets
            if (b.owner_index >= 0) {
                const int shooter_team = team_assignments_[static_cast<std::size_t>(b.owner_index)];
                if (shooter_team == bases_[bi].team_id) continue;
            }
            if (bullet_circle_collision(b.x, b.y, bases_[bi].x, bases_[bi].y, bases_[bi].radius)) {
                bases_[bi].take_damage(config_.base_bullet_damage);
                b.alive = false;
                events.base_hits.push_back({bi, static_cast<std::size_t>(b.owner_index), config_.base_bullet_damage});
                break;
            }
        }
    }
}

void ArenaWorld::resolve_ship_tower_collisions(TickEvents& events) {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;
        for (const auto& tower : towers_) {
            if (!tower.alive) continue;
            if (triangle_circle_collision_rotated(ships_[i], tower.x, tower.y, tower.radius)) {
                ships_[i].alive = false;
                events.ship_tower_deaths.push_back({i});
                break;
            }
        }
    }
}

void ArenaWorld::resolve_ship_token_collisions(TickEvents& events) {
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (!ships_[i].alive) continue;
        for (std::size_t t = 0; t < tokens_.size(); ++t) {
            if (!tokens_[t].alive) continue;
            const float dx = ships_[i].x - tokens_[t].x;
            const float dy = ships_[i].y - tokens_[t].y;
            const float dist_sq = dx * dx + dy * dy;
            const float hit_r = tokens_[t].radius + Triangle::SIZE;
            if (dist_sq < hit_r * hit_r) {
                tokens_[t].alive = false;
                events.tokens_collected.push_back({i, t});
            }
        }
    }
}

void ArenaWorld::add_bullet(const Bullet& b) {
    bullets_.push_back(b);
}

void ArenaWorld::reset(uint32_t seed) {
    rng_.seed(seed);
    bullets_.clear();
    towers_.clear();
    tokens_.clear();
    std::fill(enemy_kills_.begin(), enemy_kills_.end(), 0);
    std::fill(ally_kills_.begin(), ally_kills_.end(), 0);
    tick_count_ = 0;
    spawn_obstacles();
}

int ArenaWorld::team_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < team_assignments_.size()) {
        return team_assignments_[ship_idx];
    }
    return -1;
}

int ArenaWorld::squad_of(std::size_t ship_idx) const noexcept {
    if (ship_idx < squad_assignments_.size()) {
        return squad_assignments_[ship_idx];
    }
    return -1;
}

SquadStats ArenaWorld::compute_squad_stats(int team, int squad) const {
    SquadStats stats;
    float count = 0, alive_ct = 0;
    float sum_x = 0, sum_y = 0;

    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (team_assignments_[i] != team || squad_assignments_[i] != squad) continue;
        count += 1.0f;
        if (!ships_[i].alive) continue;
        alive_ct += 1.0f;
        sum_x += ships_[i].x;
        sum_y += ships_[i].y;
    }

    if (count == 0.0f) return stats;
    stats.alive_fraction = alive_ct / count;

    if (alive_ct > 0.0f) {
        stats.centroid_x = sum_x / alive_ct;
        stats.centroid_y = sum_y / alive_ct;

        const auto t = static_cast<std::size_t>(team);
        if (t < bases_.size()) {
            const float dx_home = stats.centroid_x - bases_[t].x;
            const float dy_home = stats.centroid_y - bases_[t].y;
            const float dist_home = std::sqrt(dx_home * dx_home + dy_home * dy_home);
            const float diag = config_.world_diagonal();
            stats.avg_dist_to_home = dist_home / diag;
            if (dist_home > 0.0f) {
                stats.centroid_dir_sin = dx_home / dist_home;
                stats.centroid_dir_cos = dy_home / dist_home;
            }

            float min_enemy_dist = diag;
            for (const auto& base : bases_) {
                if (base.team_id == team) continue;
                const float dx_e = stats.centroid_x - base.x;
                const float dy_e = stats.centroid_y - base.y;
                min_enemy_dist = std::min(min_enemy_dist,
                    std::sqrt(dx_e * dx_e + dy_e * dy_e));
            }
            stats.avg_dist_to_enemy_base = min_enemy_dist / diag;
        }
    }

    // Compute squad spacing: stddev of alive ship distances from centroid, normalized
    if (alive_ct > 0.0f) {
        const float diag = config_.world_diagonal();
        float sum_dist = 0.0f;
        float sum_dist_sq = 0.0f;
        for (std::size_t i = 0; i < ships_.size(); ++i) {
            if (team_assignments_[i] != team || squad_assignments_[i] != squad) continue;
            if (!ships_[i].alive) continue;
            const float dx = ships_[i].x - stats.centroid_x;
            const float dy = ships_[i].y - stats.centroid_y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            sum_dist += dist;
            sum_dist_sq += dist * dist;
        }
        const float mean_dist = sum_dist / alive_ct;
        const float variance = (sum_dist_sq / alive_ct) - (mean_dist * mean_dist);
        stats.squad_spacing = std::sqrt(std::max(0.0f, variance)) / diag;
    }

    return stats;
}

std::size_t ArenaWorld::alive_count() const noexcept {
    std::size_t count = 0;
    for (const auto& ship : ships_) {
        if (ship.alive) ++count;
    }
    return count;
}

std::size_t ArenaWorld::teams_alive() const {
    std::set<int> alive_teams;
    for (std::size_t i = 0; i < ships_.size(); ++i) {
        if (ships_[i].alive) {
            alive_teams.insert(team_assignments_[i]);
        }
    }
    return alive_teams.size();
}

} // namespace neuroflyer

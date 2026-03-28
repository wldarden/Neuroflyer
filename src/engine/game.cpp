#include <neuroflyer/game.h>
#include <neuroflyer/collision.h>

#include <algorithm>
#include <cmath>

namespace neuroflyer {

void Triangle::apply_actions(bool up, bool down, bool left, bool right, bool shoot) {
    dx = 0.0f;
    dy = 0.0f;
    if (up) dy -= speed;
    if (down) dy += speed;
    if (left) dx -= speed;
    if (right) dx += speed;
    wants_shoot = shoot;
}

void Triangle::apply_arena_actions(bool up, bool down, bool left, bool right,
                                    bool shoot) {
    dx = 0.0f;
    dy = 0.0f;

    if (left)  rotation -= rotation_speed;
    if (right) rotation += rotation_speed;

    // Thrust in facing direction (rotation=0 is up, CW positive)
    float fx = std::sin(rotation);   // X component of facing direction
    float fy = -std::cos(rotation);  // Y component (negative = up in screen coords)

    if (up)   { dx += fx * speed; dy += fy * speed; }
    if (down) { dx -= fx * speed; dy -= fy * speed; }

    wants_shoot = shoot;
}

void Triangle::update(float screen_w, float screen_h) {
    x += dx;
    y += dy;
    x = std::clamp(x, SIZE, screen_w - SIZE);
    y = std::clamp(y, SIZE * 2.0f, screen_h - SIZE * 3.0f);  // keep away from edges

    if (shoot_cooldown > 0) --shoot_cooldown;
}

bool check_collision(const Triangle& tri, const Tower& tower) {
    if (!tower.alive) return false;
    return triangle_circle_collision(tri, tower.x, tower.y, tower.radius);
}

bool check_bullet_hit(const Bullet& bullet, const Tower& tower) {
    if (!bullet.alive || !tower.alive) return false;
    return bullet_circle_collision(bullet.x, bullet.y, tower.x, tower.y, tower.radius);
}

void Bullet::update_directional() {
    float move_x = dir_x * SPEED;
    float move_y = dir_y * SPEED;
    x += move_x;
    y += move_y;
    distance_traveled += std::sqrt(move_x * move_x + move_y * move_y);
    if (distance_traveled >= max_range) {
        alive = false;
    }
}

// --- GameSession ---

GameSession::GameSession(uint32_t level_seed, float screen_w, float screen_h,
                         const GameConfig& config)
    : config_(config),
      triangle_(screen_w / 2.0f, screen_h * 0.8f),
      tower_rng_(level_seed),
      token_rng_(level_seed + 12345),
      screen_w_(screen_w), screen_h_(screen_h) {

    triangle_.speed = config.ship_speed;
    triangle_.fire_cooldown = config.fire_cooldown;

    next_tower_y_ = 50.0f;
    next_token_y_ = 100.0f;
    maybe_spawn_towers();
    maybe_spawn_tokens();
}

void GameSession::maybe_spawn_towers() {
    // Spawn towers up to one screen height ahead of current scroll
    float spawn_horizon = scroll_offset_ + screen_h_ + 200.0f;

    // Density increases after 45 seconds (assuming ~60fps = 2700 ticks)
    // At SCROLL_SPEED=2, 45 sec = 5400 world-y units
    // After that, density increases every 5 seconds (= 600 world-y)
    constexpr float EASY_PHASE = 5400.0f;
    constexpr float RAMP_INTERVAL = 600.0f;
    constexpr float BASE_GAP_MIN = 80.0f;
    constexpr float BASE_GAP_MAX = 150.0f;
    constexpr float MIN_GAP = 15.0f;

    // Starting difficulty: 0=normal, 1=skip easy phase, N=skip easy + N-1 ramp steps
    // Applied as a virtual offset so the gap calculation thinks we're further into the level
    float difficulty_offset = 0.0f;
    if (config_.starting_difficulty >= 1) {
        difficulty_offset = EASY_PHASE +
            static_cast<float>(config_.starting_difficulty - 1) * RAMP_INTERVAL;
    }

    while (next_tower_y_ < spawn_horizon) {
        // Calculate current gap based on how far into the level we are
        float effective_y = next_tower_y_ + difficulty_offset;
        float gap_min = BASE_GAP_MIN;
        float gap_max = BASE_GAP_MAX;

        if (effective_y > EASY_PHASE) {
            float ramp_steps = (effective_y - EASY_PHASE) / RAMP_INTERVAL;
            float shrink = ramp_steps * 8.0f;  // gaps shrink by 8px per 5-second interval
            gap_min = std::max(MIN_GAP, BASE_GAP_MIN - shrink);
            gap_max = std::max(MIN_GAP + 10.0f, BASE_GAP_MAX - shrink * 1.5f);
        }

        std::uniform_real_distribution<float> gap_dist(gap_min, gap_max);
        std::uniform_real_distribution<float> x_dist(0.0f, screen_w_);
        std::uniform_real_distribution<float> radius_dist(15.0f, 35.0f);
        std::uniform_real_distribution<float> angle_dist(0.0f, 360.0f);
        std::uniform_real_distribution<float> speed_dist(0.3f, 3.0f);

        next_tower_y_ += gap_dist(tower_rng_);
        float tower_x = x_dist(tower_rng_);
        float tower_r = radius_dist(tower_rng_);
        float tower_angle = angle_dist(tower_rng_);
        float tower_spin = speed_dist(tower_rng_);
        // Randomly spin clockwise or counter-clockwise
        if (tower_rng_() & 1) tower_spin = -tower_spin;

        towers_.push_back({.x = tower_x, .y = next_tower_y_, .radius = tower_r, .alive = true,
                           .rotation_angle = tower_angle, .rotation_speed = tower_spin});
    }
}

void GameSession::set_actions(bool up, bool down, bool left, bool right, bool shoot) {
    triangle_.apply_actions(up, down, left, right, shoot);
}

void GameSession::tick() {
    if (!triangle_.alive) return;

    // Scroll
    scroll_offset_ += config_.scroll_speed;
    ++tick_count_;

    // Spin asteroids
    for (auto& tower : towers_) {
        tower.rotation_angle += tower.rotation_speed;
    }

    // Distance points scaled by screen position using configurable multipliers.
    // X: tent/triangle shape — edge_mult at x=0, center_mult at x=0.5, edge_mult at x=1
    // Y: piecewise linear — bottom_mult at ny=1, center_mult at ny=0.5, top_mult at ny=0
    {
        float nx = triangle_.x / screen_w_;  // 0..1
        float ny = triangle_.y / screen_h_;  // 0..1 (0 = top, 1 = bottom)

        // Horizontal: linear tent from edge to center to edge
        float h_mult = config_.x_edge_mult +
            (config_.x_center_mult - config_.x_edge_mult) *
            (1.0f - 2.0f * std::abs(nx - 0.5f));

        // Vertical: piecewise linear (3-point: top at ny=0, center at ny=0.5, bottom at ny=1)
        float v_mult;
        if (ny <= 0.5f) {
            // Top half: interpolate from top_mult (ny=0) to center_mult (ny=0.5)
            float t = ny * 2.0f;  // 0..1
            v_mult = config_.y_top_mult + (config_.y_center_mult - config_.y_top_mult) * t;
        } else {
            // Bottom half: interpolate from center_mult (ny=0.5) to bottom_mult (ny=1)
            float t = (ny - 0.5f) * 2.0f;  // 0..1
            v_mult = config_.y_center_mult + (config_.y_bottom_mult - config_.y_center_mult) * t;
        }

        // Average the two multipliers instead of multiplying them.
        // Multiplication causes negative × negative = positive (bug: corner
        // ships get rewarded when both axes have negative multipliers).
        float position_mult = (h_mult + v_mult) * 0.5f;
        distance_score_ += config_.scroll_speed * position_mult;
    }

    // Spawn new objects ahead
    maybe_spawn_towers();
    maybe_spawn_tokens();

    // Update triangle
    triangle_.update(screen_w_, screen_h_);

    // Fire bullet
    if (triangle_.wants_shoot && triangle_.shoot_cooldown <= 0) {
        bullets_.push_back({.x = triangle_.x, .y = triangle_.y - Triangle::SIZE});
        triangle_.shoot_cooldown = config_.fire_cooldown;
        ++bullets_fired_;
    }

    // Update bullets
    for (auto& b : bullets_) {
        if (b.alive) b.update();
        if (b.y < -20.0f) b.alive = false;  // past top of screen
    }

    // Collision detection — use screen coords for everything
    for (auto& tower : towers_) {
        if (!tower.alive) continue;

        float screen_y = screen_h_ - (tower.y - scroll_offset_);

        // Skip towers far off screen
        if (screen_y < -100.0f || screen_y > screen_h_ + 100.0f) continue;

        // Make a screen-space tower for collision checks
        Tower screen_tower{.x = tower.x, .y = screen_y, .radius = tower.radius, .alive = true};

        // Check bullet hits
        for (auto& bullet : bullets_) {
            if (check_bullet_hit(bullet, screen_tower)) {
                tower.alive = false;
                bullet.alive = false;
                ++towers_destroyed_;
                break;
            }
        }

        if (!tower.alive) continue;  // destroyed by bullet this tick

        // Check triangle collision
        if (check_collision(triangle_, screen_tower)) {
            triangle_.alive = false;
        }
    }

    // Token collection — triangle touches token to collect
    for (auto& token : tokens_) {
        if (!token.alive) continue;
        float screen_y = screen_h_ - (token.y - scroll_offset_);
        if (screen_y < -100.0f || screen_y > screen_h_ + 100.0f) continue;

        // Check if triangle center is within token radius (generous hitbox)
        float tdx = triangle_.x - token.x;
        float tdy = triangle_.y - screen_y;
        if ((tdx * tdx + tdy * tdy) < (token.radius + Triangle::SIZE) * (token.radius + Triangle::SIZE)) {
            token.alive = false;
            ++tokens_collected_;
        }
    }

    // Clean up dead bullets
    std::erase_if(bullets_, [](const Bullet& b) { return !b.alive; });

    // Clean up off-screen objects
    float cull_y = scroll_offset_ - screen_h_ - 100.0f;
    std::erase_if(towers_, [cull_y](const Tower& t) { return t.y < cull_y; });
    std::erase_if(tokens_, [cull_y](const Token& t) { return t.y < cull_y; });
}

void GameSession::maybe_spawn_tokens() {
    float spawn_horizon = scroll_offset_ + screen_h_ + 200.0f;

    // Tokens spawn less frequently than towers
    std::uniform_real_distribution<float> gap_dist(200.0f, 400.0f);
    std::uniform_real_distribution<float> x_dist(20.0f, screen_w_ - 20.0f);

    while (next_token_y_ < spawn_horizon) {
        next_token_y_ += gap_dist(token_rng_);
        float token_x = x_dist(token_rng_);
        tokens_.push_back({.x = token_x, .y = next_token_y_, .radius = 10.0f, .alive = true});
    }
}

std::vector<Token> GameSession::tokens_in_screen_coords() const {
    std::vector<Token> result;
    for (const auto& t : tokens_) {
        if (!t.alive) continue;
        float sy = screen_h_ - (t.y - scroll_offset_);
        if (sy < -100.0f || sy > screen_h_ + 100.0f) continue;
        result.push_back({.x = t.x, .y = sy, .radius = t.radius, .alive = true});
    }
    return result;
}

std::vector<Tower> GameSession::towers_in_screen_coords() const {
    std::vector<Tower> result;
    for (const auto& t : towers_) {
        if (!t.alive) continue;
        float sy = screen_h_ - (t.y - scroll_offset_);
        if (sy < -100.0f || sy > screen_h_ + 100.0f) continue;
        result.push_back({.x = t.x, .y = sy, .radius = t.radius, .alive = true,
                          .rotation_angle = t.rotation_angle, .rotation_speed = t.rotation_speed});
    }
    return result;
}

} // namespace neuroflyer

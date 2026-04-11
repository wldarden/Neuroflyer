#pragma once

#include <neuroflyer/config.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

struct Tower {
    float x, y;
    float radius = 20.0f;
    bool alive = true;
    float rotation_angle = 0.0f;   // current rotation in degrees
    float rotation_speed = 0.0f;   // degrees per tick
};

struct Token {
    float x, y;
    float radius = 10.0f;
    bool alive = true;
};

struct Bullet {
    float x, y;
    bool alive = true;
    static constexpr float SPEED = 8.0f;

    float dir_x = 0.0f;            // normalized direction X
    float dir_y = -1.0f;           // normalized direction Y (default: up)
    int owner_index = -1;           // which ship fired this
    float distance_traveled = 0.0f; // for max-range cutoff
    float max_range = 1000.0f;      // despawn after this distance

    void update() { y -= SPEED; }
    void update_directional();
};

struct Triangle {
    float x, y;
    bool alive = true;
    int shoot_cooldown = 0;
    float speed = 2.0f;        // from GameConfig
    int fire_cooldown = 30;    // from GameConfig
    static constexpr float SIZE = 12.0f;

    float hp = 3.0f;                // current health
    float max_hp = 3.0f;            // maximum health

    float rotation = 0.0f;          // radians, 0 = facing up, CW positive
    float rotation_speed = 0.05f;   // radians per tick

    Triangle(float x, float y) : x(x), y(y) {}

    void apply_actions(bool up, bool down, bool left, bool right, bool shoot);
    void apply_arena_actions(bool up, bool down, bool left, bool right, bool shoot);
    void update(float screen_w, float screen_h);

    void take_damage(float amount) {
        hp = std::max(0.0f, hp - amount);
        if (hp <= 0.0f) alive = false;
    }

    /// Number of hits taken: 0 = pristine, 1 = cracked, 2 = on fire
    [[nodiscard]] int damage_level() const noexcept {
        const float lost = max_hp - hp;
        if (lost >= 2.0f) return 2;
        if (lost >= 1.0f) return 1;
        return 0;
    }

    // Set by apply_actions/apply_arena_actions, consumed by update
    float dx = 0.0f, dy = 0.0f;
    bool wants_shoot = false;
};

/// Check if a triangle collides with a tower.
[[nodiscard]] bool check_collision(const Triangle& tri, const Tower& tower);

/// Check if a bullet hits a tower.
[[nodiscard]] bool check_bullet_hit(const Bullet& bullet, const Tower& tower);

/// One individual's playthrough of a level.
class GameSession {
public:
    GameSession(uint32_t level_seed, float screen_w, float screen_h,
                const GameConfig& config = {});

    /// Advance one tick. Call after applying neural net outputs via set_actions().
    void tick();

    /// Set the neural net's action outputs for the next tick.
    void set_actions(bool up, bool down, bool left, bool right, bool shoot);

    [[nodiscard]] bool alive() const noexcept { return triangle_.alive; }
    [[nodiscard]] float score() const noexcept {
        return distance_score_ * config_.pts_per_distance
               + towers_destroyed_ * config_.pts_per_tower
               + tokens_collected_ * config_.pts_per_token
               + bullets_fired_ * config_.pts_per_bullet;
    }
    [[nodiscard]] float distance() const noexcept { return distance_score_; }
    [[nodiscard]] const Triangle& triangle() const noexcept { return triangle_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] float scroll_offset() const noexcept { return scroll_offset_; }

    /// Get all living towers in screen coordinates. Single source of truth for conversion.
    [[nodiscard]] std::vector<Tower> towers_in_screen_coords() const;
    /// Get all living tokens in screen coordinates.
    [[nodiscard]] std::vector<Token> tokens_in_screen_coords() const;

private:
    void maybe_spawn_towers();
    void maybe_spawn_tokens();

    GameConfig config_;
    Triangle triangle_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    std::mt19937 tower_rng_;
    std::mt19937 token_rng_;
    float scroll_offset_ = 0.0f;
    float distance_score_ = 0.0f;
    int towers_destroyed_ = 0;
    int tokens_collected_ = 0;
    int bullets_fired_ = 0;
    float screen_w_, screen_h_;
    float next_tower_y_ = 0.0f;
    float next_token_y_ = 0.0f;
    uint32_t tick_count_ = 0;
};

} // namespace neuroflyer

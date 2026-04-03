#pragma once

#include <neuroflyer/base.h>
#include <neuroflyer/collision.h>
#include <neuroflyer/game.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

enum class AttackRunPhase { Phase1 = 0, Phase2 = 1, Phase3 = 2, Done = 3 };

struct AttackRunConfig {
    float world_width = 4000.0f;
    float world_height = 4000.0f;
    std::size_t population_size = 200;
    std::size_t tower_count = 50;
    std::size_t token_count = 30;

    // Starbase placement
    float min_starbase_distance = 1000.0f;
    float starbase_hp = 1000.0f;
    float starbase_radius = 100.0f;

    // Ship physics
    float rotation_speed = 0.05f;    // radians/tick
    float bullet_max_range = 1000.0f;
    float base_bullet_damage = 10.0f;
    bool wrap_ns = true;
    bool wrap_ew = true;

    // Phase timing
    uint32_t phase_duration_ticks = 20 * 60;  // 20 seconds at 60fps

    // Scoring weights
    float travel_weight = 1.0f;
    float attack_hit_bonus = 500.0f;
    float token_bonus = 50.0f;
    float death_penalty = 200.0f;

    [[nodiscard]] float world_diagonal() const noexcept {
        return std::sqrt(world_width * world_width + world_height * world_height);
    }
};

class AttackRunSession {
public:
    AttackRunSession(const AttackRunConfig& config, uint32_t seed);

    void tick();
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);

    [[nodiscard]] bool is_over() const noexcept { return phase_ == AttackRunPhase::Done; }
    [[nodiscard]] uint32_t current_tick() const noexcept { return tick_count_; }
    [[nodiscard]] AttackRunPhase phase() const noexcept { return phase_; }
    [[nodiscard]] uint32_t phase_ticks_remaining() const noexcept;
    [[nodiscard]] int phase_number() const noexcept;
    [[nodiscard]] std::vector<float> get_scores() const { return scores_; }

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const Base& starbase() const noexcept { return starbase_; }
    [[nodiscard]] float squad_center_x() const noexcept { return squad_center_x_; }
    [[nodiscard]] float squad_center_y() const noexcept { return squad_center_y_; }
    [[nodiscard]] const AttackRunConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_obstacles();
    void spawn_starbase();

    void apply_boundary_rules();
    void spawn_bullets_from_ships();
    void update_bullets();
    void resolve_ship_tower_collisions();
    void resolve_ship_token_collisions();
    void resolve_bullet_starbase_collisions();
    void resolve_bullet_tower_collisions();
    void compute_phase_scores();
    void advance_phase();

    AttackRunConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    Base starbase_;
    float squad_center_x_;
    float squad_center_y_;

    AttackRunPhase phase_ = AttackRunPhase::Phase1;
    uint32_t phase_tick_ = 0;
    uint32_t tick_count_ = 0;

    std::vector<float> scores_;
    std::vector<int> tokens_collected_;

    std::mt19937 rng_;
};

} // namespace neuroflyer

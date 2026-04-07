#pragma once

#include <neuroflyer/arena_world.h>
#include <neuroflyer/base.h>
#include <neuroflyer/game.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

enum class DrillPhase { Expand = 0, Contract = 1, Attack = 2, Done = 3 };

struct FighterDrillConfig {
    ArenaWorldConfig world{};

    std::size_t population_size = 200;

    // Starbase placement
    float starbase_distance = 1500.0f;  // from squad center
    float starbase_hp = 1000.0f;
    float starbase_radius = 100.0f;
    float base_bullet_damage = 10.0f;

    // Phase timing
    uint32_t phase_duration_ticks = 20 * 60;  // 20 seconds at 60fps

    // Scoring weights
    float expand_weight = 1.0f;
    float contract_weight = 1.0f;
    float attack_travel_weight = 1.0f;
    float attack_hit_bonus = 500.0f;
    float token_bonus = 50.0f;
    float death_penalty = 200.0f;

    FighterDrillConfig() {
        world.world_width = 4000.0f;
        world.world_height = 4000.0f;
        world.num_teams = 1;
        world.num_squads = 1;
        world.fighters_per_squad = population_size;
        world.tower_count = 50;
        world.token_count = 30;
        world.rotation_speed = 0.05f;
        world.bullet_max_range = 1000.0f;
        world.wrap_ns = true;
        world.wrap_ew = true;
        world.friendly_fire = false;
    }

    [[nodiscard]] float world_diagonal() const noexcept {
        return world.world_diagonal();
    }
};

class FighterDrillSession {
public:
    FighterDrillSession(const FighterDrillConfig& config, uint32_t seed);

    void tick();
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);

    [[nodiscard]] bool is_over() const noexcept { return phase_ == DrillPhase::Done; }
    [[nodiscard]] uint32_t current_tick() const noexcept { return world_.current_tick(); }
    [[nodiscard]] DrillPhase phase() const noexcept { return phase_; }
    [[nodiscard]] uint32_t phase_ticks_remaining() const noexcept;
    [[nodiscard]] std::vector<float> get_scores() const { return scores_; }

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return world_.ships(); }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return world_.ships(); }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return world_.towers(); }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return world_.tokens(); }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return world_.bullets(); }
    [[nodiscard]] const Base& starbase() const noexcept { return world_.bases()[0]; }
    [[nodiscard]] float squad_center_x() const noexcept { return squad_center_x_; }
    [[nodiscard]] float squad_center_y() const noexcept { return squad_center_y_; }
    [[nodiscard]] const FighterDrillConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_starbase();
    void compute_phase_scores();

    FighterDrillConfig config_;
    ArenaWorld world_;
    float squad_center_x_;
    float squad_center_y_;

    DrillPhase phase_ = DrillPhase::Expand;
    uint32_t phase_tick_ = 0;

    std::vector<float> scores_;
    std::vector<int> tokens_collected_;

    std::mt19937 rng_;
};

} // namespace neuroflyer

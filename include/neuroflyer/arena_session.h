#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/game.h>

#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

class ArenaSession {
public:
    ArenaSession(const ArenaConfig& config, uint32_t seed);

    void tick();
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);
    void apply_boundary_rules();
    void resolve_bullet_ship_collisions();
    void add_bullet(const Bullet& b);

    [[nodiscard]] bool is_over() const noexcept;
    [[nodiscard]] uint32_t current_tick() const noexcept { return tick_count_; }
    [[nodiscard]] int team_of(std::size_t ship_idx) const noexcept;
    [[nodiscard]] std::vector<float> get_scores() const;
    [[nodiscard]] std::size_t alive_count() const noexcept;
    [[nodiscard]] std::size_t teams_alive() const;

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const ArenaConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_obstacles();
    void spawn_bullets_from_ships();
    void update_bullets();
    void resolve_bullet_tower_collisions();
    void resolve_ship_tower_collisions();
    void resolve_ship_token_collisions();
    void check_end_conditions();

    ArenaConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    std::vector<int> team_assignments_;
    std::vector<float> survival_ticks_;
    std::mt19937 rng_;
    uint32_t tick_count_ = 0;
    bool over_ = false;
};

} // namespace neuroflyer

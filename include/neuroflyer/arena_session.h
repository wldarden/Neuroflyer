#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/base.h>
#include <neuroflyer/game.h>

#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

struct SquadStats {
    float alive_fraction = 0.0f;
    float centroid_x = 0.0f, centroid_y = 0.0f;
    float avg_dist_to_home = 0.0f;
    float avg_dist_to_enemy_base = 0.0f;
    float centroid_dir_sin = 0.0f, centroid_dir_cos = 0.0f;  // relative to home base
};

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
    [[nodiscard]] int squad_of(std::size_t ship_idx) const noexcept;
    [[nodiscard]] SquadStats compute_squad_stats(int team, int squad) const;
    [[nodiscard]] std::vector<float> get_scores() const;
    [[nodiscard]] std::size_t alive_count() const noexcept;
    [[nodiscard]] std::size_t teams_alive() const;
    [[nodiscard]] const std::vector<int>& enemy_kills() const noexcept { return enemy_kills_; }
    [[nodiscard]] const std::vector<int>& ally_kills() const noexcept { return ally_kills_; }

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const std::vector<Base>& bases() const noexcept { return bases_; }
    [[nodiscard]] std::vector<Base>& bases() noexcept { return bases_; }
    [[nodiscard]] const std::vector<int>& tokens_collected() const noexcept { return tokens_collected_; }
    [[nodiscard]] const ArenaConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_bases();
    void spawn_obstacles();
    void spawn_bullets_from_ships();
    void update_bullets();
    void resolve_bullet_tower_collisions();
    void resolve_bullet_base_collisions();
    void resolve_ship_tower_collisions();
    void resolve_ship_token_collisions();
    void check_end_conditions();

    ArenaConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    std::vector<Base> bases_;
    std::vector<int> team_assignments_;
    std::vector<int> squad_assignments_;
    std::vector<float> survival_ticks_;
    std::vector<int> tokens_collected_;
    std::vector<int> enemy_kills_;
    std::vector<int> ally_kills_;
    std::mt19937 rng_;
    uint32_t tick_count_ = 0;
    bool over_ = false;
};

} // namespace neuroflyer

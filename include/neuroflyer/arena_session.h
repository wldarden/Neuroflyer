#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_world.h>
#include <neuroflyer/base.h>
#include <neuroflyer/game.h>

#include <cstdint>
#include <vector>

namespace neuroflyer {

class ArenaSession {
public:
    ArenaSession(const ArenaConfig& config, uint32_t seed);

    void tick();
    void process_tick_events(const TickEvents& events);
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);
    void apply_boundary_rules();
    void resolve_bullet_ship_collisions();
    void add_bullet(const Bullet& b);

    [[nodiscard]] bool is_over() const noexcept;
    [[nodiscard]] uint32_t current_tick() const noexcept { return world_.current_tick(); }
    [[nodiscard]] int team_of(std::size_t ship_idx) const noexcept { return world_.team_of(ship_idx); }
    [[nodiscard]] int squad_of(std::size_t ship_idx) const noexcept { return world_.squad_of(ship_idx); }
    [[nodiscard]] SquadStats compute_squad_stats(int team, int squad) const { return world_.compute_squad_stats(team, squad); }
    [[nodiscard]] std::vector<float> get_scores() const;
    [[nodiscard]] std::size_t alive_count() const noexcept { return world_.alive_count(); }
    [[nodiscard]] std::size_t teams_alive() const { return world_.teams_alive(); }
    [[nodiscard]] const std::vector<int>& enemy_kills() const noexcept { return world_.enemy_kills(); }
    [[nodiscard]] const std::vector<int>& ally_kills() const noexcept { return world_.ally_kills(); }

    [[nodiscard]] ArenaWorld& world() noexcept { return world_; }
    [[nodiscard]] const ArenaWorld& world() const noexcept { return world_; }

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return world_.ships(); }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return world_.ships(); }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return world_.towers(); }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return world_.tokens(); }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return world_.bullets(); }
    [[nodiscard]] const std::vector<Base>& bases() const noexcept { return world_.bases(); }
    [[nodiscard]] std::vector<Base>& bases() noexcept { return world_.bases(); }
    [[nodiscard]] const std::vector<int>& tokens_collected() const noexcept { return tokens_collected_; }
    [[nodiscard]] const ArenaConfig& config() const noexcept { return config_; }

private:
    void check_end_conditions();

    ArenaConfig config_;
    ArenaWorld world_;
    std::vector<float> survival_ticks_;
    std::vector<int> tokens_collected_;
    bool over_ = false;
};

} // namespace neuroflyer

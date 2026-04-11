#pragma once

#include <neuroflyer/base.h>
#include <neuroflyer/game.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

/// Configuration for ArenaWorld physics simulation.
/// Subset of ArenaConfig covering only world/physics parameters.
struct ArenaWorldConfig {
    float world_width = 1280.0f * 64.0f;
    float world_height = 800.0f * 64.0f;

    std::size_t num_teams = 2;
    std::size_t num_squads = 1;
    std::size_t fighters_per_squad = 8;

    std::size_t tower_count = 200;
    std::size_t token_count = 100;

    float bullet_max_range = 1000.0f;

    bool wrap_ns = true;
    bool wrap_ew = true;

    float rotation_speed = 0.05f;
    float ship_hp = 3.0f;
    float bullet_ship_damage = 1.0f;

    float base_hp = 1000.0f;
    float base_radius = 100.0f;
    float base_bullet_damage = 10.0f;

    bool friendly_fire = false;

    [[nodiscard]] std::size_t population_size() const noexcept {
        return num_teams * num_squads * fighters_per_squad;
    }

    [[nodiscard]] float world_diagonal() const noexcept {
        return std::sqrt(world_width * world_width + world_height * world_height);
    }
};

/// Per-tick event: a bullet was fired by a ship.
struct BulletFired {
    std::size_t ship_idx;
};

/// Per-tick event: a ship was killed.
struct ShipKill {
    std::size_t victim_idx;
    std::size_t killer_idx;
    bool friendly;  // true if killer and victim are on the same team
};

/// Per-tick event: a ship hit a tower and died.
struct ShipTowerDeath {
    std::size_t ship_idx;
};

/// Per-tick event: a token was collected by a ship.
struct TokenCollected {
    std::size_t ship_idx;
    std::size_t token_idx;
};

/// Per-tick event: a bullet hit a base.
struct BaseHit {
    std::size_t base_idx;
    std::size_t shooter_idx;
    float damage;
};

/// Per-tick event: a bullet destroyed a tower.
struct TowerDestroyed {
    std::size_t tower_idx;
};

/// Aggregated events from a single tick.
struct TickEvents {
    std::vector<BulletFired> bullets_fired;
    std::vector<ShipKill> ship_kills;
    std::vector<ShipTowerDeath> ship_tower_deaths;
    std::vector<TokenCollected> tokens_collected;
    std::vector<BaseHit> base_hits;
    std::vector<TowerDestroyed> towers_destroyed;
};

/// Squad-level statistics computed from ship positions.
struct SquadStats {
    float alive_fraction = 0.0f;
    float centroid_x = 0.0f, centroid_y = 0.0f;
    float avg_dist_to_home = 0.0f;
    float avg_dist_to_enemy_base = 0.0f;
    float centroid_dir_sin = 0.0f, centroid_dir_cos = 0.0f;
    float squad_spacing = 0.0f;
};

/// Pure physics simulation for arena-style gameplay.
///
/// ArenaWorld owns ships, bullets, towers, tokens, and bases. It handles
/// movement, boundary wrapping/clamping, bullet lifecycle, and all collision
/// types. Game modes compose ArenaWorld and interpret its TickEvents for
/// scoring.
class ArenaWorld {
public:
    ArenaWorld(const ArenaWorldConfig& config, uint32_t seed);

    /// Advance one tick. Returns events describing what happened.
    [[nodiscard]] TickEvents tick();

    /// Set a ship's actions for the next tick.
    void set_ship_actions(std::size_t idx,
                          bool up, bool down, bool left, bool right, bool shoot);

    /// Apply boundary wrapping/clamping to all ships.
    void apply_boundary_rules();

    /// Resolve bullet-ship collisions. Populates events.
    void resolve_bullet_ship_collisions(TickEvents& events);

    /// Manually inject a bullet (for game modes like AttackRunSession).
    void add_bullet(const Bullet& b);

    /// Reset world state for a new round. Clears bullets, towers, tokens,
    /// tracking arrays, and tick counter. Does NOT touch ships or bases
    /// (game modes reposition those).
    void reset(uint32_t seed);

    // --- Queries ---

    [[nodiscard]] uint32_t current_tick() const noexcept { return tick_count_; }
    [[nodiscard]] int team_of(std::size_t ship_idx) const noexcept;
    [[nodiscard]] int squad_of(std::size_t ship_idx) const noexcept;
    [[nodiscard]] SquadStats compute_squad_stats(int team, int squad) const;
    [[nodiscard]] std::size_t alive_count() const noexcept;
    [[nodiscard]] std::size_t teams_alive() const;

    // Backward-compat kill tracking (also reported in TickEvents)
    [[nodiscard]] const std::vector<int>& enemy_kills() const noexcept { return enemy_kills_; }
    [[nodiscard]] const std::vector<int>& ally_kills() const noexcept { return ally_kills_; }

    // --- Entity access ---

    [[nodiscard]] std::vector<Triangle>& ships() noexcept { return ships_; }
    [[nodiscard]] const std::vector<Triangle>& ships() const noexcept { return ships_; }
    [[nodiscard]] const std::vector<Tower>& towers() const noexcept { return towers_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Bullet>& bullets() const noexcept { return bullets_; }
    [[nodiscard]] const std::vector<Base>& bases() const noexcept { return bases_; }
    [[nodiscard]] std::vector<Base>& bases() noexcept { return bases_; }
    [[nodiscard]] const ArenaWorldConfig& config() const noexcept { return config_; }

private:
    void spawn_ships();
    void spawn_bases();
    void spawn_obstacles();
    void spawn_bullets_from_ships(TickEvents& events);
    void update_bullets();
    void resolve_bullet_tower_collisions(TickEvents& events);
    void resolve_bullet_base_collisions(TickEvents& events);
    void resolve_ship_tower_collisions(TickEvents& events);
    void resolve_ship_token_collisions(TickEvents& events);

    ArenaWorldConfig config_;
    std::vector<Triangle> ships_;
    std::vector<Tower> towers_;
    std::vector<Token> tokens_;
    std::vector<Bullet> bullets_;
    std::vector<Base> bases_;
    std::vector<int> team_assignments_;
    std::vector<int> squad_assignments_;
    std::vector<int> enemy_kills_;
    std::vector<int> ally_kills_;
    std::mt19937 rng_;
    uint32_t tick_count_ = 0;
};

} // namespace neuroflyer

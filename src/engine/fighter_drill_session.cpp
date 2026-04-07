#include <neuroflyer/fighter_drill_session.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace neuroflyer {

static ArenaWorldConfig build_world_config(const FighterDrillConfig& config) {
    ArenaWorldConfig wc = config.world;
    wc.num_teams = 1;
    wc.num_squads = 1;
    wc.fighters_per_squad = config.population_size;
    wc.base_hp = config.starbase_hp;
    wc.base_radius = config.starbase_radius;
    wc.base_bullet_damage = config.base_bullet_damage;
    return wc;
}

FighterDrillSession::FighterDrillSession(const FighterDrillConfig& config, uint32_t seed)
    : config_(config)
    , world_(build_world_config(config), seed)
    , squad_center_x_(config.world.world_width / 2.0f)
    , squad_center_y_(config.world.world_height / 2.0f)
    , rng_(seed)
{
    scores_.resize(config_.population_size, 0.0f);
    tokens_collected_.resize(config_.population_size, 0);

    // ArenaWorld spawns ships in team-angular placement, but drill wants all
    // ships at squad center with random rotations.
    spawn_ships();

    // ArenaWorld spawns a default base per team, but drill places the starbase
    // at a custom position.
    spawn_starbase();
}

void FighterDrillSession::spawn_ships() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);

    for (auto& ship : world_.ships()) {
        ship.x = squad_center_x_;
        ship.y = squad_center_y_;
        ship.rotation = angle_dist(rng_);
        ship.rotation_speed = config_.world.rotation_speed;
    }
}

void FighterDrillSession::spawn_starbase() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);
    float angle = angle_dist(rng_);
    world_.bases()[0] = Base(
        squad_center_x_ + config_.starbase_distance * std::cos(angle),
        squad_center_y_ + config_.starbase_distance * std::sin(angle),
        config_.starbase_radius,
        config_.starbase_hp,
        1);  // team 1 = enemy (different from team 0 so bullets can hit it)
}

uint32_t FighterDrillSession::phase_ticks_remaining() const noexcept {
    if (phase_ == DrillPhase::Done) return 0;
    return config_.phase_duration_ticks - phase_tick_;
}

void FighterDrillSession::set_ship_actions(
    std::size_t idx, bool up, bool down, bool left, bool right, bool shoot) {
    world_.set_ship_actions(idx, up, down, left, right, shoot);
}

void FighterDrillSession::tick() {
    if (phase_ == DrillPhase::Done) return;

    auto events = world_.tick();

    // Phase-based movement scoring
    compute_phase_scores();

    // Process events for scoring
    for (const auto& tc : events.tokens_collected) {
        scores_[tc.ship_idx] += config_.token_bonus;
        tokens_collected_[tc.ship_idx]++;
    }
    for (const auto& death : events.ship_tower_deaths) {
        scores_[death.ship_idx] -= config_.death_penalty;
    }
    for (const auto& hit : events.base_hits) {
        scores_[hit.shooter_idx] += config_.attack_hit_bonus;
    }

    // Advance tick counters
    ++phase_tick_;

    // Phase transitions
    if (phase_tick_ >= config_.phase_duration_ticks) {
        phase_tick_ = 0;
        switch (phase_) {
            case DrillPhase::Expand:   phase_ = DrillPhase::Contract; break;
            case DrillPhase::Contract: phase_ = DrillPhase::Attack;   break;
            case DrillPhase::Attack:   phase_ = DrillPhase::Done;     break;
            case DrillPhase::Done:     break;
        }
    }
}

void FighterDrillSession::compute_phase_scores() {
    if (phase_ == DrillPhase::Done) return;

    const auto& ships = world_.ships();
    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        float vx = ships[i].dx;
        float vy = ships[i].dy;

        switch (phase_) {
            case DrillPhase::Expand: {
                float dir_x = ships[i].x - squad_center_x_;
                float dir_y = ships[i].y - squad_center_y_;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.expand_weight;
                }
                break;
            }
            case DrillPhase::Contract: {
                float dir_x = squad_center_x_ - ships[i].x;
                float dir_y = squad_center_y_ - ships[i].y;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.contract_weight;
                }
                break;
            }
            case DrillPhase::Attack: {
                const auto& base = world_.bases()[0];
                float dir_x = base.x - ships[i].x;
                float dir_y = base.y - ships[i].y;
                float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                if (len > 0.001f) {
                    dir_x /= len;
                    dir_y /= len;
                    scores_[i] += (vx * dir_x + vy * dir_y) * config_.attack_travel_weight;
                }
                break;
            }
            case DrillPhase::Done:
                break;
        }
    }
}

} // namespace neuroflyer

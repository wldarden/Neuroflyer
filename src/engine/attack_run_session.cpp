#include <neuroflyer/attack_run_session.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace neuroflyer {

static ArenaWorldConfig build_world_config(const AttackRunConfig& config) {
    ArenaWorldConfig wc = config.world;
    wc.num_teams = 1;
    wc.num_squads = 1;
    wc.fighters_per_squad = config.population_size;
    wc.base_hp = config.starbase_hp;
    wc.base_radius = config.starbase_radius;
    wc.base_bullet_damage = config.base_bullet_damage;
    return wc;
}

AttackRunSession::AttackRunSession(const AttackRunConfig& config, uint32_t seed)
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

    // ArenaWorld spawns a default base per team, but attack runs places the
    // starbase at a custom position.
    spawn_starbase();
}

void AttackRunSession::spawn_ships() {
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);

    for (auto& ship : world_.ships()) {
        ship.x = squad_center_x_;
        ship.y = squad_center_y_;
        ship.rotation = angle_dist(rng_);
        ship.rotation_speed = config_.world.rotation_speed;
    }
}

void AttackRunSession::spawn_starbase() {
    const float max_distance = std::min(config_.world.world_width, config_.world.world_height) / 2.0f;
    std::uniform_real_distribution<float> dist_dist(
        config_.min_starbase_distance, max_distance);
    std::uniform_real_distribution<float> angle_dist(
        0.0f, 2.0f * std::numbers::pi_v<float>);

    const float distance = dist_dist(rng_);
    const float angle = angle_dist(rng_);
    const float raw_x = squad_center_x_ + distance * std::cos(angle);
    const float raw_y = squad_center_y_ + distance * std::sin(angle);
    const float clamped_x = std::clamp(raw_x, config_.starbase_radius,
                                        config_.world.world_width - config_.starbase_radius);
    const float clamped_y = std::clamp(raw_y, config_.starbase_radius,
                                        config_.world.world_height - config_.starbase_radius);

    world_.bases()[0] = Base(clamped_x, clamped_y,
                             config_.starbase_radius, config_.starbase_hp, 1);  // team 1 = enemy
}

uint32_t AttackRunSession::phase_ticks_remaining() const noexcept {
    if (phase_ == AttackRunPhase::Done) return 0;
    return config_.phase_duration_ticks - phase_tick_;
}

int AttackRunSession::phase_number() const noexcept {
    switch (phase_) {
        case AttackRunPhase::Phase1: return 1;
        case AttackRunPhase::Phase2: return 2;
        case AttackRunPhase::Phase3: return 3;
        case AttackRunPhase::Done:   return 3;
    }
    return 0;
}

void AttackRunSession::set_ship_actions(
    std::size_t idx, bool up, bool down, bool left, bool right, bool shoot) {
    world_.set_ship_actions(idx, up, down, left, right, shoot);
}

void AttackRunSession::tick() {
    if (phase_ == AttackRunPhase::Done) return;

    auto events = world_.tick();
    process_tick_events(events);
}

void AttackRunSession::process_tick_events(const TickEvents& events) {
    if (phase_ == AttackRunPhase::Done) return;

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
    for (const auto& bf : events.bullets_fired) {
        scores_[bf.ship_idx] -= config_.bullet_cost;
    }

    // Advance tick counters
    ++phase_tick_;

    // Phase transitions: timer expired OR starbase destroyed
    if (phase_tick_ >= config_.phase_duration_ticks || !world_.bases()[0].alive()) {
        advance_phase();
    }
}

void AttackRunSession::advance_phase() {
    phase_tick_ = 0;
    switch (phase_) {
        case AttackRunPhase::Phase1:
            phase_ = AttackRunPhase::Phase2;
            spawn_starbase();
            break;
        case AttackRunPhase::Phase2:
            phase_ = AttackRunPhase::Phase3;
            spawn_starbase();
            break;
        case AttackRunPhase::Phase3:
            phase_ = AttackRunPhase::Done;
            break;
        case AttackRunPhase::Done:
            break;
    }
}

void AttackRunSession::compute_phase_scores() {
    if (phase_ == AttackRunPhase::Done) return;
    if (!world_.bases()[0].alive()) return;

    const auto& ships = world_.ships();
    const auto& base = world_.bases()[0];
    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!ships[i].alive) continue;

        const float vx = ships[i].dx;
        const float vy = ships[i].dy;

        // All phases score movement toward the starbase
        float dir_x = base.x - ships[i].x;
        float dir_y = base.y - ships[i].y;
        const float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        if (len > 0.001f) {
            dir_x /= len;
            dir_y /= len;
            scores_[i] += (vx * dir_x + vy * dir_y) * config_.travel_weight;
        }
    }
}

} // namespace neuroflyer

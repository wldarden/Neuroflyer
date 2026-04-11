#include <neuroflyer/arena_session.h>

namespace neuroflyer {

ArenaSession::ArenaSession(const ArenaConfig& config, uint32_t seed)
    : config_(config), world_(config.world, seed) {
    const std::size_t pop = config.world.population_size();
    survival_ticks_.resize(pop, 0.0f);
    tokens_collected_.resize(pop, 0);
}

void ArenaSession::tick() {
    if (over_) return;

    auto events = world_.tick();
    process_tick_events(events);
}

void ArenaSession::process_tick_events(const TickEvents& events) {
    if (over_) return;

    // Track survival ticks for alive ships
    for (std::size_t i = 0; i < world_.ships().size(); ++i) {
        if (world_.ships()[i].alive) {
            survival_ticks_[i] += 1.0f;
        }
    }

    // Track token collection from events
    for (const auto& tc : events.tokens_collected) {
        if (tc.ship_idx < tokens_collected_.size()) {
            tokens_collected_[tc.ship_idx]++;
        }
    }

    check_end_conditions();
}

void ArenaSession::set_ship_actions(std::size_t idx,
                                     bool up, bool down, bool left, bool right,
                                     bool shoot) {
    world_.set_ship_actions(idx, up, down, left, right, shoot);
}

void ArenaSession::apply_boundary_rules() {
    world_.apply_boundary_rules();
}

void ArenaSession::resolve_bullet_ship_collisions() {
    TickEvents events;
    world_.resolve_bullet_ship_collisions(events);
}

void ArenaSession::add_bullet(const Bullet& b) {
    world_.add_bullet(b);
}

bool ArenaSession::is_over() const noexcept {
    return over_;
}

std::vector<float> ArenaSession::get_scores() const {
    std::vector<float> scores(config_.world.num_teams, 0.0f);
    const auto& ek = world_.enemy_kills();
    const auto& ak = world_.ally_kills();
    for (std::size_t i = 0; i < world_.ships().size(); ++i) {
        int team = world_.team_of(i);
        auto t = static_cast<std::size_t>(team);
        // 1 point per second at 60fps: survival_ticks / 60
        scores[t] += survival_ticks_[i] / 60.0f;
        // +1000 per enemy kill, -1000 per ally kill
        scores[t] += static_cast<float>(ek[i]) * 1000.0f;
        scores[t] -= static_cast<float>(ak[i]) * 1000.0f;
    }
    return scores;
}

void ArenaSession::check_end_conditions() {
    if (world_.current_tick() >= config_.time_limit_ticks) {
        over_ = true;
        return;
    }

    // Count teams with alive bases
    std::size_t bases_alive = 0;
    for (const auto& base : world_.bases()) {
        if (base.alive()) ++bases_alive;
    }
    if (bases_alive <= 1) {
        over_ = true;
        return;
    }

    // Also end if only 1 or 0 teams have alive ships
    if (world_.teams_alive() <= 1) {
        over_ = true;
    }
}

} // namespace neuroflyer

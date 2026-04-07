#include <neuroflyer/skirmish_tournament.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sensor_engine.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>

namespace neuroflyer {

// ── Shared helpers ─────────────────────────────────────────────────────────

namespace {

void tick_arena_match(
    ArenaSession& arena,
    const ArenaConfig& arena_config,
    const ShipDesign& fighter_design,
    std::vector<neuralnet::Network>& ntm_nets,
    std::vector<neuralnet::Network>& leader_nets,
    std::vector<neuralnet::Network>& fighter_nets,
    std::vector<std::vector<float>>& recurrent_states,
    const std::vector<int>& ship_teams,
    std::vector<SquadLeaderFighterInputs>* out_sl_inputs,
    std::vector<std::vector<float>>* out_leader_inputs = nullptr,
    std::vector<std::vector<float>>* out_fighter_inputs = nullptr) {

    const std::size_t total_ships = arena.ships().size();

    SectorGrid grid(arena_config.world.world_width, arena_config.world.world_height,
                    arena_config.sector_size);
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena.ships()[i].alive) {
            grid.insert(i, arena.ships()[i].x, arena.ships()[i].y);
        }
    }
    for (std::size_t b = 0; b < arena.bases().size(); ++b) {
        if (arena.bases()[b].alive()) {
            grid.insert(total_ships + b, arena.bases()[b].x, arena.bases()[b].y);
        }
    }

    constexpr std::size_t kNumTeams = 2;
    std::vector<SquadLeaderOrder> team_orders(kNumTeams);
    std::vector<float> squad_center_xs(kNumTeams, 0.0f);
    std::vector<float> squad_center_ys(kNumTeams, 0.0f);

    for (std::size_t t = 0; t < kNumTeams; ++t) {
        const int team = static_cast<int>(t);
        auto stats = arena.compute_squad_stats(team, 0);
        squad_center_xs[t] = stats.centroid_x;
        squad_center_ys[t] = stats.centroid_y;

        auto threats = gather_near_threats(
            grid, stats.centroid_x, stats.centroid_y,
            arena_config.ntm_sector_radius, team,
            arena.ships(), ship_teams, arena.bases());

        auto ntm = run_ntm_threat_selection(
            ntm_nets[t], stats.centroid_x, stats.centroid_y,
            stats.alive_fraction, threats,
            arena_config.world.world_width, arena_config.world.world_height);

        const float own_base_x = arena.bases()[t].x;
        const float own_base_y = arena.bases()[t].y;
        const float own_base_hp = arena.bases()[t].hp_normalized();
        float enemy_base_x = 0, enemy_base_y = 0;
        float min_dist_sq = std::numeric_limits<float>::max();
        for (const auto& base : arena.bases()) {
            if (base.team_id == team) continue;
            const float dx = stats.centroid_x - base.x;
            const float dy = stats.centroid_y - base.y;
            const float dsq = dx * dx + dy * dy;
            if (dsq < min_dist_sq) {
                min_dist_sq = dsq;
                enemy_base_x = base.x;
                enemy_base_y = base.y;
            }
        }

        const float world_diag = std::sqrt(
            arena_config.world.world_width * arena_config.world.world_width +
            arena_config.world.world_height * arena_config.world.world_height);
        const float home_dx = own_base_x - stats.centroid_x;
        const float home_dy = own_base_y - stats.centroid_y;
        const float home_dist_raw = std::sqrt(home_dx * home_dx + home_dy * home_dy);
        const float home_distance = home_dist_raw / world_diag;
        const float home_heading_sin = (home_dist_raw > 1e-6f) ? home_dx / home_dist_raw : 0.0f;
        const float home_heading_cos = (home_dist_raw > 1e-6f) ? home_dy / home_dist_raw : 0.0f;
        const float cmd_dx = enemy_base_x - stats.centroid_x;
        const float cmd_dy = enemy_base_y - stats.centroid_y;
        const float cmd_dist_raw = std::sqrt(cmd_dx * cmd_dx + cmd_dy * cmd_dy);
        const float cmd_heading_sin = (cmd_dist_raw > 1e-6f) ? cmd_dx / cmd_dist_raw : 0.0f;
        const float cmd_heading_cos = (cmd_dist_raw > 1e-6f) ? cmd_dy / cmd_dist_raw : 0.0f;
        const float cmd_target_distance = cmd_dist_raw / world_diag;

        float enemy_alive_frac = 0.0f;
        std::size_t enemy_total = 0, enemy_alive = 0;
        for (std::size_t si = 0; si < total_ships; ++si) {
            if (ship_teams[si] != team) {
                ++enemy_total;
                if (arena.ships()[si].alive) ++enemy_alive;
            }
        }
        if (enemy_total > 0) enemy_alive_frac = static_cast<float>(enemy_alive) / static_cast<float>(enemy_total);

        float time_remaining = 1.0f - static_cast<float>(arena.current_tick()) /
            static_cast<float>(std::max(arena_config.time_limit_ticks, 1u));

        team_orders[t] = run_squad_leader(
            leader_nets[t], stats.alive_fraction,
            home_heading_sin, home_heading_cos, home_distance,
            own_base_hp,
            cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
            ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y,
            enemy_alive_frac, time_remaining,
            stats.centroid_x / arena_config.world.world_width,
            stats.centroid_y / arena_config.world.world_height);

        // Store squad leader net inputs for visualization
        if (out_leader_inputs && t < out_leader_inputs->size()) {
            (*out_leader_inputs)[t] = {
                stats.alive_fraction,
                home_heading_sin, home_heading_cos, home_distance,
                own_base_hp,
                cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
                ntm.active ? 1.0f : 0.0f,
                ntm.heading_sin, ntm.heading_cos,
                ntm.distance, ntm.threat_score,
                enemy_alive_frac, time_remaining,
                stats.centroid_x / arena_config.world.world_width,
                stats.centroid_y / arena_config.world.world_height
            };
        }
    }

    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena.ships()[i].alive) continue;

        const int team = ship_teams[i];
        const auto t = static_cast<std::size_t>(team);

        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena.ships()[i].x, arena.ships()[i].y,
            arena.ships()[i].rotation,
            team_orders[t],
            squad_center_xs[t], squad_center_ys[t],
            arena_config.world.world_width, arena_config.world.world_height);

        if (out_sl_inputs) {
            (*out_sl_inputs)[i] = sl_inputs;
        }

        auto ctx = ArenaQueryContext::for_ship(
            arena.ships()[i], i, team,
            arena_config.world.world_width, arena_config.world.world_height,
            arena.towers(), arena.tokens(),
            arena.ships(), ship_teams, arena.bullets());

        auto input = build_arena_ship_input(
            fighter_design, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states[i]);

        // Store fighter input for visualization
        if (out_fighter_inputs && i < out_fighter_inputs->size()) {
            (*out_fighter_inputs)[i] = input;
        }

        auto output = fighter_nets[t].forward(
            std::span<const float>(input));

        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design.memory_slots);

        arena.set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        recurrent_states[i] = decoded.memory;
    }

    arena.tick();
}

void score_match(
    const ArenaSession& arena,
    const SkirmishConfig& config,
    const std::vector<int>& ship_teams,
    const std::size_t variant_indices[2],
    std::vector<float>& scores) {

    const std::size_t total_ships = arena.ships().size();
    const auto& kills = arena.enemy_kills();

    for (std::size_t t = 0; t < 2; ++t) {
        const int team = static_cast<int>(t);
        float score = 0.0f;

        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team) {
                score += config.kill_points * static_cast<float>(kills[i]);
            }
        }

        for (const auto& base : arena.bases()) {
            float damage_dealt = base.max_hp - base.hp;
            if (base.team_id == team) {
                // Penalty for damage taken on own base (symmetric with base_hit_points)
                if (damage_dealt > 0.0f && config.base_bullet_damage > 0.0f) {
                    float hits = damage_dealt / config.base_bullet_damage;
                    score -= config.base_hit_points * hits;
                }
                if (!base.alive()) {
                    score -= config.base_kill_points();
                }
            } else {
                // Reward for damage dealt to enemy base
                if (damage_dealt > 0.0f && config.base_bullet_damage > 0.0f) {
                    float hits = damage_dealt / config.base_bullet_damage;
                    score += config.base_hit_points * hits;
                }
                if (!base.alive()) {
                    score += config.base_kill_points();
                }
            }
        }

        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams[i] == team && !arena.ships()[i].alive) {
                score -= config.death_points;
            }
        }

        scores[variant_indices[t]] += score;
    }
}

} // anonymous namespace

// ── Constructor ─────────────────────────────────────────────────────────────

SkirmishTournament::SkirmishTournament(
    const SkirmishConfig& config,
    const ShipDesign& fighter_design,
    const std::vector<TeamIndividual>& variants,
    uint32_t seed)
    : config_(config)
    , fighter_design_(fighter_design)
    , variants_(variants)
    , scores_(variants.size(), 0.0f)
    , rng_(seed) {

    // Initialize active list with all variant indices
    active_.reserve(variants.size());
    for (std::size_t i = 0; i < variants.size(); ++i) {
        active_.push_back(i);
    }

    if (active_.size() <= 1) {
        complete_ = true;
        return;
    }

    build_rounds();
}

// ── Accessors ───────────────────────────────────────────────────────────────

std::size_t SkirmishTournament::current_round() const noexcept { return round_idx_; }
std::size_t SkirmishTournament::current_match() const noexcept { return match_idx_; }
std::size_t SkirmishTournament::current_seed() const noexcept { return seed_idx_; }
std::size_t SkirmishTournament::total_rounds() const noexcept { return rounds_.size(); }

std::size_t SkirmishTournament::matches_in_round() const noexcept {
    if (round_idx_ < rounds_.size()) {
        return rounds_[round_idx_].matchups.size();
    }
    return 0;
}

std::size_t SkirmishTournament::seeds_for_current_round() const noexcept {
    // Finals (last round when active_.size() <= 3) get double seeds
    if (active_.size() <= 3) {
        return config_.seeds_per_match * 2;
    }
    return config_.seeds_per_match;
}

bool SkirmishTournament::is_complete() const noexcept { return complete_; }

const ArenaSession* SkirmishTournament::current_arena() const noexcept {
    return arena_.get();
}

const std::vector<float>& SkirmishTournament::variant_scores() const noexcept {
    return scores_;
}

std::pair<std::size_t, std::size_t> SkirmishTournament::current_matchup() const noexcept {
    if (round_idx_ < rounds_.size() && match_idx_ < rounds_[round_idx_].matchups.size()) {
        return rounds_[round_idx_].matchups[match_idx_];
    }
    return {SIZE_MAX, SIZE_MAX};
}

const std::vector<int>& SkirmishTournament::current_ship_teams() const noexcept {
    return ship_teams_;
}

neuralnet::Network* SkirmishTournament::fighter_net(std::size_t team) noexcept {
    return (team < fighter_nets_.size()) ? &fighter_nets_[team] : nullptr;
}

neuralnet::Network* SkirmishTournament::leader_net(std::size_t team) noexcept {
    return (team < leader_nets_.size()) ? &leader_nets_[team] : nullptr;
}

const std::vector<SquadLeaderFighterInputs>& SkirmishTournament::last_squad_inputs() const noexcept {
    return last_sl_inputs_;
}

static const std::vector<float> empty_floats;

const std::vector<float>& SkirmishTournament::last_leader_input(std::size_t team) const noexcept {
    if (team < last_leader_inputs_.size()) return last_leader_inputs_[team];
    return empty_floats;
}

const std::vector<float>& SkirmishTournament::last_fighter_input(std::size_t ship) const noexcept {
    if (ship < last_fighter_inputs_.size()) return last_fighter_inputs_[ship];
    return empty_floats;
}

// ── Round Building ──────────────────────────────────────────────────────────

void SkirmishTournament::build_rounds() {
    if (active_.empty()) {
        complete_ = true;
        return;
    }

    if (active_.size() == 1) {
        complete_ = true;
        return;
    }

    if (active_.size() <= 3) {
        // Final round: all pair combinations
        TournamentRound round;
        if (active_.size() == 2) {
            round.matchups.emplace_back(active_[0], active_[1]);
        } else {
            // 3 variants: three matchups (each pair)
            round.matchups.emplace_back(active_[0], active_[1]);
            round.matchups.emplace_back(active_[0], active_[2]);
            round.matchups.emplace_back(active_[1], active_[2]);
        }
        rounds_.push_back(std::move(round));
        return;
    }

    // Regular round: pair up, last gets bye if odd
    TournamentRound round;
    std::size_t count = active_.size();
    if (count % 2 != 0) {
        round.bye_index = active_.back();
        count -= 1;
    }
    for (std::size_t i = 0; i < count; i += 2) {
        round.matchups.emplace_back(active_[i], active_[i + 1]);
    }
    rounds_.push_back(std::move(round));
}

// ── Background Match Lifecycle ─────────────────────────────────────────────

void SkirmishTournament::start_bg_match() {
    assert(!bg_match_active_);
    auto [idx_a, idx_b] = rounds_[round_idx_].matchups[bg_match_cursor_];

    bg_arena_config_ = config_.to_arena_config();
    bg_arena_config_.world.num_teams = 2;

    bg_arena_ = std::make_unique<ArenaSession>(bg_arena_config_,
                                                static_cast<uint32_t>(rng_()));

    bg_ntm_nets_.clear();
    bg_leader_nets_.clear();
    bg_fighter_nets_.clear();

    bg_ntm_nets_.push_back(variants_[idx_a].build_ntm_network());
    bg_ntm_nets_.push_back(variants_[idx_b].build_ntm_network());
    bg_leader_nets_.push_back(variants_[idx_a].build_squad_network());
    bg_leader_nets_.push_back(variants_[idx_b].build_squad_network());
    bg_fighter_nets_.push_back(variants_[idx_a].build_fighter_network());
    bg_fighter_nets_.push_back(variants_[idx_b].build_fighter_network());

    const std::size_t total = bg_arena_->ships().size();
    bg_recurrent_states_.assign(total,
        std::vector<float>(fighter_design_.memory_slots, 0.0f));
    bg_ship_teams_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        bg_ship_teams_[i] = bg_arena_->team_of(i);
    }

    bg_match_active_ = true;
}

void SkirmishTournament::finish_bg_match() {
    assert(bg_match_active_ && bg_arena_ && bg_arena_->is_over());

    auto [idx_a, idx_b] = rounds_[round_idx_].matchups[bg_match_cursor_];
    const std::size_t variant_indices[2] = {idx_a, idx_b};
    score_match(*bg_arena_, config_, bg_ship_teams_, variant_indices, scores_);

    bg_arena_.reset();
    bg_ntm_nets_.clear();
    bg_leader_nets_.clear();
    bg_fighter_nets_.clear();
    bg_recurrent_states_.clear();
    bg_ship_teams_.clear();
    bg_match_active_ = false;

    // Advance cursors
    const std::size_t num_seeds = seeds_for_current_round();
    ++bg_seed_cursor_;
    if (bg_seed_cursor_ >= num_seeds) {
        bg_seed_cursor_ = 0;
        ++bg_match_cursor_;
    }

    if (bg_match_cursor_ >= rounds_[round_idx_].matchups.size()) {
        background_matches_done_ = true;
    }
}

void SkirmishTournament::drain_background() {
    while (!background_matches_done_) {
        if (!bg_match_active_) {
            if (bg_match_cursor_ >= rounds_[round_idx_].matchups.size()) {
                background_matches_done_ = true;
                break;
            }
            start_bg_match();
        }
        tick_arena_match(*bg_arena_, bg_arena_config_, fighter_design_,
                         bg_ntm_nets_, bg_leader_nets_, bg_fighter_nets_,
                         bg_recurrent_states_, bg_ship_teams_, nullptr);
        if (bg_arena_->is_over()) {
            finish_bg_match();
        }
    }
}

// ── Match Lifecycle ─────────────────────────────────────────────────────────

void SkirmishTournament::start_match() {
    const auto& matchup = rounds_[round_idx_].matchups[match_idx_];
    const std::size_t idx_a = matchup.first;
    const std::size_t idx_b = matchup.second;

    // Build ArenaConfig for a 2-team match
    arena_config_ = config_.to_arena_config();
    arena_config_.world.num_teams = 2;

    // Create arena with a random seed
    const auto arena_seed = rng_();
    arena_ = std::make_unique<ArenaSession>(arena_config_, arena_seed);

    // Build networks for both teams
    ntm_nets_.clear();
    leader_nets_.clear();
    fighter_nets_.clear();

    ntm_nets_.push_back(variants_[idx_a].build_ntm_network());
    ntm_nets_.push_back(variants_[idx_b].build_ntm_network());

    leader_nets_.push_back(variants_[idx_a].build_squad_network());
    leader_nets_.push_back(variants_[idx_b].build_squad_network());

    fighter_nets_.push_back(variants_[idx_a].build_fighter_network());
    fighter_nets_.push_back(variants_[idx_b].build_fighter_network());

    // Initialize recurrent states
    const std::size_t total_ships = arena_->ships().size();
    recurrent_states_.assign(
        total_ships, std::vector<float>(fighter_design_.memory_slots, 0.0f));

    // Build ship-to-team lookup
    ship_teams_.resize(total_ships);
    for (std::size_t i = 0; i < total_ships; ++i) {
        ship_teams_[i] = arena_->team_of(i);
    }

    last_sl_inputs_.assign(total_ships, SquadLeaderFighterInputs{});
    last_leader_inputs_.assign(2, std::vector<float>(17, 0.0f));
    last_fighter_inputs_.assign(total_ships, std::vector<float>{});
    match_in_progress_ = true;
}

void SkirmishTournament::run_tick() {
    assert(arena_);
    tick_arena_match(*arena_, arena_config_, fighter_design_,
                     ntm_nets_, leader_nets_, fighter_nets_,
                     recurrent_states_, ship_teams_, &last_sl_inputs_,
                     &last_leader_inputs_, &last_fighter_inputs_);
}

void SkirmishTournament::finish_match() {
    assert(arena_);
    const auto& matchup = rounds_[round_idx_].matchups[match_idx_];
    const std::size_t variant_indices[2] = {matchup.first, matchup.second};
    score_match(*arena_, config_, ship_teams_, variant_indices, scores_);

    // Reset match state
    arena_.reset();
    ntm_nets_.clear();
    leader_nets_.clear();
    fighter_nets_.clear();
    recurrent_states_.clear();
    ship_teams_.clear();
    match_in_progress_ = false;

    // Advance seed — featured match is always matchup 0
    ++seed_idx_;
    if (seed_idx_ >= seeds_for_current_round()) {
        // Featured match done with all seeds → round is complete
        // (background matches already ran for matchups 1..N)
        seed_idx_ = 0;
        match_idx_ = 0;
        background_matches_done_ = false;
        bg_match_cursor_ = 1;
        bg_seed_cursor_ = 0;
        {
            // Round complete: determine winners
            const auto& round = rounds_[round_idx_];

            std::vector<std::size_t> winners;

            if (active_.size() <= 3) {
                // Finals: the variant with the highest accumulated score wins.
                // All active variants played; just pick the top one.
                // Tournament is complete after finals.
                complete_ = true;
                return;
            }

            // Regular round: for each matchup, higher score advances
            for (const auto& [a, b] : round.matchups) {
                if (scores_[a] >= scores_[b]) {
                    winners.push_back(a);
                } else {
                    winners.push_back(b);
                }
            }

            // Add bye variant
            if (round.bye_index != SIZE_MAX) {
                winners.push_back(round.bye_index);
            }

            active_ = winners;
            ++round_idx_;

            if (active_.size() <= 1) {
                complete_ = true;
                return;
            }

            // Shuffle and build the next round
            std::shuffle(active_.begin(), active_.end(), rng_);
            build_rounds();
        }
    }
}

// ── Step ────────────────────────────────────────────────────────────────────

bool SkirmishTournament::step() {
    if (complete_) return true;

    if (!match_in_progress_) {
        if (round_idx_ < rounds_.size() &&
            match_idx_ < rounds_[round_idx_].matchups.size()) {
            start_match();
            return false;
        }
        complete_ = true;
        return true;
    }

    if (arena_ && !arena_->is_over()) {
        run_tick();
        return false;
    }

    if (arena_ && arena_->is_over()) {
        // Drain remaining background work before scoring the round
        drain_background();
        finish_match();
        return complete_;
    }

    return complete_;
}

void SkirmishTournament::run_background_work(int budget_ms) {
    if (background_matches_done_) return;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(budget_ms);

    while (!background_matches_done_) {
        if (!bg_match_active_) {
            if (bg_match_cursor_ >= rounds_[round_idx_].matchups.size()) {
                background_matches_done_ = true;
                break;
            }
            start_bg_match();
        }

        tick_arena_match(*bg_arena_, bg_arena_config_, fighter_design_,
                         bg_ntm_nets_, bg_leader_nets_, bg_fighter_nets_,
                         bg_recurrent_states_, bg_ship_teams_, nullptr);

        if (bg_arena_->is_over()) {
            finish_bg_match();
        }

        if (std::chrono::steady_clock::now() >= deadline) break;
    }
}

} // namespace neuroflyer

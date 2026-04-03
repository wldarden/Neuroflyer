#include <neuroflyer/skirmish_tournament.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/sensor_engine.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace neuroflyer {

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

// ── Match Lifecycle ─────────────────────────────────────────────────────────

void SkirmishTournament::start_match() {
    const auto& matchup = rounds_[round_idx_].matchups[match_idx_];
    const std::size_t idx_a = matchup.first;
    const std::size_t idx_b = matchup.second;

    // Build ArenaConfig for a 2-team match
    arena_config_ = config_.to_arena_config();
    arena_config_.num_teams = 2;

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

    match_in_progress_ = true;
}

void SkirmishTournament::run_tick() {
    assert(arena_);
    const std::size_t total_ships = arena_->ships().size();

    // Build sector grid for this tick
    SectorGrid grid(arena_config_.world_width, arena_config_.world_height,
                    arena_config_.sector_size);
    // Insert alive ships
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (arena_->ships()[i].alive) {
            grid.insert(i, arena_->ships()[i].x, arena_->ships()[i].y);
        }
    }
    // Insert bases
    for (std::size_t b = 0; b < arena_->bases().size(); ++b) {
        if (arena_->bases()[b].alive()) {
            grid.insert(total_ships + b, arena_->bases()[b].x, arena_->bases()[b].y);
        }
    }

    // Per team: run NTM + squad leader -> SquadLeaderOrder
    constexpr std::size_t kNumTeams = 2;
    std::vector<SquadLeaderOrder> team_orders(kNumTeams);
    std::vector<float> squad_center_xs(kNumTeams, 0.0f);
    std::vector<float> squad_center_ys(kNumTeams, 0.0f);

    for (std::size_t t = 0; t < kNumTeams; ++t) {
        const int team = static_cast<int>(t);
        auto stats = arena_->compute_squad_stats(team, 0);
        squad_center_xs[t] = stats.centroid_x;
        squad_center_ys[t] = stats.centroid_y;

        auto threats = gather_near_threats(
            grid, stats.centroid_x, stats.centroid_y,
            arena_config_.ntm_sector_radius, team,
            arena_->ships(), ship_teams_, arena_->bases());

        auto ntm = run_ntm_threat_selection(
            ntm_nets_[t], stats.centroid_x, stats.centroid_y,
            stats.alive_fraction, threats,
            arena_config_.world_width, arena_config_.world_height);

        // Find bases
        const float own_base_x = arena_->bases()[t].x;
        const float own_base_y = arena_->bases()[t].y;
        const float own_base_hp = arena_->bases()[t].hp_normalized();
        float enemy_base_x = 0, enemy_base_y = 0;
        float min_dist_sq = std::numeric_limits<float>::max();
        for (const auto& base : arena_->bases()) {
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

        // Compute squad leader inputs
        const float world_diag = std::sqrt(
            arena_config_.world_width * arena_config_.world_width +
            arena_config_.world_height * arena_config_.world_height);
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

        team_orders[t] = run_squad_leader(
            leader_nets_[t], stats.alive_fraction,
            home_heading_sin, home_heading_cos, home_distance,
            own_base_hp, stats.squad_spacing,
            cmd_heading_sin, cmd_heading_cos, cmd_target_distance,
            ntm, own_base_x, own_base_y, enemy_base_x, enemy_base_y);
    }

    // For each alive ship: build input, run fighter net, decode output, set actions
    for (std::size_t i = 0; i < total_ships; ++i) {
        if (!arena_->ships()[i].alive) continue;

        const int team = ship_teams_[i];
        const auto t = static_cast<std::size_t>(team);

        // Compute squad leader fighter inputs
        auto sl_inputs = compute_squad_leader_fighter_inputs(
            arena_->ships()[i].x, arena_->ships()[i].y,
            arena_->ships()[i].rotation,
            team_orders[t],
            squad_center_xs[t], squad_center_ys[t],
            arena_config_.world_width, arena_config_.world_height);

        // Build ArenaQueryContext
        auto ctx = ArenaQueryContext::for_ship(
            arena_->ships()[i], i, team,
            arena_config_.world_width, arena_config_.world_height,
            arena_->towers(), arena_->tokens(),
            arena_->ships(), ship_teams_, arena_->bullets());

        // Build fighter input
        auto input = build_arena_ship_input(
            fighter_design_, ctx,
            sl_inputs.squad_target_heading, sl_inputs.squad_target_distance,
            sl_inputs.squad_center_heading, sl_inputs.squad_center_distance,
            sl_inputs.aggression, sl_inputs.spacing,
            recurrent_states_[i]);

        // Forward fighter net
        auto output = fighter_nets_[t].forward(
            std::span<const float>(input));

        // Decode output into actions + memory
        auto decoded = decode_output(
            std::span<const float>(output),
            fighter_design_.memory_slots);

        // Set ship actions
        arena_->set_ship_actions(i,
            decoded.up, decoded.down, decoded.left, decoded.right, decoded.shoot);

        // Save recurrent state
        recurrent_states_[i] = decoded.memory;
    }

    // Advance arena one tick
    arena_->tick();
}

void SkirmishTournament::finish_match() {
    assert(arena_);
    const auto& matchup = rounds_[round_idx_].matchups[match_idx_];
    const std::size_t idx_a = matchup.first;
    const std::size_t idx_b = matchup.second;
    const std::size_t variant_indices[2] = {idx_a, idx_b};

    const std::size_t total_ships = arena_->ships().size();
    const auto& kills = arena_->enemy_kills();

    // Compute kill-based scores per team (same formula as run_skirmish_match)
    for (std::size_t t = 0; t < 2; ++t) {
        const int team = static_cast<int>(t);
        float score = 0.0f;

        // +kill_points per enemy killed by any fighter on this team
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams_[i] == team) {
                score += config_.kill_points * static_cast<float>(kills[i]);
            }
        }

        // +base_kill_points() for each enemy base destroyed
        for (const auto& base : arena_->bases()) {
            if (base.team_id == team) continue;
            if (!base.alive()) {
                score += config_.base_kill_points();
            }
        }

        // -death_points per own dead fighter
        for (std::size_t i = 0; i < total_ships; ++i) {
            if (ship_teams_[i] == team && !arena_->ships()[i].alive) {
                score -= config_.death_points;
            }
        }

        scores_[variant_indices[t]] += score;
    }

    // Reset match state
    arena_.reset();
    ntm_nets_.clear();
    leader_nets_.clear();
    fighter_nets_.clear();
    recurrent_states_.clear();
    ship_teams_.clear();
    match_in_progress_ = false;

    // Advance seed/match/round
    ++seed_idx_;
    if (seed_idx_ >= seeds_for_current_round()) {
        seed_idx_ = 0;
        ++match_idx_;

        if (match_idx_ >= rounds_[round_idx_].matchups.size()) {
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
            match_idx_ = 0;
            ++round_idx_;

            if (active_.size() <= 1) {
                complete_ = true;
                return;
            }

            // Build the next round
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
        finish_match();
        return complete_;
    }

    return complete_;
}

} // namespace neuroflyer

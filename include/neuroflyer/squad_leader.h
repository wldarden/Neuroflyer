#pragma once

#include <neuroflyer/arena_config.h>
#include <neuroflyer/base.h>
#include <neuroflyer/game.h>
#include <neuroflyer/sector_grid.h>

#include <neuralnet/network.h>

#include <cstddef>
#include <span>
#include <vector>

namespace neuroflyer {

// ── Order Enums ──────────────────────────────────────────────────────────────

enum class TacticalOrder {
    AttackStarbase = 0,
    AttackShip = 1,
    DefendHome = 2
};

enum class SpacingOrder {
    Expand = 0,
    Contract = 1
};

// ── NTM (Near Threat Matrix) ─────────────────────────────────────────────────

/// One nearby threat entity visible to a squad leader.
struct NearThreat {
    float x = 0, y = 0;             // world position
    float health = 1.0f;            // normalized HP (ships: alive ? 1 : 0, bases: hp/max)
    bool is_ship = false;
    bool is_starbase = false;
    std::size_t entity_id = 0;      // index into ships or bases array
};

/// Result of running NTMs and selecting the top-1 threat.
struct NtmResult {
    bool active = false;             // true if any threats were found
    float threat_score = 0.0f;       // evolved score of the active threat
    float target_x = 0, target_y = 0; // position of active threat entity
    float heading = 0.0f;            // direction from squad center (radians, normalized)
    float distance = 0.0f;           // distance from squad center (normalized to world diag)
};

/// Gather nearby enemy entities from the sector grid for a given squad.
/// Returns one NearThreat per enemy entity within the NTM diamond.
[[nodiscard]] std::vector<NearThreat> gather_near_threats(
    const SectorGrid& grid,
    float squad_center_x, float squad_center_y,
    int ntm_sector_radius,
    int squad_team,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Base> bases);

/// Run NTM sub-nets for all nearby threats and select the top-1.
/// ntm_net uses shared weights — called once per threat with different inputs.
[[nodiscard]] NtmResult run_ntm_threat_selection(
    const neuralnet::Network& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h);

// ── Squad Leader ─────────────────────────────────────────────────────────────

/// Full result of running the squad leader net for one squad.
struct SquadLeaderOrder {
    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    SpacingOrder spacing = SpacingOrder::Expand;

    // Target entity position (determined by tactical order + NTM result)
    float target_x = 0, target_y = 0;
};

/// Run the squad leader net and interpret its output into orders.
/// commander_target_x/y: Phase 1 = enemy starbase. Phase 2+ = commander-selected.
[[nodiscard]] SquadLeaderOrder run_squad_leader(
    const neuralnet::Network& leader_net,
    float squad_health,
    float home_distance,
    float home_heading,
    float home_health,
    float squad_spacing,
    float commander_target_heading,
    float commander_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y);

// ── Fighter Inputs ───────────────────────────────────────────────────────────

/// The 6 structured inputs a fighter receives from its squad leader.
struct SquadLeaderFighterInputs {
    float squad_target_heading = 0;    // dir from this fighter to order target
    float squad_target_distance = 0;   // dist from this fighter to order target (normalized)
    float squad_center_heading = 0;    // dir from this fighter to squad center
    float squad_center_distance = 0;   // dist from this fighter to squad center (normalized)
    float aggression = 0;              // +1 attack, -1 defend
    float spacing = 0;                 // +1 expand, -1 contract
};

/// Compute the 6 fighter inputs from the squad leader's orders.
[[nodiscard]] SquadLeaderFighterInputs compute_squad_leader_fighter_inputs(
    float fighter_x, float fighter_y,
    const SquadLeaderOrder& order,
    float squad_center_x, float squad_center_y,
    float world_w, float world_h);

} // namespace neuroflyer

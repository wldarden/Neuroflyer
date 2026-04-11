#include <neuroflyer/squad_leader.h>
#include <neuroflyer/arena_sensor.h>  // for compute_dir_range

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace neuroflyer {

std::vector<NearThreat> gather_near_threats(
    const SectorGrid& grid,
    float squad_center_x, float squad_center_y,
    int ntm_sector_radius,
    int squad_team,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Base> bases) {

    auto center_sector = grid.sector_of(squad_center_x, squad_center_y);
    auto entity_ids = grid.entities_in_diamond(center_sector, ntm_sector_radius);

    std::vector<NearThreat> threats;
    std::size_t ship_count = ships.size();

    for (auto id : entity_ids) {
        if (id < ship_count) {
            // It's a ship
            if (!ships[id].alive) continue;
            if (ship_teams[id] == squad_team) continue;  // skip friendlies

            NearThreat t;
            t.x = ships[id].x;
            t.y = ships[id].y;
            t.health = 1.0f;  // ships: alive = 1.0
            t.is_ship = true;
            t.is_starbase = false;
            t.entity_id = id;
            threats.push_back(t);
        } else {
            // It's a base
            auto base_idx = id - ship_count;
            if (base_idx >= bases.size()) continue;
            if (!bases[base_idx].alive()) continue;
            if (bases[base_idx].team_id == squad_team) continue;  // skip own base

            NearThreat t;
            t.x = bases[base_idx].x;
            t.y = bases[base_idx].y;
            t.health = bases[base_idx].hp_normalized();
            t.is_ship = false;
            t.is_starbase = true;
            t.entity_id = base_idx;
            threats.push_back(t);
        }
    }

    return threats;
}

NtmResult run_ntm_threat_selection(
    const neuralnet::Network& ntm_net,
    float squad_center_x, float squad_center_y,
    float squad_alive_fraction,
    const std::vector<NearThreat>& threats,
    float world_w, float world_h) {

    NtmResult result;
    if (threats.empty()) return result;  // active = false, all zeros

    float best_score = -std::numeric_limits<float>::max();

    for (const auto& threat : threats) {
        auto dr = compute_dir_range(
            squad_center_x, squad_center_y,
            threat.x, threat.y,
            world_w, world_h);

        std::vector<float> ntm_input = {
            dr.dir_sin,                            // heading_sin
            dr.dir_cos,                            // heading_cos
            dr.range,                              // distance (normalized)
            threat.health,
            squad_alive_fraction,
            threat.is_ship ? 1.0f : 0.0f,
            threat.is_starbase ? 1.0f : 0.0f
        };

        auto output = ntm_net.forward(std::span<const float>(ntm_input));
        float threat_score = output[0];

        if (threat_score > best_score) {
            best_score = threat_score;
            result.active = true;
            result.threat_score = threat_score;
            result.target_x = threat.x;
            result.target_y = threat.y;
            result.heading_sin = dr.dir_sin;
            result.heading_cos = dr.dir_cos;
            result.distance = dr.range;
        }
    }

    return result;
}

SquadLeaderOrder run_squad_leader(
    const neuralnet::Network& leader_net,
    float squad_health,
    float home_heading_sin, float home_heading_cos, float home_distance,
    float home_health,
    float cmd_target_heading_sin, float cmd_target_heading_cos, float cmd_target_distance,
    const NtmResult& ntm,
    float own_base_x, float own_base_y,
    float enemy_base_x, float enemy_base_y,
    float enemy_alive_fraction,
    float time_remaining,
    float squad_center_x_norm,
    float squad_center_y_norm) {

    std::vector<float> input = {
        squad_health,
        home_heading_sin,
        home_heading_cos,
        home_distance,
        home_health,
        cmd_target_heading_sin,
        cmd_target_heading_cos,
        cmd_target_distance,
        ntm.active ? 1.0f : 0.0f,
        ntm.active ? ntm.heading_sin : 0.0f,
        ntm.active ? ntm.heading_cos : 0.0f,
        ntm.active ? ntm.distance : 0.0f,
        ntm.active ? ntm.threat_score : 0.0f,
        enemy_alive_fraction,
        time_remaining,
        squad_center_x_norm,
        squad_center_y_norm
    };

    auto output = leader_net.forward(std::span<const float>(input));

    // Argmax for spacing group (outputs 0-1)
    SpacingOrder spacing = (output[0] >= output[1])
        ? SpacingOrder::Expand : SpacingOrder::Contract;

    // Argmax for tactical group (outputs 2-4)
    TacticalOrder tactical = TacticalOrder::AttackStarbase;
    float max_tactical = output[2];
    if (output[3] > max_tactical) {
        max_tactical = output[3];
        tactical = TacticalOrder::AttackShip;
    }
    if (output[4] > max_tactical) {
        tactical = TacticalOrder::DefendHome;
    }

    SquadLeaderOrder order;
    order.tactical = tactical;
    order.spacing = spacing;

    switch (tactical) {
        case TacticalOrder::AttackStarbase:
            order.target_x = enemy_base_x;
            order.target_y = enemy_base_y;
            break;
        case TacticalOrder::AttackShip:
            if (ntm.active) {
                order.target_x = ntm.target_x;
                order.target_y = ntm.target_y;
            } else {
                // Fallback: no active threat, target enemy starbase
                order.target_x = enemy_base_x;
                order.target_y = enemy_base_y;
            }
            break;
        case TacticalOrder::DefendHome:
            order.target_x = own_base_x;
            order.target_y = own_base_y;
            break;
    }

    return order;
}

SquadLeaderFighterInputs compute_squad_leader_fighter_inputs(
    float fighter_x, float fighter_y,
    float fighter_rotation,
    const SquadLeaderOrder& order,
    float squad_center_x, float squad_center_y,
    float world_w, float world_h) {

    constexpr float PI = std::numbers::pi_v<float>;

    SquadLeaderFighterInputs inputs;

    // Squad target: relative to fighter facing
    auto target_dr = compute_dir_range(
        fighter_x, fighter_y,
        order.target_x, order.target_y,
        world_w, world_h);
    float target_world_angle = std::atan2(target_dr.dir_sin, -target_dr.dir_cos);
    float target_relative = target_world_angle - fighter_rotation;
    while (target_relative > PI) target_relative -= 2.0f * PI;
    while (target_relative < -PI) target_relative += 2.0f * PI;
    inputs.squad_target_heading = target_relative / PI;  // [-1, 1]
    inputs.squad_target_distance = target_dr.range;

    // Squad center: relative to fighter facing
    auto center_dr = compute_dir_range(
        fighter_x, fighter_y,
        squad_center_x, squad_center_y,
        world_w, world_h);
    float center_world_angle = std::atan2(center_dr.dir_sin, -center_dr.dir_cos);
    float center_relative = center_world_angle - fighter_rotation;
    while (center_relative > PI) center_relative -= 2.0f * PI;
    while (center_relative < -PI) center_relative += 2.0f * PI;
    inputs.squad_center_heading = center_relative / PI;  // [-1, 1]
    inputs.squad_center_distance = center_dr.range;

    switch (order.tactical) {
        case TacticalOrder::AttackStarbase:
        case TacticalOrder::AttackShip:
            inputs.aggression = 1.0f;
            break;
        case TacticalOrder::DefendHome:
            inputs.aggression = -1.0f;
            break;
    }

    inputs.spacing = (order.spacing == SpacingOrder::Expand) ? 1.0f : -1.0f;

    return inputs;
}

} // namespace neuroflyer

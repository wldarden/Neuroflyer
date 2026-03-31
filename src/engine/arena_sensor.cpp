#include <neuroflyer/arena_config.h>
#include <neuroflyer/arena_sensor.h>
#include <neuroflyer/collision.h>
#include <neuroflyer/sensor_engine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace neuroflyer {

namespace {

/// Ray-circle intersection returning normalized distance [0,1], or 1.0 for miss.
/// Ray is defined by origin (ox,oy) and full vector (dx,dy) where the ray tip
/// is at (ox+dx, oy+dy). range is the actual ray length for normalization.
float ray_circle_hit(float ox, float oy, float dx, float dy, float range,
                     float cx, float cy, float cr) {
    if (range < 1e-6f) return 1.0f;
    float t = ray_circle_intersect(ox, oy, dx, dy, cx, cy, cr);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    float dist = t * std::sqrt(dx * dx + dy * dy);
    return std::min(dist / range, 1.0f);
}

/// Raycast path: cast a ray at abs_angle with sensor.range against all arena entities.
ArenaSensorReading query_arena_raycast(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    float abs_angle = ctx.ship_rotation + sensor.angle;
    float ray_dir_x = std::sin(abs_angle);
    float ray_dir_y = -std::cos(abs_angle);

    float dx = ray_dir_x * sensor.range;
    float dy = ray_dir_y * sensor.range;

    float closest = 1.0f;
    ArenaHitType closest_type = ArenaHitType::Nothing;

    auto update_closest = [&](float d, ArenaHitType type) {
        if (d < closest) {
            closest = d;
            closest_type = type;
        }
    };

    for (const auto& tower : ctx.towers) {
        if (!tower.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 tower.x, tower.y, tower.radius);
        update_closest(d, ArenaHitType::Tower);
    }

    for (const auto& token : ctx.tokens) {
        if (!token.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 token.x, token.y, token.radius);
        update_closest(d, ArenaHitType::Token);
    }

    for (std::size_t i = 0; i < ctx.ships.size(); ++i) {
        if (i == ctx.self_index) continue;
        const auto& ship = ctx.ships[i];
        if (!ship.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 ship.x, ship.y, Triangle::SIZE);
        bool is_friend = (i < ctx.ship_teams.size() &&
                          ctx.ship_teams[i] == ctx.self_team);
        update_closest(d, is_friend ? ArenaHitType::FriendlyShip
                                    : ArenaHitType::EnemyShip);
    }

    for (const auto& bullet : ctx.bullets) {
        if (!bullet.alive) continue;
        if (bullet.owner_index == static_cast<int>(ctx.self_index)) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 bullet.x, bullet.y, BULLET_RADIUS);
        update_closest(d, ArenaHitType::Bullet);
    }

    return {closest, closest_type};
}

/// Occulus path: place a rotated ellipse and check point-in-ellipse for all arena entities.
/// Uses compute_sensor_shape() with the absolute angle (ship_rotation + sensor.angle)
/// so the ellipse rotates with the ship.
ArenaSensorReading query_arena_occulus(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    SensorDef rotated = sensor;
    rotated.angle = ctx.ship_rotation + sensor.angle;
    auto shape = compute_sensor_shape(rotated, ctx.ship_x, ctx.ship_y);

    float closest_dist = 1.0f;
    ArenaHitType closest_type = ArenaHitType::Nothing;

    auto update_closest = [&](float d, ArenaHitType type) {
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = type;
        }
    };

    for (const auto& tower : ctx.towers) {
        if (!tower.alive) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      tower.x, tower.y, tower.radius),
            ArenaHitType::Tower);
    }

    for (const auto& token : ctx.tokens) {
        if (!token.alive) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      token.x, token.y, token.radius),
            ArenaHitType::Token);
    }

    for (std::size_t i = 0; i < ctx.ships.size(); ++i) {
        if (i == ctx.self_index) continue;
        const auto& ship = ctx.ships[i];
        if (!ship.alive) continue;
        bool is_friend = (i < ctx.ship_teams.size() &&
                          ctx.ship_teams[i] == ctx.self_team);
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      ship.x, ship.y, Triangle::SIZE),
            is_friend ? ArenaHitType::FriendlyShip : ArenaHitType::EnemyShip);
    }

    for (const auto& bullet : ctx.bullets) {
        if (!bullet.alive) continue;
        if (bullet.owner_index == static_cast<int>(ctx.self_index)) continue;
        update_closest(
            ellipse_overlap_distance(shape, ctx.ship_x, ctx.ship_y,
                                      bullet.x, bullet.y, BULLET_RADIUS),
            ArenaHitType::Bullet);
    }

    return {closest_dist, closest_type};
}

} // namespace

ArenaQueryContext ArenaQueryContext::for_ship(
    const Triangle& ship, std::size_t index, int team,
    float world_w, float world_h,
    std::span<const Tower> towers,
    std::span<const Token> tokens,
    std::span<const Triangle> ships,
    std::span<const int> ship_teams,
    std::span<const Bullet> bullets) {

    ArenaQueryContext ctx;
    ctx.ship_x = ship.x;
    ctx.ship_y = ship.y;
    ctx.ship_rotation = ship.rotation;
    ctx.world_w = world_w;
    ctx.world_h = world_h;
    ctx.self_index = index;
    ctx.self_team = team;
    ctx.towers = towers;
    ctx.tokens = tokens;
    ctx.ships = ships;
    ctx.ship_teams = ship_teams;
    ctx.bullets = bullets;
    return ctx;
}

ArenaSensorReading query_arena_sensor(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    switch (sensor.type) {
    case SensorType::Occulus:
        return query_arena_occulus(sensor, ctx);
    case SensorType::Raycast:
    default:
        return query_arena_raycast(sensor, ctx);
    }
}

DirRange compute_dir_range(
    float from_x, float from_y,
    float to_x, float to_y,
    float world_w, float world_h) {

    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float world_diag = std::sqrt(world_w * world_w + world_h * world_h);
    if (world_diag < 1e-6f) world_diag = 1.0f;

    DirRange result;
    if (distance > 1e-6f) {
        result.dir_sin = dx / distance;
        result.dir_cos = dy / distance;
    }
    result.range = std::clamp(distance / world_diag, 0.0f, 1.0f);
    return result;
}

std::size_t compute_arena_input_size(const ShipDesign& design) {
    std::size_t sensor_values = 0;
    for (const auto& s : design.sensors) {
        sensor_values += s.is_full_sensor ? 5 : 1;
    }
    return sensor_values + ArenaConfig::squad_leader_fighter_inputs + design.memory_slots;
}

std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float squad_target_heading, float squad_target_distance,
    float squad_center_heading, float squad_center_distance,
    float aggression, float spacing,
    std::span<const float> memory) {

    std::vector<float> input;
    input.reserve(compute_arena_input_size(design));

    // --- Sensor values ---
    for (const auto& sensor : design.sensors) {
        auto reading = query_arena_sensor(sensor, ctx);
        input.push_back(reading.distance);

        if (sensor.is_full_sensor) {
            // 5 values per full sensor: distance, is_tower, is_token, is_friend, is_bullet
            input.push_back(reading.entity_type == ArenaHitType::Tower ? 1.0f : 0.0f);
            input.push_back(reading.entity_type == ArenaHitType::Token ? 1.0f : 0.0f);

            // is_friend: -1 = enemy, 0 = not a ship, 1 = friendly
            float is_friend = 0.0f;
            if (reading.entity_type == ArenaHitType::FriendlyShip) is_friend = 1.0f;
            else if (reading.entity_type == ArenaHitType::EnemyShip) is_friend = -1.0f;
            input.push_back(is_friend);

            input.push_back(reading.entity_type == ArenaHitType::Bullet ? 1.0f : 0.0f);
        }
    }

    // --- Squad leader inputs (6) ---
    input.push_back(squad_target_heading);
    input.push_back(squad_target_distance);
    input.push_back(squad_center_heading);
    input.push_back(squad_center_distance);
    input.push_back(aggression);
    input.push_back(spacing);

    // --- Memory ---
    input.insert(input.end(), memory.begin(), memory.end());

    return input;
}

std::vector<std::string> build_arena_fighter_input_labels(const ShipDesign& design) {
    std::vector<std::string> labels;

    // Sensor labels: 5 per full sensor, 1 per sight sensor.
    int sensor_idx = 0;
    for (const auto& s : design.sensors) {
        char buf[32];
        if (!s.is_full_sensor) {
            std::snprintf(buf, sizeof(buf), "SNS %d D", sensor_idx);
            labels.push_back(buf);
        } else {
            std::snprintf(buf, sizeof(buf), "SNS %d D", sensor_idx);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %d TWR", sensor_idx);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %d TKN", sensor_idx);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %d FRD", sensor_idx);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %d BLT", sensor_idx);
            labels.push_back(buf);
        }
        ++sensor_idx;
    }

    // Squad leader inputs (6).
    labels.push_back("TGT HDG");
    labels.push_back("TGT DST");
    labels.push_back("CTR HDG");
    labels.push_back("CTR DST");
    labels.push_back("AGG");
    labels.push_back("SPC");

    // Memory.
    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "MEM %d", m);
        labels.push_back(buf);
    }

    return labels;
}

std::vector<std::size_t> build_arena_fighter_display_order(const ShipDesign& design) {
    // Identity permutation — no reordering needed for arena fighter nets.
    std::size_t total = compute_arena_input_size(design);
    std::vector<std::size_t> order(total);
    for (std::size_t i = 0; i < total; ++i) {
        order[i] = i;
    }
    return order;
}

} // namespace neuroflyer

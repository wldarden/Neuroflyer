#include <neuroflyer/arena_sensor.h>

#include <algorithm>
#include <cmath>

namespace neuroflyer {

namespace {

/// Ray-circle intersection returning normalized distance [0,1], or 1.0 for miss.
/// Ray is defined by origin (ox,oy) and full vector (dx,dy) where the ray tip
/// is at (ox+dx, oy+dy). range is the actual ray length for normalization.
float ray_circle_hit(float ox, float oy, float dx, float dy, float range,
                     float cx, float cy, float cr) {
    // Use f = O - C (same convention as collision.h ray_circle_intersect)
    float fx = ox - cx;
    float fy = oy - cy;
    float a = dx * dx + dy * dy;
    float b = 2.0f * (fx * dx + fy * dy);
    float c_val = fx * fx + fy * fy - cr * cr;
    float disc = b * b - 4.0f * a * c_val;
    if (disc < 0.0f) return 1.0f;
    float sqrt_disc = std::sqrt(disc);
    float t = (-b - sqrt_disc) / (2.0f * a);
    if (t < 0.0f) t = (-b + sqrt_disc) / (2.0f * a);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    float dist = t * std::sqrt(a);
    return std::min(dist / range, 1.0f);
}

} // namespace

ArenaSensorReading query_arena_sensor(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx) {

    // Ray direction: rotation 0 = facing up, sensor.angle relative to ship rotation
    float abs_angle = ctx.ship_rotation + sensor.angle;
    float ray_dir_x = std::sin(abs_angle);
    float ray_dir_y = -std::cos(abs_angle);

    // Full ray vector (origin + this vector = ray tip)
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

    // Check towers
    for (const auto& tower : ctx.towers) {
        if (!tower.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 tower.x, tower.y, tower.radius);
        update_closest(d, ArenaHitType::Tower);
    }

    // Check tokens
    for (const auto& token : ctx.tokens) {
        if (!token.alive) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 token.x, token.y, token.radius);
        update_closest(d, ArenaHitType::Token);
    }

    // Check ships (skip self, classify friend/enemy)
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

    // Check bullets (skip own bullets)
    constexpr float BULLET_RADIUS = 2.0f;
    for (const auto& bullet : ctx.bullets) {
        if (!bullet.alive) continue;
        if (bullet.owner_index == static_cast<int>(ctx.self_index)) continue;
        float d = ray_circle_hit(ctx.ship_x, ctx.ship_y, dx, dy, sensor.range,
                                 bullet.x, bullet.y, BULLET_RADIUS);
        update_closest(d, ArenaHitType::Bullet);
    }

    return {closest, closest_type};
}

DirRange compute_dir_range(
    float from_x, float from_y,
    float to_x, float to_y,
    float world_w, float world_h) {

    float dx = to_x - from_x;
    float dy = to_y - from_y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float world_diag = std::sqrt(world_w * world_w + world_h * world_h);

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
    return sensor_values + 6 + design.memory_slots;
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

} // namespace neuroflyer

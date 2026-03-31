#include <neuroflyer/sensor_engine.h>
#include <neuroflyer/collision.h>
#include <neuroflyer/ui/theme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace neuroflyer {

// --- Shared sensor shape derivation ---

SensorShape compute_sensor_shape(const SensorDef& sensor, float ship_x, float ship_y) {
    if (sensor.type != SensorType::Occulus) {
        return {};  // raycasts are lines, no shape
    }

    // Match the renderer's formula exactly:
    //   major_radius = range * 0.5
    //   center_distance = SHIP_GAP + major_radius
    //   minor_radius = width * center_distance  (width is the tangent-like ratio)
    float major_r = sensor.range * 0.5f;
    float center_dist = SHIP_SENSOR_GAP + major_r;

    float sin_a = std::sin(sensor.angle);
    float cos_a = std::cos(sensor.angle);

    SensorShape shape;
    shape.center_x = ship_x + sin_a * center_dist;
    shape.center_y = ship_y - cos_a * center_dist;
    shape.major_radius = major_r;
    shape.minor_radius = sensor.width * center_dist;
    shape.rotation = sensor.angle - std::numbers::pi_v<float> * 0.5f;
    return shape;
}

namespace {

/// Raycast mode: cast a single ray at sensor.angle with sensor.range.
SensorReading query_raycast(const SensorDef& sensor,
                            float ship_x, float ship_y,
                            const std::vector<Tower>& towers,
                            const std::vector<Token>& tokens) {
    // Direction vector: angle 0 = forward (negative Y), positive angle = right
    float dx = std::sin(sensor.angle);
    float dy = -std::cos(sensor.angle);

    float closest = sensor.range;
    HitType closest_type = HitType::Nothing;

    for (const auto& tower : towers) {
        if (!tower.alive) continue;
        float t = ray_circle_intersect(ship_x, ship_y, dx, dy,
                                       tower.x, tower.y, tower.radius);
        if (t >= 0.0f && t < closest) {
            closest = t;
            closest_type = HitType::Tower;
        }
    }

    for (const auto& token : tokens) {
        if (!token.alive) continue;
        float t = ray_circle_intersect(ship_x, ship_y, dx, dy,
                                       token.x, token.y, token.radius);
        if (t >= 0.0f && t < closest) {
            closest = t;
            closest_type = HitType::Token;
        }
    }

    return {closest / sensor.range, closest_type};
}

/// Occulus mode: place an ellipse at the sensor position and check overlap.
/// Uses compute_sensor_shape for the ellipse geometry (single source of truth).
SensorReading query_occulus(const SensorDef& sensor,
                            float ship_x, float ship_y,
                            const std::vector<Tower>& towers,
                            const std::vector<Token>& tokens) {
    auto shape = compute_sensor_shape(sensor, ship_x, ship_y);

    float closest_dist = 1.0f;
    HitType closest_type = HitType::Nothing;

    for (const auto& tower : towers) {
        if (!tower.alive) continue;
        float d = ellipse_overlap_distance(shape, ship_x, ship_y,
                                            tower.x, tower.y, tower.radius);
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = HitType::Tower;
        }
    }

    for (const auto& token : tokens) {
        if (!token.alive) continue;
        float d = ellipse_overlap_distance(shape, ship_x, ship_y,
                                            token.x, token.y, token.radius);
        if (d >= 0.0f && d < closest_dist) {
            closest_dist = d;
            closest_type = HitType::Token;
        }
    }

    return {closest_dist, closest_type};
}

} // namespace

float ellipse_overlap_distance(
    const SensorShape& shape,
    float ship_x, float ship_y,
    float obj_x, float obj_y, float obj_radius) {
    float cos_r = std::cos(shape.rotation);
    float sin_r = std::sin(shape.rotation);
    float minor_radius = std::max(shape.minor_radius, 0.01f);

    float ddx = obj_x - shape.center_x;
    float ddy = obj_y - shape.center_y;
    float lmaj = ddx * cos_r + ddy * sin_r;
    float lmin = -ddx * sin_r + ddy * cos_r;
    float eff_maj = shape.major_radius + obj_radius;
    float eff_min = minor_radius + obj_radius;
    float val = (lmaj * lmaj) / (eff_maj * eff_maj) +
                (lmin * lmin) / (eff_min * eff_min);
    if (val <= 1.0f) {
        float obj_dx = obj_x - ship_x;
        float obj_dy = obj_y - ship_y;
        float center_dist = std::sqrt(obj_dx * obj_dx + obj_dy * obj_dy);
        float edge_dist = std::max(0.0f, center_dist - obj_radius);
        float max_reach = SHIP_SENSOR_GAP + shape.major_radius * 2.0f;
        return std::min(edge_dist / max_reach, 1.0f);
    }
    return -1.0f;
}

SensorReading query_sensor(const SensorDef& sensor,
                           float ship_x, float ship_y,
                           const std::vector<Tower>& towers,
                           const std::vector<Token>& tokens) {
    switch (sensor.type) {
    case SensorType::Occulus:
        return query_occulus(sensor, ship_x, ship_y, towers, tokens);
    case SensorType::Raycast:
    default:
        return query_raycast(sensor, ship_x, ship_y, towers, tokens);
    }
}

std::vector<SensorEndpoint> query_sensors_with_endpoints(
    const ShipDesign& design,
    float ship_x, float ship_y,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens) {

    ShipDesign effective = design;
    if (effective.sensors.empty()) {
        effective = create_legacy_ship_design(static_cast<int>(design.memory_slots));
    }
    // Same order as build_ship_input — ShipDesign order, NOT angle-sorted.

    std::vector<SensorEndpoint> endpoints;
    endpoints.reserve(effective.sensors.size());

    for (const auto& sensor : effective.sensors) {
        auto reading = query_sensor(sensor, ship_x, ship_y, towers, tokens);

        SensorEndpoint ep{};
        ep.distance = reading.distance;
        ep.hit = reading.hit;
        ep.is_full_sensor = sensor.is_full_sensor;
        ep.type = sensor.type;

        switch (sensor.type) {
        case SensorType::Raycast: {
            float actual_dist = reading.distance * sensor.range;
            float dx = std::sin(sensor.angle);
            float dy = -std::cos(sensor.angle);
            ep.x = ship_x + dx * actual_dist;
            ep.y = ship_y + dy * actual_dist;
            break;
        }
        case SensorType::Occulus: {
            ep.x = ship_x + std::sin(sensor.angle) * sensor.range;
            ep.y = ship_y - std::cos(sensor.angle) * sensor.range;
            break;
        }
        }

        endpoints.push_back(ep);
    }

    return endpoints;
}

std::vector<float> build_ship_input(
    const ShipDesign& design,
    float ship_x, float ship_y,
    float game_w, float game_h,
    float scroll_speed,
    float pts_per_token,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    std::span<const float> memory) {

    ShipDesign effective = design;
    if (effective.sensors.empty()) {
        effective = create_legacy_ship_design(static_cast<int>(design.memory_slots));
    }
    // DO NOT sort here — input order must match the network's trained expectations.
    // Sensors are iterated in ShipDesign order (stable IDs from genome creation).
    // Visual sorting is done separately in build_input_labels/build_input_colors.

    std::vector<float> input;
    float value_scale = std::max(pts_per_token * 2.0f, 0.1f);

    for (const auto& sensor : effective.sensors) {
        auto reading = query_sensor(sensor, ship_x, ship_y, towers, tokens);
        input.push_back(reading.distance);
        if (sensor.is_full_sensor) {
            bool is_tower = (reading.hit == HitType::Tower);
            bool is_token = (reading.hit == HitType::Token);
            input.push_back(is_tower ? 1.0f : 0.0f);
            float valuable = is_token ? std::min(pts_per_token / value_scale, 1.0f) : 0.0f;
            input.push_back(valuable);
            input.push_back(is_token ? 1.0f : 0.0f);
        }
    }

    input.push_back((ship_x / game_w) * 2.0f - 1.0f);
    input.push_back(1.0f - (ship_y / game_h) * 2.0f);
    input.push_back(scroll_speed / 10.0f);

    input.insert(input.end(), memory.begin(), memory.end());
    return input;
}

DecodedOutput decode_output(std::span<const float> output,
                            std::size_t memory_slots) {
    DecodedOutput d;
    d.up    = output.size() > 0 && output[0] > 0.0f;
    d.down  = output.size() > 1 && output[1] > 0.0f;
    d.left  = output.size() > 2 && output[2] > 0.0f;
    d.right = output.size() > 3 && output[3] > 0.0f;
    d.shoot = output.size() > 4 && output[4] > 0.0f;
    for (std::size_t m = 0; m < memory_slots && (ACTION_COUNT + m) < output.size(); ++m) {
        d.memory.push_back(output[ACTION_COUNT + m]);
    }
    return d;
}

ShipDesign create_legacy_ship_design(int memory_slots) {
    ShipDesign design;
    design.memory_slots = static_cast<uint16_t>(memory_slots);
    constexpr int sight[] = {0, 1, 3, 5, 7, 9, 11, 12};
    constexpr int sensor[] = {2, 4, 6, 8, 10};

    auto range_for_ray = [](int i) -> float {
        return LEGACY_RAY_RANGE * ray_range_multiplier(i, LEGACY_NUM_RAYS);
    };

    constexpr float pi = std::numbers::pi_v<float>;

    // Sight rays first (distance only)
    for (int si : sight) {
        float frac = static_cast<float>(si) / static_cast<float>(LEGACY_NUM_RAYS - 1);
        float angle = -pi / 2.0f + pi * frac;
        design.sensors.push_back({SensorType::Raycast, angle, range_for_ray(si), 0.0f, false});
    }
    // Sensor rays (full info)
    for (int si : sensor) {
        float frac = static_cast<float>(si) / static_cast<float>(LEGACY_NUM_RAYS - 1);
        float angle = -pi / 2.0f + pi * frac;
        design.sensors.push_back({SensorType::Raycast, angle, range_for_ray(si), 0.0f, true});
    }
    assign_sensor_ids(design);
    return design;
}

std::vector<std::string> build_input_labels(const ShipDesign& design) {
    std::vector<std::string> labels;
    constexpr float rad_to_deg = 180.0f / std::numbers::pi_v<float>;

    // Labels in ShipDesign order (matching input vector order).
    // Visual sorting is a display-layer concern, not a data concern.
    for (const auto& s : design.sensors) {
        int deg = static_cast<int>(s.angle * rad_to_deg);
        char prefix[16];
        if (deg < -5) std::snprintf(prefix, sizeof(prefix), "L%d", -deg);
        else if (deg > 5) std::snprintf(prefix, sizeof(prefix), "R%d", deg);
        else std::snprintf(prefix, sizeof(prefix), "C");

        if (!s.is_full_sensor) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "EYE %s", prefix);
            labels.push_back(buf);
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "SNS %s D", prefix);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %s !", prefix);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %s $", prefix);
            labels.push_back(buf);
            std::snprintf(buf, sizeof(buf), "SNS %s ?", prefix);
            labels.push_back(buf);
        }
    }

    labels.push_back("POS X");
    labels.push_back("POS Y");
    labels.push_back("SPEED");

    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "MEM %d", m);
        labels.push_back(buf);
    }

    return labels;
}

std::vector<NodeStyle> build_input_colors(const ShipDesign& design) {
    std::vector<NodeStyle> colors;

    // Colors in ShipDesign order (matching input vector order).
    auto ns = [](const theme::Color& c) -> NodeStyle { return {c.r, c.g, c.b}; };

    for (const auto& s : design.sensors) {
        if (!s.is_full_sensor) {
            colors.push_back(ns(theme::node_sight));
        } else {
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
            colors.push_back(ns(theme::node_sensor));
        }
    }

    // System inputs: pos_x, pos_y, speed
    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));
    colors.push_back(ns(theme::node_system));

    // Memory inputs
    for (uint16_t m = 0; m < design.memory_slots; ++m) {
        colors.push_back(ns(theme::node_memory));
    }

    return colors;
}

std::vector<std::size_t> build_display_order(const ShipDesign& design) {
    // Each sensor occupies 1 input (sight) or 4 inputs (full sensor).
    // Build (data_start_index, angle) pairs for each sensor, then sort by angle.
    struct SensorSpan {
        std::size_t data_start;
        std::size_t count;  // 1 or 4
        float angle;
    };
    std::vector<SensorSpan> spans;
    std::size_t idx = 0;
    for (const auto& s : design.sensors) {
        std::size_t n = s.is_full_sensor ? 4 : 1;
        spans.push_back({idx, n, s.angle});
        idx += n;
    }

    // System inputs (pos_x, pos_y, speed) — no angle, sort after sensors
    std::size_t system_start = idx;
    constexpr std::size_t SYSTEM_COUNT = 3;
    idx += SYSTEM_COUNT;

    // Memory inputs — sort after system
    std::size_t mem_start = idx;
    std::size_t mem_count = design.memory_slots;

    // Sort sensor spans by angle
    std::sort(spans.begin(), spans.end(),
        [](const SensorSpan& a, const SensorSpan& b) { return a.angle < b.angle; });

    // Build the permutation: display_order[visual_pos] = data_index
    std::vector<std::size_t> order;
    order.reserve(idx + mem_count);

    // Sensors sorted by angle
    for (const auto& span : spans) {
        for (std::size_t j = 0; j < span.count; ++j) {
            order.push_back(span.data_start + j);
        }
    }

    // System inputs
    for (std::size_t j = 0; j < SYSTEM_COUNT; ++j) {
        order.push_back(system_start + j);
    }

    // Memory inputs
    for (std::size_t j = 0; j < mem_count; ++j) {
        order.push_back(mem_start + j);
    }

    return order;
}

std::vector<std::string> build_squad_leader_input_labels() {
    return {
        "Sqd HP",   // squad health
        "Home Sin",  // home heading sin
        "Home Cos",  // home heading cos
        "Home Dst",  // home distance
        "Home HP",   // home health
        "Spacing",   // squad spacing
        "Cmd Sin",   // commander target heading sin
        "Cmd Cos",   // commander target heading cos
        "Cmd Dst",   // commander target distance
        "Threat?",   // active threat flag
        "Thr Sin",   // threat heading sin
        "Thr Cos",   // threat heading cos
        "Thr Dst",   // threat distance
        "Thr Scr",   // threat score
    };
}

std::vector<std::string> build_squad_leader_output_labels() {
    return {
        "EXPD",   // expand
        "CNTR",   // contract
        "A.BAS",  // attack starbase
        "A.SHP",  // attack ship
        "D.HOM",  // defend home
    };
}

} // namespace neuroflyer

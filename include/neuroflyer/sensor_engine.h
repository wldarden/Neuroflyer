#pragma once
#include <neuroflyer/game.h>
#include <neuroflyer/ray.h>
#include <neuroflyer/ship_design.h>
#include <span>
#include <string>
#include <vector>

namespace neuroflyer {

/// Legacy constants for backward compatibility with the original 13-ray layout.
inline constexpr int LEGACY_NUM_RAYS = 13;
inline constexpr float LEGACY_RAY_RANGE = 300.0f;

/// Derived geometric shape of a sensor — THE single derivation function.
/// Used by: query_occulus (detection), renderer (drawing), visualization (endpoints).
struct SensorShape {
    float center_x = 0, center_y = 0;  // ellipse center (world coords)
    float major_radius = 0;             // along the radial direction
    float minor_radius = 0;             // perpendicular to radial
    float rotation = 0;                 // radians
};

/// Compute the geometric shape of an Occulus sensor given ship position.
/// For Raycast sensors, returns zero shape (raycasts are lines, not ellipses).
[[nodiscard]] SensorShape compute_sensor_shape(
    const SensorDef& sensor, float ship_x, float ship_y);

/// Result of querying one sensor against the world.
struct SensorReading {
    float distance = 1.0f;
    HitType hit = HitType::Nothing;
};

/// Query a single sensor. Dispatches on sensor type.
[[nodiscard]] SensorReading query_sensor(
    const SensorDef& sensor,
    float ship_x, float ship_y,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens);

/// Build the complete neural net input vector from a ShipDesign.
/// THE single source of truth — all consumers call this.
[[nodiscard]] std::vector<float> build_ship_input(
    const ShipDesign& design,
    float ship_x, float ship_y,
    float game_w, float game_h,
    float scroll_speed,
    float pts_per_token,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens,
    std::span<const float> memory);

/// Endpoint information for visualizing a sensor.
struct SensorEndpoint {
    float x, y;              // endpoint position (world coords)
    float distance;          // 0-1 normalized
    HitType hit;
    bool is_full_sensor;     // for color coding: sight vs sensor
    SensorType type;         // Raycast vs Occulus
};

/// Query all sensors in a ShipDesign and return endpoints for visualization.
/// Same sensors, same math as build_ship_input — but also returns positions for drawing.
[[nodiscard]] std::vector<SensorEndpoint> query_sensors_with_endpoints(
    const ShipDesign& design,
    float ship_x, float ship_y,
    const std::vector<Tower>& towers,
    const std::vector<Token>& tokens);

/// Decode neural net output into actions + memory.
struct DecodedOutput {
    bool up = false, down = false, left = false, right = false, shoot = false;
    std::vector<float> memory;
};
[[nodiscard]] DecodedOutput decode_output(
    std::span<const float> output,
    std::size_t memory_slots);

/// Create a default ShipDesign matching the legacy 13-ray layout.
/// Used for backward compat with old genomes that have empty sensor lists.
[[nodiscard]] ShipDesign create_legacy_ship_design(int memory_slots);

/// Build input labels from a ShipDesign, ordered by sensor definition.
/// Returns one label per input (sight/sensor/pos/speed/memory).
[[nodiscard]] std::vector<std::string> build_input_labels(const ShipDesign& design);

/// Build input node colors from a ShipDesign.
/// Green=sight, Purple=sensor, Blue=system, Red=memory.
[[nodiscard]] std::vector<NodeStyle> build_input_colors(const ShipDesign& design);

/// Build a display permutation that sorts input nodes visually by angle.
/// Sensors sorted -90° to +90° (top to bottom), then system (pos/speed),
/// then memory at the bottom. display_order[visual_pos] = data_index.
/// Data order is unchanged — this is purely a visual rearrangement.
[[nodiscard]] std::vector<std::size_t> build_display_order(const ShipDesign& design);

} // namespace neuroflyer

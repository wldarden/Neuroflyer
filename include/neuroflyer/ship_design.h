#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace neuroflyer {

inline constexpr std::size_t ACTION_COUNT = 5;

enum class SensorType : uint8_t { Raycast = 0, Occulus = 1 };

struct SensorDef {
    SensorType type;
    float angle;
    float range;
    float width;
    bool is_full_sensor;
    uint16_t id = 0;  // 0 = unset sentinel
};

/// Per-parameter evolution toggles — which properties can mutate during training.
struct EvolvableFlags {
    bool sensor_angle = false;
    bool sensor_range = false;
    bool sensor_width = false;
    bool activation_function = false;
};

/// RGB color for an input node in the net panel.
struct NodeStyle {
    uint8_t r, g, b;
};

struct ShipDesign {
    std::vector<SensorDef> sensors;
    uint16_t memory_slots = 4;
    EvolvableFlags evolvable;
};

[[nodiscard]] inline std::size_t compute_input_size(const ShipDesign& design) {
    std::size_t size = 3;  // pos_x, pos_y, speed
    for (const auto& s : design.sensors) {
        size += s.is_full_sensor ? 4 : 1;
    }
    size += design.memory_slots;
    return size;
}

[[nodiscard]] inline std::size_t compute_output_size(const ShipDesign& design) {
    return ACTION_COUNT + design.memory_slots;
}

/// Assign random unique IDs to any sensor with id == 0.
inline void assign_sensor_ids(ShipDesign& design) {
    // Collect existing IDs
    std::vector<uint16_t> existing;
    for (const auto& s : design.sensors) {
        if (s.id != 0) existing.push_back(s.id);
    }

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned int> dist(1, 65535);

    for (auto& s : design.sensors) {
        if (s.id != 0) continue;
        uint16_t candidate = 0;
        do {
            candidate = dist(rng);
        } while (candidate == 0 ||
                 std::find(existing.begin(), existing.end(), candidate) != existing.end());
        s.id = candidate;
        existing.push_back(candidate);
    }
}

} // namespace neuroflyer

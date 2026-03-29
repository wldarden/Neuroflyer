#pragma once

#include <neuroflyer/game.h>
#include <neuroflyer/base.h>
#include <neuroflyer/ship_design.h>

#include <cstddef>
#include <span>
#include <vector>

namespace neuroflyer {

enum class ArenaHitType {
    Nothing = 0,
    Tower,
    Token,
    FriendlyShip,
    EnemyShip,
    Bullet
};

struct ArenaQueryContext {
    float ship_x = 0, ship_y = 0;
    float ship_rotation = 0;        // ship's facing direction
    float world_w = 0, world_h = 0; // for position normalization
    std::size_t self_index = 0;
    int self_team = 0;

    // Entity references — caller must ensure lifetime
    std::span<const Tower> towers;
    std::span<const Token> tokens;
    std::span<const Triangle> ships;
    std::span<const int> ship_teams;   // parallel to ships
    std::span<const Bullet> bullets;
};

struct ArenaSensorReading {
    float distance = 1.0f;         // 0 = touching, 1 = nothing detected
    ArenaHitType entity_type = ArenaHitType::Nothing;
};

/// Query a single sensor against all arena entities.
[[nodiscard]] ArenaSensorReading query_arena_sensor(
    const SensorDef& sensor,
    const ArenaQueryContext& ctx);

/// Direction + range between two points.
struct DirRange {
    float dir_sin = 0, dir_cos = 0;
    float range = 1.0f;  // normalized to world diagonal
};

[[nodiscard]] DirRange compute_dir_range(
    float from_x, float from_y,
    float to_x, float to_y,
    float world_w, float world_h);

/// Compute arena fighter input size.
[[nodiscard]] std::size_t compute_arena_input_size(
    const ShipDesign& design,
    std::size_t broadcast_signal_count);

/// Build the complete arena fighter input vector.
/// Layout: [sensor values...] [pos/rotation] [nav inputs] [broadcast signals] [memory]
[[nodiscard]] std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float dir_to_target_sin, float dir_to_target_cos, float range_to_target,
    float dir_to_home_sin, float dir_to_home_cos, float range_to_home,
    float own_base_hp,
    std::span<const float> broadcast_signals,
    std::span<const float> memory);

} // namespace neuroflyer

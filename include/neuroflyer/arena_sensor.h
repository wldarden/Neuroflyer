#pragma once

#include <neuroflyer/entity_grid.h>
#include <neuroflyer/game.h>
#include <neuroflyer/base.h>
#include <neuroflyer/ship_design.h>

#include <cstddef>
#include <span>
#include <string>
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

    // Optional spatial acceleration — if set, sensor queries check only
    // nearby grid cells instead of iterating all entities.
    const EntityGrid* grid = nullptr;

    /// Build a context for querying sensors from a specific ship's perspective.
    [[nodiscard]] static ArenaQueryContext for_ship(
        const Triangle& ship, std::size_t index, int team,
        float world_w, float world_h,
        std::span<const Tower> towers,
        std::span<const Token> tokens,
        std::span<const Triangle> ships,
        std::span<const int> ship_teams,
        std::span<const Bullet> bullets);
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
[[nodiscard]] std::size_t compute_arena_input_size(const ShipDesign& design);

/// Build the complete arena fighter input vector.
/// Layout: [sensor values...] [squad leader inputs (6)] [memory]
[[nodiscard]] std::vector<float> build_arena_ship_input(
    const ShipDesign& design,
    const ArenaQueryContext& ctx,
    float squad_target_heading, float squad_target_distance,
    float squad_center_heading, float squad_center_distance,
    float aggression, float spacing,
    std::span<const float> memory);

/// Build an EntityGrid populated with all alive entities from the given spans.
/// Cell size is set to the max sensor range from the ShipDesign.
[[nodiscard]] EntityGrid build_sensor_grid(
    const ShipDesign& design,
    float world_w, float world_h,
    std::span<const Triangle> ships,
    std::span<const Tower> towers,
    std::span<const Token> tokens,
    std::span<const Bullet> bullets);

/// Build input labels for arena fighter nets.
[[nodiscard]] std::vector<std::string> build_arena_fighter_input_labels(const ShipDesign& design);

/// Build display order for arena fighter nets.
[[nodiscard]] std::vector<std::size_t> build_arena_fighter_display_order(const ShipDesign& design);

} // namespace neuroflyer

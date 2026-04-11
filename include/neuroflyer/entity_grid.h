#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neuroflyer {

enum class EntityType : uint8_t { Ship, Tower, Token, Bullet };

/// Lightweight spatial grid for accelerating sensor queries.
/// Entities are inserted by position + type + index. Queries return all
/// entities in grid cells overlapping a bounding box around a point.
/// Cell size should be set to the max sensor range so each query checks
/// at most a 3×3 neighborhood.
class EntityGrid {
public:
    EntityGrid(float world_w, float world_h, float cell_size);

    void clear();
    void insert(float x, float y, EntityType type, uint32_t index);

    /// Call fn(EntityType, uint32_t index) for each entity in cells
    /// overlapping the axis-aligned bounding box of `radius` around (cx, cy).
    template<typename F>
    void for_each_nearby(float cx, float cy, float radius, F&& fn) const {
        const int min_gx = std::max(0, cell_x(cx - radius));
        const int max_gx = std::min(cols_ - 1, cell_x(cx + radius));
        const int min_gy = std::max(0, cell_y(cy - radius));
        const int max_gy = std::min(rows_ - 1, cell_y(cy + radius));

        for (int gy = min_gy; gy <= max_gy; ++gy) {
            for (int gx = min_gx; gx <= max_gx; ++gx) {
                for (const auto& e : cells_[gy * cols_ + gx]) {
                    fn(e.type, e.index);
                }
            }
        }
    }

private:
    struct Entry { EntityType type; uint32_t index; };

    float cell_size_;
    int cols_, rows_;
    std::vector<std::vector<Entry>> cells_;

    int cell_x(float x) const {
        return std::clamp(static_cast<int>(x / cell_size_), 0, cols_ - 1);
    }
    int cell_y(float y) const {
        return std::clamp(static_cast<int>(y / cell_size_), 0, rows_ - 1);
    }
};

} // namespace neuroflyer

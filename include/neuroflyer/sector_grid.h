#pragma once

#include <cstddef>
#include <vector>

namespace neuroflyer {

struct SectorCoord {
    int row = 0;
    int col = 0;

    bool operator==(const SectorCoord& o) const noexcept {
        return row == o.row && col == o.col;
    }
};

/// 2D spatial index dividing the world into fixed-size sectors.
/// Each sector holds a list of entity indices.
class SectorGrid {
public:
    /// Construct a grid for a world of the given dimensions.
    SectorGrid(float world_width, float world_height, float sector_size);

    /// Which sector contains this world-space point.
    [[nodiscard]] SectorCoord sector_of(float x, float y) const noexcept;

    /// Insert an entity index into the sector containing (x, y).
    void insert(std::size_t entity_id, float x, float y);

    /// Clear all entities from all sectors.
    void clear();

    /// Gather all entity IDs within Manhattan distance <= radius sectors
    /// of the given center sector. Returns a flat vector of entity IDs.
    [[nodiscard]] std::vector<std::size_t> entities_in_diamond(
        SectorCoord center, int radius) const;

    /// Grid dimensions.
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int cols() const noexcept { return cols_; }
    [[nodiscard]] float sector_size() const noexcept { return sector_size_; }

private:
    [[nodiscard]] std::size_t grid_index(int row, int col) const noexcept;

    float sector_size_;
    int rows_;
    int cols_;
    // Flat grid: cells_[row * cols_ + col] = vector of entity IDs
    std::vector<std::vector<std::size_t>> cells_;
};

} // namespace neuroflyer

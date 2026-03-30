#include <neuroflyer/sector_grid.h>

#include <algorithm>
#include <cmath>

namespace neuroflyer {

SectorGrid::SectorGrid(float world_width, float world_height, float sector_size)
    : sector_size_(sector_size),
      rows_(std::max(1, static_cast<int>(std::ceil(world_height / sector_size)))),
      cols_(std::max(1, static_cast<int>(std::ceil(world_width / sector_size)))) {
    cells_.resize(static_cast<std::size_t>(rows_ * cols_));
}

SectorCoord SectorGrid::sector_of(float x, float y) const noexcept {
    int col = static_cast<int>(x / sector_size_);
    int row = static_cast<int>(y / sector_size_);
    col = std::clamp(col, 0, cols_ - 1);
    row = std::clamp(row, 0, rows_ - 1);
    return {row, col};
}

void SectorGrid::insert(std::size_t entity_id, float x, float y) {
    auto sc = sector_of(x, y);
    cells_[grid_index(sc.row, sc.col)].push_back(entity_id);
}

void SectorGrid::clear() {
    for (auto& cell : cells_) {
        cell.clear();
    }
}

std::vector<std::size_t> SectorGrid::entities_in_diamond(
    SectorCoord center, int radius) const {

    std::vector<std::size_t> result;
    for (int dr = -radius; dr <= radius; ++dr) {
        int max_dc = radius - std::abs(dr);
        for (int dc = -max_dc; dc <= max_dc; ++dc) {
            int r = center.row + dr;
            int c = center.col + dc;
            if (r < 0 || r >= rows_ || c < 0 || c >= cols_) continue;
            const auto& cell = cells_[grid_index(r, c)];
            result.insert(result.end(), cell.begin(), cell.end());
        }
    }
    return result;
}

std::size_t SectorGrid::grid_index(int row, int col) const noexcept {
    return static_cast<std::size_t>(row * cols_ + col);
}

} // namespace neuroflyer

#include <neuroflyer/entity_grid.h>

#include <algorithm>
#include <cmath>

namespace neuroflyer {

EntityGrid::EntityGrid(float world_w, float world_h, float cell_size)
    : cell_size_(std::max(cell_size, 1.0f))
    , cols_(std::max(1, static_cast<int>(std::ceil(world_w / cell_size_))))
    , rows_(std::max(1, static_cast<int>(std::ceil(world_h / cell_size_))))
    , cells_(static_cast<std::size_t>(cols_ * rows_)) {}

void EntityGrid::clear() {
    for (auto& cell : cells_) cell.clear();
}

void EntityGrid::insert(float x, float y, EntityType type, uint32_t index) {
    const int gx = cell_x(x);
    const int gy = cell_y(y);
    cells_[gy * cols_ + gx].push_back({type, index});
}

} // namespace neuroflyer

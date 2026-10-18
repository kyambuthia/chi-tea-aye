#pragma once

#include <cstdint>
#include <vector>

namespace world {

struct Box;

// A uniform grid over the ground plane that buckets static boxes by footprint, so
// collision and ray queries look at the few boxes nearby instead of all of them.
// Cells are stored flat (an offset table plus one index array) for cache-friendly reads.
//
// Queries reuse a per-grid visit stamp to report each box once, so a grid must not be
// queried from two threads at the same time.
class SpatialGrid {
public:
    // Buckets boxes over [-half_size, half_size] on X and Z. Boxes outside are clamped
    // into the border cells.
    void build(const std::vector<Box>& boxes, float half_size, float cell_size);

    // Appends (does not clear) the indices of boxes whose footprint overlaps the rectangle.
    void query(float min_x, float min_z, float max_x, float max_z, std::vector<int>& out) const;

    // Nearest box along the ray (unit direction) within max_distance, walking only the
    // cells the ray crosses (Amanatides-Woo traversal). Returns max_distance and sets
    // hit_index to -1 on a miss.
    float raycast(const std::vector<Box>& boxes, const float origin[3], const float direction[3], float max_distance,
        int* hit_index = nullptr) const;

    int cellsPerSide() const { return cells_; }
    float cellSize() const { return cell_size_; }

private:
    int cellOf(float v) const;

    float half_size_ = 1.0f;
    float cell_size_ = 1.0f;
    int cells_ = 1;
    std::vector<uint32_t> cell_start_;  // cells_ * cells_ + 1 offsets into items_
    std::vector<int> items_;
    mutable std::vector<uint32_t> stamp_;
    mutable uint32_t query_id_ = 0;
};

} // namespace world

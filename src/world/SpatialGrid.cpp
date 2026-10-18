#include "world/SpatialGrid.h"

#include "math/Geometry.h"
#include "world/Box.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace world {

int SpatialGrid::cellOf(float v) const {
    const int c = static_cast<int>(std::floor((v + half_size_) / cell_size_));
    return std::clamp(c, 0, cells_ - 1);
}

void SpatialGrid::build(const std::vector<Box>& boxes, float half_size, float cell_size) {
    half_size_ = half_size;
    cell_size_ = cell_size;
    cells_ = std::max(1, static_cast<int>(std::ceil(2.0f * half_size / cell_size)));
    const int cell_count = cells_ * cells_;

    // Two passes: count per cell, then fill. No per-cell allocations.
    std::vector<uint32_t> counts(cell_count, 0);
    for (const Box& b : boxes) {
        for (int z = cellOf(b.min[2]); z <= cellOf(b.max[2]); ++z) {
            for (int x = cellOf(b.min[0]); x <= cellOf(b.max[0]); ++x) {
                ++counts[z * cells_ + x];
            }
        }
    }
    cell_start_.assign(cell_count + 1, 0);
    for (int c = 0; c < cell_count; ++c) {
        cell_start_[c + 1] = cell_start_[c] + counts[c];
    }
    items_.assign(cell_start_[cell_count], 0);
    std::vector<uint32_t> fill(cell_start_.begin(), cell_start_.end() - 1);
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        const Box& b = boxes[i];
        for (int z = cellOf(b.min[2]); z <= cellOf(b.max[2]); ++z) {
            for (int x = cellOf(b.min[0]); x <= cellOf(b.max[0]); ++x) {
                items_[fill[z * cells_ + x]++] = i;
            }
        }
    }
    stamp_.assign(boxes.size(), 0);
    query_id_ = 0;
}

void SpatialGrid::query(float min_x, float min_z, float max_x, float max_z, std::vector<int>& out) const {
    if (stamp_.empty()) {
        return;
    }
    if (++query_id_ == 0) {  // wrapped: clear the stamps so old ones cannot match
        std::fill(stamp_.begin(), stamp_.end(), 0);
        query_id_ = 1;
    }
    for (int z = cellOf(min_z); z <= cellOf(max_z); ++z) {
        for (int x = cellOf(min_x); x <= cellOf(max_x); ++x) {
            const int c = z * cells_ + x;
            for (uint32_t k = cell_start_[c]; k < cell_start_[c + 1]; ++k) {
                const int i = items_[k];
                if (stamp_[i] != query_id_) {
                    stamp_[i] = query_id_;
                    out.push_back(i);
                }
            }
        }
    }
}

float SpatialGrid::raycast(const std::vector<Box>& boxes, const float origin[3], const float direction[3], float max_distance,
    int* hit_index) const {
    if (hit_index != nullptr) {
        *hit_index = -1;
    }
    if (stamp_.empty()) {
        return max_distance;
    }
    // Clip the ray to the grid's footprint.
    float t_enter = 0.0f;
    float t_exit = max_distance;
    for (int axis : {0, 2}) {
        if (std::fabs(direction[axis]) < 1e-9f) {
            if (origin[axis] < -half_size_ || origin[axis] > half_size_) {
                return max_distance;
            }
            continue;
        }
        float a = (-half_size_ - origin[axis]) / direction[axis];
        float b = (half_size_ - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        t_enter = std::max(t_enter, a);
        t_exit = std::min(t_exit, b);
    }
    if (t_enter > t_exit) {
        return max_distance;
    }

    if (++query_id_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        query_id_ = 1;
    }
    const float inf = std::numeric_limits<float>::infinity();
    const float px = origin[0] + direction[0] * t_enter;
    const float pz = origin[2] + direction[2] * t_enter;
    int cx = cellOf(px);
    int cz = cellOf(pz);
    const int step_x = direction[0] > 0.0f ? 1 : -1;
    const int step_z = direction[2] > 0.0f ? 1 : -1;
    const float delta_x = std::fabs(direction[0]) > 1e-9f ? cell_size_ / std::fabs(direction[0]) : inf;
    const float delta_z = std::fabs(direction[2]) > 1e-9f ? cell_size_ / std::fabs(direction[2]) : inf;
    auto boundary = [this](int cell, int step) { return -half_size_ + static_cast<float>(cell + (step > 0 ? 1 : 0)) * cell_size_; };
    float next_x = std::fabs(direction[0]) > 1e-9f ? (boundary(cx, step_x) - origin[0]) / direction[0] : inf;
    float next_z = std::fabs(direction[2]) > 1e-9f ? (boundary(cz, step_z) - origin[2]) / direction[2] : inf;

    float best = max_distance;
    int best_index = -1;
    for (;;) {
        const int c = cz * cells_ + cx;
        for (uint32_t k = cell_start_[c]; k < cell_start_[c + 1]; ++k) {
            const int i = items_[k];
            if (stamp_[i] == query_id_) {
                continue;
            }
            stamp_[i] = query_id_;
            const float d = math::rayBoxDistance(origin, direction, boxes[i].min, boxes[i].max);
            if (d >= 0.0f && d < best) {
                best = d;
                best_index = i;
            }
        }
        // Every box in a later cell is hit no sooner than that cell's entry.
        const float t_next = std::min(next_x, next_z);
        if (best <= t_next || t_next > t_exit) {
            break;
        }
        if (next_x < next_z) {
            cx += step_x;
            next_x += delta_x;
        } else {
            cz += step_z;
            next_z += delta_z;
        }
        if (cx < 0 || cx >= cells_ || cz < 0 || cz >= cells_) {
            break;
        }
    }
    if (hit_index != nullptr) {
        *hit_index = best_index;
    }
    return best;
}

} // namespace world

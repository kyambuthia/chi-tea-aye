#pragma once

#include <cmath>
#include <limits>

namespace math {

// Distance along a ray (unit direction) to an axis-aligned box, or -1 when it misses.
// A ray that starts inside the box reports zero. Slab test.
inline float rayBoxDistance(const float origin[3], const float direction[3], const float box_min[3], const float box_max[3]) {
    float t_near = 0.0f;
    float t_far = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) < 1e-9f) {
            if (origin[axis] < box_min[axis] || origin[axis] > box_max[axis]) {
                return -1.0f;
            }
            continue;
        }
        float t0 = (box_min[axis] - origin[axis]) / direction[axis];
        float t1 = (box_max[axis] - origin[axis]) / direction[axis];
        if (t0 > t1) {
            const float swap = t0;
            t0 = t1;
            t1 = swap;
        }
        t_near = t0 > t_near ? t0 : t_near;
        t_far = t1 < t_far ? t1 : t_far;
        if (t_near > t_far) {
            return -1.0f;
        }
    }
    return t_near;
}

// Overlap of a circle (centre cx, cz; radius r) with a box's ground footprint.
// On overlap, returns true with the unit push-out normal and the depth to move
// the circle along it. A centre inside the footprint pushes out the nearest side.
inline bool circleBoxPush(float cx, float cz, float r, const float box_min[3], const float box_max[3],
    float& normal_x, float& normal_z, float& depth) {
    const float nx = std::fmax(box_min[0], std::fmin(box_max[0], cx));
    const float nz = std::fmax(box_min[2], std::fmin(box_max[2], cz));
    const float dx = cx - nx;
    const float dz = cz - nz;
    const float d2 = dx * dx + dz * dz;
    if (d2 >= r * r) {
        return false;
    }
    if (d2 > 1e-10f) {
        const float d = std::sqrt(d2);
        normal_x = dx / d;
        normal_z = dz / d;
        depth = r - d;
        return true;
    }
    // Centre inside: leave through the closest face.
    const float faces[4] = {cx - box_min[0], box_max[0] - cx, cz - box_min[2], box_max[2] - cz};
    int best = 0;
    for (int i = 1; i < 4; ++i) {
        if (faces[i] < faces[best]) best = i;
    }
    normal_x = best == 0 ? -1.0f : (best == 1 ? 1.0f : 0.0f);
    normal_z = best == 2 ? -1.0f : (best == 3 ? 1.0f : 0.0f);
    depth = faces[best] + r;
    return true;
}

} // namespace math

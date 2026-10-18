#pragma once

#include "math/Mat4.h"

#include <cmath>

namespace math {

// The six planes of a view frustum, extracted from a view-projection matrix
// (Gribb and Hartmann, "Fast Extraction of Viewing Frustum Planes from the
// World-View-Projection Matrix"). Normals point inward and are unit length, so
// plane distances are in world units.
struct Frustum {
    float planes[6][4];  // a, b, c, d with a*x + b*y + c*z + d >= 0 inside

    static Frustum fromViewProjection(const Mat4& vp) {
        // Column-major: row r of the matrix is (m[r], m[4 + r], m[8 + r], m[12 + r]).
        auto row = [&vp](int r, int c) { return vp.m[c * 4 + r]; };
        Frustum f{};
        for (int i = 0; i < 6; ++i) {
            const int axis = i / 2;                 // x, y, z
            const float sign = (i % 2 == 0) ? 1.0f : -1.0f;  // left/bottom/near, then right/top/far
            float len = 0.0f;
            for (int c = 0; c < 4; ++c) {
                f.planes[i][c] = row(3, c) + sign * row(axis, c);
            }
            len = std::sqrt(f.planes[i][0] * f.planes[i][0] + f.planes[i][1] * f.planes[i][1] + f.planes[i][2] * f.planes[i][2]);
            if (len > 1e-12f) {
                for (float& c : f.planes[i]) c /= len;
            }
        }
        return f;
    }

    // False only when the box is entirely outside one plane. May keep a few boxes
    // that are just outside a corner, which is the usual, cheap trade.
    bool intersectsBox(const float lo[3], const float hi[3]) const {
        for (const auto& p : planes) {
            // The box corner farthest along the plane normal.
            const float x = p[0] >= 0.0f ? hi[0] : lo[0];
            const float y = p[1] >= 0.0f ? hi[1] : lo[1];
            const float z = p[2] >= 0.0f ? hi[2] : lo[2];
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) {
                return false;
            }
        }
        return true;
    }

    bool intersectsSphere(const float c[3], float radius) const {
        for (const auto& p : planes) {
            if (p[0] * c[0] + p[1] * c[1] + p[2] * c[2] + p[3] < -radius) {
                return false;
            }
        }
        return true;
    }
};

} // namespace math

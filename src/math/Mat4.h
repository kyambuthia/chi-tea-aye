#pragma once

#include <cmath>

// Column-major 4x4 matrices, matching OpenGL and glTF storage.
namespace math {

struct Mat4 {
    float m[16];
};

inline Mat4 identity() {
    return Mat4{{1.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 1.0f}};
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 out = identity();
    out.m[12] = x;
    out.m[13] = y;
    out.m[14] = z;
    return out;
}

// Rotation about the X axis.
inline Mat4 rotationX(float radians) {
    Mat4 out = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out.m[5] = c;
    out.m[9] = -s;
    out.m[6] = s;
    out.m[10] = c;
    return out;
}

// Rotation about the Z axis.
inline Mat4 rotationZ(float radians) {
    Mat4 out = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out.m[0] = c;
    out.m[4] = -s;
    out.m[1] = s;
    out.m[5] = c;
    return out;
}

// Rotation about the Y axis; +Z maps to (sin, 0, cos).
inline Mat4 rotationY(float radians) {
    Mat4 out = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

// Builds translation * rotation * scale from a glTF-style TRS node.
// Rotation is a quaternion stored as x, y, z, w.
inline Mat4 fromTrs(const float t[3], const float r[4], const float s[3]) {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    Mat4 out{};
    out.m[0] = (1.0f - 2.0f * (y * y + z * z)) * s[0];
    out.m[1] = (2.0f * (x * y + w * z)) * s[0];
    out.m[2] = (2.0f * (x * z - w * y)) * s[0];
    out.m[3] = 0.0f;
    out.m[4] = (2.0f * (x * y - w * z)) * s[1];
    out.m[5] = (1.0f - 2.0f * (x * x + z * z)) * s[1];
    out.m[6] = (2.0f * (y * z + w * x)) * s[1];
    out.m[7] = 0.0f;
    out.m[8] = (2.0f * (x * z + w * y)) * s[2];
    out.m[9] = (2.0f * (y * z - w * x)) * s[2];
    out.m[10] = (1.0f - 2.0f * (x * x + y * y)) * s[2];
    out.m[11] = 0.0f;
    out.m[12] = t[0];
    out.m[13] = t[1];
    out.m[14] = t[2];
    out.m[15] = 1.0f;
    return out;
}

inline Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
    const float f = 1.0f / std::tan(fovy_radians * 0.5f);
    Mat4 out{};
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (far_z + near_z) / (near_z - far_z);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
    return out;
}

inline Mat4 lookAt(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z) {
    float fx = target_x - eye_x;
    float fy = target_y - eye_y;
    float fz = target_z - eye_z;
    const float f_len = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
    fx /= f_len;
    fy /= f_len;
    fz /= f_len;

    float sx = -fz;
    float sy = 0.0f;
    float sz = fx;
    const float s_len = std::sqrt((sx * sx) + (sz * sz));
    sx /= s_len;
    sz /= s_len;

    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    Mat4 out = identity();
    out.m[0] = sx;
    out.m[4] = sy;
    out.m[8] = sz;
    out.m[1] = ux;
    out.m[5] = uy;
    out.m[9] = uz;
    out.m[2] = -fx;
    out.m[6] = -fy;
    out.m[10] = -fz;
    out.m[12] = -((sx * eye_x) + (sy * eye_y) + (sz * eye_z));
    out.m[13] = -((ux * eye_x) + (uy * eye_y) + (uz * eye_z));
    out.m[14] = (fx * eye_x) + (fy * eye_y) + (fz * eye_z);
    return out;
}

// Inverse of an affine transform (last row 0 0 0 1), such as a node or joint transform.
inline Mat4 affineInverse(const Mat4& a) {
    const float* m = a.m;
    // 3x3 part: columns are m[0..2], m[4..6], m[8..10].
    const float c00 = m[5] * m[10] - m[9] * m[6];
    const float c01 = m[9] * m[2] - m[1] * m[10];
    const float c02 = m[1] * m[6] - m[5] * m[2];
    const float c10 = m[8] * m[6] - m[4] * m[10];
    const float c11 = m[0] * m[10] - m[8] * m[2];
    const float c12 = m[4] * m[2] - m[0] * m[6];
    const float c20 = m[4] * m[9] - m[8] * m[5];
    const float c21 = m[8] * m[1] - m[0] * m[9];
    const float c22 = m[0] * m[5] - m[4] * m[1];
    const float det = m[0] * c00 + m[4] * c01 + m[8] * c02;
    const float inv_det = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;

    // Inverse 3x3 (column-major), i.e. transpose of the cofactor matrix over det.
    Mat4 out{};
    out.m[0] = c00 * inv_det;
    out.m[1] = c01 * inv_det;
    out.m[2] = c02 * inv_det;
    out.m[4] = c10 * inv_det;
    out.m[5] = c11 * inv_det;
    out.m[6] = c12 * inv_det;
    out.m[8] = c20 * inv_det;
    out.m[9] = c21 * inv_det;
    out.m[10] = c22 * inv_det;
    out.m[3] = out.m[7] = out.m[11] = 0.0f;
    out.m[15] = 1.0f;

    // Translation: -A^-1 * t.
    const float tx = m[12], ty = m[13], tz = m[14];
    out.m[12] = -(out.m[0] * tx + out.m[4] * ty + out.m[8] * tz);
    out.m[13] = -(out.m[1] * tx + out.m[5] * ty + out.m[9] * tz);
    out.m[14] = -(out.m[2] * tx + out.m[6] * ty + out.m[10] * tz);
    return out;
}

// Rotation taking unit vector from onto unit vector to, about the origin.
inline Mat4 rotationBetween(const float from[3], const float to[3]) {
    const float dot = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
    float axis[3] = {from[1] * to[2] - from[2] * to[1],
                     from[2] * to[0] - from[0] * to[2],
                     from[0] * to[1] - from[1] * to[0]};
    float q[4];
    if (dot < -0.999999f) {
        // Opposite vectors: turn half a revolution about any perpendicular axis.
        float perp[3] = {0.0f, 0.0f, 0.0f};
        perp[0] = std::fabs(from[0]) < 0.9f ? 1.0f : 0.0f;
        perp[1] = perp[0] == 0.0f ? 1.0f : 0.0f;
        float a[3] = {from[1] * perp[2] - from[2] * perp[1],
                      from[2] * perp[0] - from[0] * perp[2],
                      from[0] * perp[1] - from[1] * perp[0]};
        const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        q[0] = a[0] / len;
        q[1] = a[1] / len;
        q[2] = a[2] / len;
        q[3] = 0.0f;
    } else {
        q[0] = axis[0];
        q[1] = axis[1];
        q[2] = axis[2];
        q[3] = 1.0f + dot;
        const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (float& v : q) {
            v /= len;
        }
    }
    const float t[3] = {0.0f, 0.0f, 0.0f};
    const float unit[3] = {1.0f, 1.0f, 1.0f};
    return fromTrs(t, q, unit);
}

// Applies a rotation about a pivot point rather than the origin.
inline Mat4 rotateAbout(const Mat4& rotation, const float pivot[3]) {
    return multiply(translation(pivot[0], pivot[1], pivot[2]),
        multiply(rotation, translation(-pivot[0], -pivot[1], -pivot[2])));
}

} // namespace math

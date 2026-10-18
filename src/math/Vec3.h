#pragma once

#include "math/Mat4.h"

#include <cmath>

// Small value types for rigid-body maths. Plain structs, no hidden state.
namespace math {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3{0.0f, 0.0f, 0.0f};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// Unit quaternion (x, y, z, w) for orientation.
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

inline Quat operator*(Quat a, Quat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline Quat normalize(Quat q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-12f) return Quat{};
    const float s = 1.0f / l;
    return {q.x * s, q.y * s, q.z * s, q.w * s};
}

// Rotation of radians about +Y; +Z turns toward +X, matching math::rotationY.
inline Quat quatFromYaw(float radians) {
    return {0.0f, std::sin(0.5f * radians), 0.0f, std::cos(0.5f * radians)};
}

inline Quat quatFromAxisAngle(Vec3 axis, float radians) {
    const Vec3 a = normalize(axis);
    const float s = std::sin(0.5f * radians);
    return {a.x * s, a.y * s, a.z * s, std::cos(0.5f * radians)};
}

inline Vec3 rotate(Quat q, Vec3 v) {
    // v + 2w(u x v) + 2u x (u x v), with u the vector part.
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

inline Vec3 rotateInverse(Quat q, Vec3 v) {
    return rotate(Quat{-q.x, -q.y, -q.z, q.w}, v);
}

// Integrates an angular velocity (world frame, radians per second) over dt.
inline Quat integrate(Quat q, Vec3 omega, float dt) {
    const Quat spin{omega.x * 0.5f * dt, omega.y * 0.5f * dt, omega.z * 0.5f * dt, 0.0f};
    const Quat d = spin * q;
    return normalize(Quat{q.x + d.x, q.y + d.y, q.z + d.z, q.w + d.w});
}

// Normalised lerp along the shorter arc: smooth enough between close physics steps.
inline Quat nlerp(Quat a, Quat b, float t) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0.0f ? -1.0f : 1.0f;
    return normalize(Quat{a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t,
        a.w + (s * b.w - a.w) * t});
}

// Rigid transform: rotation q, then translation p.
inline Mat4 toMat4(Quat q, Vec3 p) {
    const float t[3] = {p.x, p.y, p.z};
    const float r[4] = {q.x, q.y, q.z, q.w};
    const float s[3] = {1.0f, 1.0f, 1.0f};
    return fromTrs(t, r, s);
}

} // namespace math

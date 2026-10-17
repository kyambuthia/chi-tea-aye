#pragma once

#include "math/Mat4.h"

#include <cmath>

namespace gameplay {

// Where a weapon sits relative to the hand: the prop's grip is placed at the
// hand, with the grip offset applied in the hand's frame.
//
//   model = handWorld * gripOffset
//
// handWorld is the posed hand's world transform (skin matrix times bind pose).
inline math::Mat4 makeWeaponModelTransform(const math::Mat4& hand_world, const math::Mat4& grip_offset) {
    return math::multiply(hand_world, grip_offset);
}

// A held rifle, aimed along a direction. Its grip point (in prop space) sits at
// grip_world, and its muzzle (the prop's -Z end) points along aim, a unit vector.
//
//   model = translate(grip_world) * [X Y Z] * translate(-grip_local)
//
// [X Y Z] is a proper rotation: Z is -aim, X is level and across the rifle, and
// Y completes the frame. (The mesh is not mirrored.)
inline math::Mat4 makeAimedWeaponTransform(const float grip_world[3], const float aim[3], const float grip_local[3]) {
    // Z: the mesh's +Z points back along the aim, toward the stock.
    float zx = -aim[0], zy = -aim[1], zz = -aim[2];
    const float zlen = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zlen > 1e-6f) {
        zx /= zlen;
        zy /= zlen;
        zz /= zlen;
    } else {
        zz = 1.0f;
    }
    // X: across the rifle, level. Aimed straight up or down, use +X as the reference.
    // cross(up, Z) with up = (0, 1, 0) is (Z.z, 0, -Z.x).
    float xx = zz;
    float xy = 0.0f;
    float xz = -zx;
    float xlen = std::sqrt(xx * xx + xy * xy + xz * xz);
    if (xlen < 1e-6f) {
        xx = 1.0f;
        xy = 0.0f;
        xz = 0.0f;
        xlen = 1.0f;
    }
    xx /= xlen;
    xy /= xlen;
    xz /= xlen;
    // Y: cross(Z, X), so the basis is right-handed.
    const float yx = zy * xz - zz * xy;
    const float yy = zz * xx - zx * xz;
    const float yz = zx * xy - zy * xx;

    math::Mat4 basis = math::identity();
    basis.m[0] = xx; basis.m[1] = xy; basis.m[2] = xz;   // column X
    basis.m[4] = yx; basis.m[5] = yy; basis.m[6] = yz;   // column Y
    basis.m[8] = zx; basis.m[9] = zy; basis.m[10] = zz;  // column Z
    const math::Mat4 to_grip = math::translation(-grip_local[0], -grip_local[1], -grip_local[2]);
    return math::multiply(math::translation(grip_world[0], grip_world[1], grip_world[2]), math::multiply(basis, to_grip));
}

} // namespace gameplay

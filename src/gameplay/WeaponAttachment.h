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

// A held rifle: its grip point (in prop space) sits at grip_world, and its barrel
// (+Z) points along the forward direction on the ground. The prop's Y stays up.
//
//   model = translate(grip_world) * [X Y Z] * translate(-grip_local)
inline math::Mat4 makeHeldWeaponTransform(const float grip_world[3], const float forward[3], const float grip_local[3]) {
    // Z: forward, flattened to the ground.
    float zx = forward[0], zz = forward[2];
    const float zlen = std::sqrt(zx * zx + zz * zz);
    if (zlen > 1e-6f) {
        zx /= zlen;
        zz /= zlen;
    } else {
        zx = 0.0f;
        zz = 1.0f;
    }
    // X: right of the barrel, cross(up, Z).
    const float xx = -zz, xz = zx;
    // Y: cross(Z, X) is up for a flat forward.
    math::Mat4 basis = math::identity();
    basis.m[0] = xx;   basis.m[1] = 0.0f; basis.m[2] = xz;   // column X
    basis.m[4] = 0.0f; basis.m[5] = 1.0f; basis.m[6] = 0.0f; // column Y
    basis.m[8] = zx;   basis.m[9] = 0.0f; basis.m[10] = zz;  // column Z
    const math::Mat4 to_grip = math::translation(-grip_local[0], -grip_local[1], -grip_local[2]);
    return math::multiply(math::translation(grip_world[0], grip_world[1], grip_world[2]), math::multiply(basis, to_grip));
}

} // namespace gameplay

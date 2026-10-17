#pragma once

#include "math/Mat4.h"

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

} // namespace gameplay

#include "physics/Physics.h"

#include "app/InputState.h"

#include <cmath>

namespace physics {

void Physics::init() {}

void Physics::inputDirection(const app::InputState& input, float camera_azimuth, float& dir_x, float& dir_z) const {
    // Camera forward on the ground: from the camera toward the player.
    const float forward_x = -std::sin(camera_azimuth);
    const float forward_z = -std::cos(camera_azimuth);
    // Right is forward rotated a quarter turn clockwise seen from above.
    const float right_x = -forward_z;
    const float right_z = forward_x;

    const float input_x = (input.keys.d ? 1.0f : 0.0f) - (input.keys.a ? 1.0f : 0.0f);
    const float input_z = (input.keys.w ? 1.0f : 0.0f) - (input.keys.s ? 1.0f : 0.0f);

    dir_x = input_x * right_x + input_z * forward_x;
    dir_z = input_x * right_z + input_z * forward_z;
    const float length = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    if (length > 1.0f) {
        dir_x /= length;
        dir_z /= length;
    }
}

void Physics::integratePlayer(float dir_x, float dir_z, float speed, float delta_time, float& x, float& z) const {
    x += dir_x * speed * delta_time;
    z += dir_z * speed * delta_time;
}

void Physics::cleanup() {}

} // namespace physics

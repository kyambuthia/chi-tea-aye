#pragma once

namespace app {
struct InputState;
}

namespace world {
class World;
}

namespace physics {

class Physics {
public:
    void init();

    // Ground direction from the input, relative to the camera: W moves away
    // from it, S toward it, and A and D strafe. Unit length, or zero for no input.
    void inputDirection(const app::InputState& input, float camera_azimuth, float& dir_x, float& dir_z) const;

    // Moves the player along a direction at a speed in meters per second.
    void integratePlayer(float dir_x, float dir_z, float speed, float delta_time, float& x, float& z) const;

    void cleanup();
};

} // namespace physics

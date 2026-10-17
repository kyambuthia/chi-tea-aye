#pragma once

namespace app {
struct InputState;
}

namespace physics {
class Physics;
}

namespace world {
class World;
}

namespace gameplay {

class GameState {
public:
    void init();
    void update(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time);
    void cleanup();

    float playerX() const;
    float playerZ() const;
    // Heading in radians about +Y; zero faces +Z.
    float playerYaw() const;
    // Name of the animation clip the player is in: "Idle", "Walk", or "Run".
    const char* clipName() const;
    // Seconds since the current clip started.
    float clipTime() const;

    // Third-person camera: azimuth is the direction from the player to the
    // camera, behind the player's heading plus the player's orbit offset.
    float cameraAzimuth() const;
    float cameraPitch() const;
    float cameraDistance() const;

private:
    float player_x_ = 0.0f;
    float player_z_ = 0.0f;
    float yaw_ = 0.0f;
    float clip_time_ = 0.0f;
    enum class Clip { Idle, Walk, Run };
    Clip clip_ = Clip::Idle;
    float orbit_yaw_ = 0.0f;
    float camera_pitch_ = 0.3f;
    float camera_distance_ = 3.2f;
};

} // namespace gameplay

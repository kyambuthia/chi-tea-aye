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
    // Name of the animation clip the player is in: "Idle", "Walk", "Run",
    // "WalkBackwards", "StrafeLeft", or "StrafeRight".
    const char* clipName() const;
    // Seconds since the current clip started.
    float clipTime() const;

    // Third-person camera, already smoothed. The eye sits at the target plus
    // distance along the azimuth, raised by the pitch.
    float cameraTargetX() const;
    float cameraTargetZ() const;
    float cameraAzimuth() const;
    float cameraPitch() const;
    float cameraDistance() const;

private:
    enum class Clip { Idle, Walk, Run, Backward, StrafeLeft, StrafeRight };

    float player_x_ = 0.0f;
    float player_z_ = 0.0f;
    float velocity_x_ = 0.0f;
    float velocity_z_ = 0.0f;
    float yaw_ = 0.0f;
    float clip_time_ = 0.0f;
    Clip clip_ = Clip::Idle;

    // The camera's goal azimuth. Orbit input moves it directly; it recenters
    // behind the player only slowly, and only when the player is idle.
    float camera_goal_azimuth_ = 3.14159265358979323846f;
    float idle_orbit_time_ = 0.0f;
    float pitch_target_ = 0.3f;
    float distance_target_ = 3.2f;

    // Smoothed camera state that follows the targets above, with the spring
    // velocities that smoothing carries from frame to frame.
    float camera_azimuth_ = 3.14159265358979323846f;
    float camera_azimuth_speed_ = 0.0f;
    float camera_pitch_ = 0.3f;
    float camera_distance_ = 3.2f;
    float camera_target_x_ = 0.0f;
    float camera_target_z_ = 0.0f;
    float camera_target_x_speed_ = 0.0f;
    float camera_target_z_speed_ = 0.0f;
};

} // namespace gameplay

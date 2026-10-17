#pragma once

#include "gameplay/Targets.h"
#include "gameplay/WeaponState.h"

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

    // Puts the shooting targets on the terrain. Call once the world is set up.
    void placeTargets(const world::World& world);

    // Off: the camera keeps the angle the player left it at, for inspecting the model.
    void setAutoRecenter(bool enabled);

    float playerX() const;
    float playerZ() const;
    // Heading in radians about +Y; zero faces +Z.
    float playerYaw() const;
    // Name of the animation clip the player is in: "Idle", "Walk", "Run", "CrouchIdle", or "CrouchWalk".
    const char* clipName() const;
    // Seconds since the current clip started.
    float clipTime() const;

    // Equipped weapon, switching, fist attack, and aim.
    const WeaponState& weapon() const;

    // The shooting range.
    const Targets& targets() const;

    // The latest shot: where it ended, and whether it hit a target. age is the
    // seconds since it was fired (large before the first shot).
    struct ShotEvent {
        float age = 1e9f;
        bool hit = false;
        float impact[3] = {0.0f, 0.0f, 0.0f};
    };
    const ShotEvent& lastShot() const;

    // The camera looks at this height standing, and lower when crouched.
    static constexpr float kStandLookHeight = 1.1f;
    static constexpr float kCrouchLookHeight = 0.7f;
    float cameraLookHeight() const;

    // The camera distance, pulled in so a wall between the warrior and the camera
    // does not hide the view behind the camera. Pass the distance the camera wants.
    float clearCameraDistance(float wanted_distance) const;

    bool crouched() const;

    // The ray from the camera through the centre of the view: where a shot goes.
    Ray cameraRay() const;

    // The camera's eye and look point. Orbiting, the eye is behind the warrior. Aimed,
    // the eye moves to the right shoulder and looks down the rifle's aim line.
    void cameraView(float eye[3], float look[3]) const;

    // Third-person camera, already smoothed. The eye sits at the target plus
    // distance along the azimuth, raised by the pitch.
    float cameraTargetX() const;
    float cameraTargetZ() const;
    float cameraAzimuth() const;
    // Pitch, including the recoil kick from the latest shots.
    float cameraPitch() const;
    float cameraDistance() const;

private:
    enum class Clip { Idle, Walk, Run, CrouchIdle, CrouchWalk };
    // Nearest wall the ray hits, or a distance beyond any range when it hits none.
    float nearestWallDistance(const Ray& ray) const;

    float player_x_ = 0.0f;
    float player_z_ = 0.0f;
    float velocity_x_ = 0.0f;
    float velocity_z_ = 0.0f;
    float yaw_ = 0.0f;
    float clip_time_ = 0.0f;
    bool crouched_ = false;
    float fire_buffer_ = 0.0f;  // seconds a press stays ready for the next shot
    float recoil_kick_ = 0.0f;  // radians the camera is kicked up, decaying after each shot
    ShotEvent last_shot_;
    float look_height_ = 1.1f;  // smoothed: the camera's look height
    Clip clip_ = Clip::Idle;
    // W, A, S, D: seconds left to keep a tapped key active.
    float tap_hold_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // The camera's goal azimuth. Orbit input moves it directly; it recenters
    // behind the player only slowly, and only when the player is idle.
    float camera_goal_azimuth_ = 3.14159265358979323846f;
    float idle_orbit_time_ = 0.0f;
    bool auto_recenter_ = true;
    float pitch_target_ = 0.18f;  // looks slightly down, so centre shots clear the low wall
    float distance_target_ = 3.2f;

    // Smoothed camera state that follows the targets above, with the spring
    // velocities that smoothing carries from frame to frame.
    float camera_azimuth_ = 3.14159265358979323846f;
    float camera_azimuth_speed_ = 0.0f;
    float camera_pitch_ = 0.18f;
    float camera_distance_ = 3.2f;
    float camera_target_x_ = 0.0f;
    float camera_target_z_ = 0.0f;
    float camera_target_x_speed_ = 0.0f;
    float camera_target_z_speed_ = 0.0f;

    WeaponState weapon_;
    Targets targets_;
    const world::World* world_ = nullptr;  // for its walls; set by placeTargets
};

} // namespace gameplay

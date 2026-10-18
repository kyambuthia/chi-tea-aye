#pragma once

#include "assets/CharacterModel.h"
#include "gameplay/Targets.h"
#include "gameplay/WeaponState.h"
#include "math/Mat4.h"
#include "physics/Vehicle.h"

#include <vector>

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

    // The warrior is an independent NPC. R respawns its ragdoll at the practice spot.
    bool spawnWarrior(physics::Physics& physics, const world::World& world, const assets::CharacterModel& character);
    bool warriorRagdollActive() const { return warrior_ragdoll_active_; }
    const math::Mat4* warriorSkinPose() const { return warrior_ragdoll_active_ ? warrior_pose_ : nullptr; }

    // Puts the shooting targets on the terrain. Call once the world is set up.
    void placeTargets(const world::World& world);
    // Parks a car at each of the city's parking spots.
    void spawnVehicles(const world::World& world);

    // Off: the camera keeps the angle the player left it at, for inspecting the model.
    void setAutoRecenter(bool enabled);

    // Moves the player (on foot) to a ground position and heading, at rest.
    void setPlayerPosition(float x, float z, float yaw);

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

    // Cars. The one being driven is vehicles()[drivenVehicle()].
    const std::vector<physics::Vehicle>& vehicles() const { return vehicles_; }
    bool driving() const { return driven_ >= 0; }
    int drivenVehicle() const { return driven_; }
    // The car close enough to get into, or -1.
    int vehicleInReach() const;
    static constexpr float kEnterReach = 1.4f;    // metres from the car's body
    static constexpr float kExitMaxSpeed = 7.0f;  // m/s: faster than this, F does not bail out

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

    // The camera's eye and look point. Aimed, it stays in third person over the
    // right shoulder and looks down the rifle's aim line.
    void cameraView(float eye[3], float look[3]) const;

    // cameraView with the camera shake added. For drawing only: shots and aim use the
    // steady view, so shake never moves where a bullet goes.
    void renderCameraView(float eye[3], float look[3]) const;

    // Vertical field of view in radians: held wide while aiming, wider at speed in a car.
    float cameraFov() const;

    // Camera trauma, 0..1: added by shots and crashes, decaying over time. The visible
    // shake is its square, so small knocks barely show (Eiserloh, GDC 2016).
    float trauma() const { return trauma_; }

    // The rifle's grip while aimed: a point on the aim line in front of the right
    // shoulder. The rifle is placed on it, and the eye sits behind it.
    void sightGrip(float out[3]) const;

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
    // Nearest wall or car the ray hits, or a distance beyond any range when it hits none.
    // skip_vehicle is a car to ignore (the one being driven, for the camera).
    float nearestWallDistance(const Ray& ray, int skip_vehicle = -1, int* hit_vehicle = nullptr) const;

    void updateOnFoot(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time,
        const app::InputState& moves);
    void updateVehicles(const app::InputState& input, const world::World& world, float delta_time);
    void collideVehiclePair(physics::Vehicle& a, physics::Vehicle& b);
    void enterOrExit(const world::World& world);
    void addTrauma(float amount);

    float player_x_ = 0.0f;
    float player_z_ = 0.0f;
    float velocity_x_ = 0.0f;
    float velocity_z_ = 0.0f;
    float yaw_ = 0.0f;
    float clip_time_ = 0.0f;
    bool crouched_ = false;
    const assets::CharacterModel* warrior_character_ = nullptr;
    bool warrior_ragdoll_active_ = false;
    math::Mat4 warrior_pose_[assets::kMaxJoints]{};
    bool inspecting_ = false;  // F3: the close camera for checking the model
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
    float car_distance_target_ = 6.5f;

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
    float fov_boost_ = 0.0f;   // smoothed extra field of view at speed (radians)
    float trauma_ = 0.0f;
    float shake_time_ = 0.0f;  // drives the shake noise

    WeaponState weapon_;
    Targets targets_;
    std::vector<physics::Vehicle> vehicles_;
    int driven_ = -1;
    const world::World* world_ = nullptr;  // for its walls; set by placeTargets and update
};

} // namespace gameplay

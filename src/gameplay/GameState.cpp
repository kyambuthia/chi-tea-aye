#include "gameplay/GameState.h"

#include "app/InputState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <cmath>

namespace gameplay {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Movement modes, chosen by the input direction relative to the facing.
// Speeds are the planted-foot speeds measured from each clip, so feet do not slide.
constexpr float kForwardCone = 60.0f * kPi / 180.0f;   // within this: walk or run, turn to face it
constexpr float kStrafeCone = 120.0f * kPi / 180.0f;   // within this: strafe, facing kept
constexpr float kWalkSpeed = 1.65f;
constexpr float kRunSpeed = 4.0f;
constexpr float kStrafeSpeed = 1.8f;
constexpr float kBackwardSpeed = 1.08f;

// The character turns at a fixed angular speed, so a heading can never jump.
constexpr float kTurnSpeed = 9.0f;             // radians per second (about 515 degrees)

// The camera eases toward its target: exponential smoothing with this rate
// (per second), so changes take about 1/rate seconds to settle.
constexpr float kCameraFollowRate = 4.0f;
constexpr float kCameraTargetRate = 10.0f;
constexpr float kCameraZoomRate = 8.0f;

// After the player stops orbiting, the camera recenters behind the character.
constexpr float kRecenterDelay = 1.5f;         // seconds without orbit input
constexpr float kRecenterSpeed = 1.2f;         // radians per second

constexpr float kOrbitSpeed = 0.006f;          // radians per pixel of drag
constexpr float kMinPitch = -0.15f;
constexpr float kMaxPitch = 1.1f;
constexpr float kMinDistance = 1.5f;
constexpr float kMaxDistance = 6.0f;

// Signed angle from one heading to another, in (-pi, pi].
float angleBetween(float from, float to) {
    float diff = std::fmod(to - from + kPi, 2.0f * kPi);
    if (diff < 0.0f) {
        diff += 2.0f * kPi;
    }
    return diff - kPi;
}

// Turns a heading toward a target by at most max_step.
float turnToward(float current, float target, float max_step) {
    float diff = angleBetween(current, target);
    if (diff > max_step) {
        diff = max_step;
    } else if (diff < -max_step) {
        diff = -max_step;
    }
    return current + diff;
}

// Fraction of the remaining distance covered this frame by exponential smoothing.
float smoothingFactor(float rate, float delta_time) {
    return 1.0f - std::exp(-rate * delta_time);
}

float approach(float current, float target, float factor) {
    return current + (target - current) * factor;
}

} // namespace

void GameState::init() {}

void GameState::update(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time) {
    (void)world;

    // Orbit input sets the target offset; without input, recenter after a pause.
    const bool orbiting = input.look_dx != 0.0f || input.look_dy != 0.0f;
    if (orbiting) {
        orbit_yaw_ -= input.look_dx * kOrbitSpeed;
        pitch_target_ = std::fmin(kMaxPitch, std::fmax(kMinPitch, pitch_target_ + input.look_dy * kOrbitSpeed));
        idle_orbit_time_ = 0.0f;
    } else {
        idle_orbit_time_ += delta_time;
    }
    distance_target_ = std::fmin(kMaxDistance, std::fmax(kMinDistance, distance_target_ - input.zoom_steps * 0.3f));

    // The camera heads for "behind the character" plus the player's offset. It is
    // updated before moving, so movement is relative to the view on screen.
    const float camera_goal = yaw_ + kPi + orbit_yaw_;
    camera_azimuth_ = turnToward(camera_azimuth_, camera_goal,
        smoothingFactor(kCameraFollowRate, delta_time) * std::fabs(angleBetween(camera_azimuth_, camera_goal)));

    float dir_x = 0.0f;
    float dir_z = 0.0f;
    physics.inputDirection(input, camera_azimuth_, dir_x, dir_z);
    const bool has_input = dir_x != 0.0f || dir_z != 0.0f;

    Clip next = Clip::Idle;
    float speed = 0.0f;
    if (has_input) {
        // Where the input points, relative to where the warrior faces.
        const float relative = angleBetween(yaw_, std::atan2(dir_x, dir_z));
        if (std::fabs(relative) <= kForwardCone) {
            // Forward: turn to face the input and walk or run.
            const bool running = input.keys.shift;
            next = running ? Clip::Run : Clip::Walk;
            speed = running ? kRunSpeed : kWalkSpeed;
            yaw_ = turnToward(yaw_, std::atan2(dir_x, dir_z), kTurnSpeed * delta_time);
        } else if (std::fabs(relative) <= kStrafeCone) {
            // Sideways: keep facing and strafe. Positive is the warrior's left.
            next = relative > 0.0f ? Clip::StrafeLeft : Clip::StrafeRight;
            speed = kStrafeSpeed;
        } else {
            // Backward: keep facing and walk backward.
            next = Clip::Backward;
            speed = kBackwardSpeed;
        }
    }

    physics.integratePlayer(dir_x, dir_z, speed, delta_time, player_x_, player_z_);

    if (next != clip_) {
        clip_ = next;
        clip_time_ = 0.0f;
    }
    clip_time_ += delta_time;

    if (has_input && idle_orbit_time_ > kRecenterDelay) {
        const float step = kRecenterSpeed * delta_time;
        if (orbit_yaw_ > step) {
            orbit_yaw_ -= step;
        } else if (orbit_yaw_ < -step) {
            orbit_yaw_ += step;
        } else {
            orbit_yaw_ = 0.0f;
        }
    }

    const float zoom_factor = smoothingFactor(kCameraZoomRate, delta_time);
    camera_pitch_ = approach(camera_pitch_, pitch_target_, zoom_factor);
    camera_distance_ = approach(camera_distance_, distance_target_, zoom_factor);

    const float target_factor = smoothingFactor(kCameraTargetRate, delta_time);
    camera_target_x_ = approach(camera_target_x_, player_x_, target_factor);
    camera_target_z_ = approach(camera_target_z_, player_z_, target_factor);
}

void GameState::cleanup() {}

float GameState::playerX() const {
    return player_x_;
}

float GameState::playerZ() const {
    return player_z_;
}

float GameState::playerYaw() const {
    return yaw_;
}

const char* GameState::clipName() const {
    switch (clip_) {
        case Clip::Walk:
            return "Walk";
        case Clip::Run:
            return "Run";
        case Clip::Backward:
            return "WalkBackwards";
        case Clip::StrafeLeft:
            return "StrafeLeft";
        case Clip::StrafeRight:
            return "StrafeRight";
        case Clip::Idle:
        default:
            return "Idle";
    }
}

float GameState::clipTime() const {
    return clip_time_;
}

float GameState::cameraTargetX() const {
    return camera_target_x_;
}

float GameState::cameraTargetZ() const {
    return camera_target_z_;
}

float GameState::cameraAzimuth() const {
    return camera_azimuth_;
}

float GameState::cameraPitch() const {
    return camera_pitch_;
}

float GameState::cameraDistance() const {
    return camera_distance_;
}

} // namespace gameplay

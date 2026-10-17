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

// Velocity ramps toward the desired velocity: accelerating is slower than braking.
constexpr float kAcceleration = 6.0f;          // meters per second squared
constexpr float kDeceleration = 9.0f;
constexpr float kStopSpeed = 0.05f;

// Facing turns faster for bigger angles: from the minimum rate for a small
// correction up to the maximum for a turn at the edge of the forward cone.
constexpr float kTurnRateSmall = 6.0f;         // radians per second
constexpr float kTurnRateLarge = 12.0f;

// Camera spring times: how long the camera takes to settle (seconds).
constexpr float kCameraAzimuthTime = 0.30f;
constexpr float kCameraTargetTime = 0.12f;
constexpr float kLookAheadTime = 0.35f;        // the camera leads the player's velocity
constexpr float kLookAheadMax = 1.0f;          // meters

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

// Critically damped spring toward target (Unity's SmoothDamp). Keeps velocity
// between frames, so it settles without overshoot and with no jumps.
float smoothDamp(float current, float target, float& velocity, float smooth_time, float delta_time) {
    const float omega = 2.0f / smooth_time;
    const float x = omega * delta_time;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (velocity + omega * change) * delta_time;
    velocity = (velocity - omega * temp) * decay;
    return target + (change + temp) * decay;
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

    // Orbit input moves the camera's goal directly. The camera does not follow
    // the warrior's heading while it moves, or the view would chase the turn
    // and spin: the input is read against the view, which turns the warrior.
    const bool orbiting = input.look_dx != 0.0f || input.look_dy != 0.0f;
    if (orbiting) {
        camera_goal_azimuth_ -= input.look_dx * kOrbitSpeed;
        pitch_target_ = std::fmin(kMaxPitch, std::fmax(kMinPitch, pitch_target_ + input.look_dy * kOrbitSpeed));
        idle_orbit_time_ = 0.0f;
    } else {
        idle_orbit_time_ += delta_time;
    }
    distance_target_ = std::fmin(kMaxDistance, std::fmax(kMinDistance, distance_target_ - input.zoom_steps * 0.3f));

    // Recenter slowly behind the warrior, only once the player has stopped
    // orbiting and stopped moving. Never while a movement key is held.
    const bool has_movement_key = input.keys.w || input.keys.a || input.keys.s || input.keys.d;
    if (!has_movement_key && idle_orbit_time_ > kRecenterDelay) {
        const float behind = yaw_ + kPi;
        const float offset = angleBetween(camera_goal_azimuth_, behind);
        const float step = kRecenterSpeed * delta_time;
        camera_goal_azimuth_ += std::fabs(offset) < step ? offset : (offset > 0.0f ? step : -step);
    }

    // The camera springs toward its goal. Input is read against that view.
    const float camera_goal_unwrapped = camera_azimuth_ + angleBetween(camera_azimuth_, camera_goal_azimuth_);
    camera_azimuth_ = smoothDamp(camera_azimuth_, camera_goal_unwrapped, camera_azimuth_speed_, kCameraAzimuthTime, delta_time);

    // Weapons: switch, punch, and aim. These are independent of locomotion.
    if (input.weapon_request >= 0) {
        weapon_.request(static_cast<WeaponSlot>(input.weapon_request));
    }
    if (input.attack_pressed) {
        weapon_.attack();
    }
    if (input.fire_pressed) {
        weapon_.fire();
    }
    weapon_.setAiming(input.aim_held);
    weapon_.update(delta_time);

    float dir_x = 0.0f;
    float dir_z = 0.0f;
    physics.inputDirection(input, camera_azimuth_, dir_x, dir_z);
    const bool has_input = dir_x != 0.0f || dir_z != 0.0f;

    // Pick the movement mode from where the input points relative to the facing.
    float desired_x = 0.0f;
    float desired_z = 0.0f;
    Clip next = Clip::Walk;
    if (has_input) {
        const float input_heading = std::atan2(dir_x, dir_z);
        const float relative = angleBetween(yaw_, input_heading);
        if (std::fabs(relative) <= kForwardCone) {
            // Forward: turn to face the input and walk or run. Bigger turns turn faster.
            const bool running = input.keys.shift;
            next = running ? Clip::Run : Clip::Walk;
            const float speed = running ? kRunSpeed : kWalkSpeed;
            desired_x = dir_x * speed;
            desired_z = dir_z * speed;
            const float turn_fraction = std::fabs(relative) / kForwardCone;
            const float turn_rate = kTurnRateSmall + (kTurnRateLarge - kTurnRateSmall) * turn_fraction;
            yaw_ = turnToward(yaw_, input_heading, turn_rate * delta_time);
        } else if (std::fabs(relative) <= kStrafeCone) {
            // Sideways: keep facing and strafe. Positive is the warrior's left.
            next = relative > 0.0f ? Clip::StrafeLeft : Clip::StrafeRight;
            desired_x = dir_x * kStrafeSpeed;
            desired_z = dir_z * kStrafeSpeed;
        } else {
            // Backward: keep facing and walk backward.
            next = Clip::Backward;
            desired_x = dir_x * kBackwardSpeed;
            desired_z = dir_z * kBackwardSpeed;
        }
    }

    // Ramp the velocity toward the desired one.
    float step_x = desired_x - velocity_x_;
    float step_z = desired_z - velocity_z_;
    const float speed_now = std::sqrt(velocity_x_ * velocity_x_ + velocity_z_ * velocity_z_);
    const float speed_wanted = std::sqrt(desired_x * desired_x + desired_z * desired_z);
    const float rate = speed_wanted > speed_now ? kAcceleration : kDeceleration;
    const float max_step = rate * delta_time;
    const float step_length = std::sqrt(step_x * step_x + step_z * step_z);
    if (step_length > max_step && step_length > 0.0f) {
        step_x *= max_step / step_length;
        step_z *= max_step / step_length;
    }
    velocity_x_ += step_x;
    velocity_z_ += step_z;

    physics.integratePlayer(velocity_x_, velocity_z_, 1.0f, delta_time, player_x_, player_z_);

    const bool moving = std::sqrt(velocity_x_ * velocity_x_ + velocity_z_ * velocity_z_) > kStopSpeed;
    Clip clip = Clip::Idle;
    if (has_input) {
        clip = next;
    } else if (moving) {
        clip = Clip::Walk;  // slowing down after input: keep walking until stopped
    }
    if (clip != clip_) {
        clip_ = clip;
        clip_time_ = 0.0f;
    }
    clip_time_ += delta_time;

    // The camera's target leads the player along its velocity, and springs toward it.
    float look_x = velocity_x_ * kLookAheadTime;
    float look_z = velocity_z_ * kLookAheadTime;
    const float look_length = std::sqrt(look_x * look_x + look_z * look_z);
    if (look_length > kLookAheadMax) {
        look_x *= kLookAheadMax / look_length;
        look_z *= kLookAheadMax / look_length;
    }
    camera_target_x_ = smoothDamp(camera_target_x_, player_x_ + look_x, camera_target_x_speed_, kCameraTargetTime, delta_time);
    camera_target_z_ = smoothDamp(camera_target_z_, player_z_ + look_z, camera_target_z_speed_, kCameraTargetTime, delta_time);

    const float zoom_factor = smoothingFactor(8.0f, delta_time);
    camera_pitch_ = approach(camera_pitch_, pitch_target_, zoom_factor);
    camera_distance_ = approach(camera_distance_, distance_target_, zoom_factor);
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

const WeaponState& GameState::weapon() const {
    return weapon_;
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

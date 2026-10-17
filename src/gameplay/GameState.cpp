#include "gameplay/GameState.h"

#include "app/InputState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>

namespace gameplay {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Movement: the body turns toward the input and walks or runs forward. It never
// strafes or walks backward, so every clip matches the way the body moves.
// Speeds are the planted-foot speeds measured from each clip, so feet do not slide.
constexpr float kWalkSpeed = 1.65f;
constexpr float kRunSpeed = 4.0f;
// Crouched: a slower walk. Estimated from the crouch clip, not yet measured on the feet.
constexpr float kCrouchSpeed = 0.9f;
// The warrior is a disc this wide on the ground, so walls stop it short of their faces.
constexpr float kPlayerRadius = 0.35f;
// Lens distance kept between the camera and a wall it is pulled in front of.
constexpr float kCameraWallMargin = 0.3f;
constexpr float kTurnRate = 10.0f;             // radians per second

// Velocity ramps toward the desired velocity. Stopping is as quick as starting:
// full walk speed takes about 0.07 s either way.
constexpr float kAcceleration = 25.0f;         // meters per second squared
constexpr float kDeceleration = 25.0f;
constexpr float kStopSpeed = 0.05f;
constexpr float kEdgeMargin = 0.5f;            // the warrior stops this far inside the world edge

// A key press counts for at least one frame, so a tap inside one frame still moves.
constexpr float kMinTapHold = 1.0f / 60.0f;

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
constexpr float kMinDistance = 2.0f;
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
    // Movement reads the held keys plus any key pressed since the last frame.
    // Each tap stays active for at least one frame.
    app::InputState moves = input;
    const bool pressed[4] = {input.tapped.w, input.tapped.a, input.tapped.s, input.tapped.d};
    bool* keys[4] = {&moves.keys.w, &moves.keys.a, &moves.keys.s, &moves.keys.d};
    for (int k = 0; k < 4; ++k) {
        if (pressed[k]) {
            tap_hold_[k] = kMinTapHold;
        }
        if (tap_hold_[k] > 0.0f) {
            *keys[k] = true;
            tap_hold_[k] -= delta_time;
        }
    }

    const bool has_movement_key = moves.keys.w || moves.keys.a || moves.keys.s || moves.keys.d;
    if (auto_recenter_ && !has_movement_key && idle_orbit_time_ > kRecenterDelay) {
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
    if (input.fire_pressed && weapon_.fire()) {
        const Ray ray = cameraRay();
        targets_.shoot(ray, std::min(Targets::kRange, nearestWallDistance(ray)));
    }
    targets_.update(delta_time);
    weapon_.setAiming(input.aim_held);
    weapon_.update(delta_time);

    if (input.crouch_pressed) {
        crouched_ = !crouched_;
    }
    const float look_goal = crouched_ ? kCrouchLookHeight : kStandLookHeight;
    look_height_ = approach(look_height_, look_goal, smoothingFactor(8.0f, delta_time));

    float dir_x = 0.0f;
    float dir_z = 0.0f;
    physics.inputDirection(moves, camera_azimuth_, dir_x, dir_z);
    const bool has_input = dir_x != 0.0f || dir_z != 0.0f;

    // The body turns toward the input, and moves only along its own facing once
    // it faces the input, so the clip always matches the motion on screen.
    float desired_x = 0.0f;
    float desired_z = 0.0f;
    Clip next = Clip::Walk;
    if (has_input) {
        const float input_heading = std::atan2(dir_x, dir_z);
        yaw_ = turnToward(yaw_, input_heading, kTurnRate * delta_time);
        const float align = std::fmax(0.0f, std::cos(angleBetween(yaw_, input_heading)));
        const bool running = input.keys.shift;
        next = crouched_ ? Clip::CrouchWalk : (running ? Clip::Run : Clip::Walk);
        const float speed = (crouched_ ? kCrouchSpeed : (running ? kRunSpeed : kWalkSpeed)) * align;
        // Facing is +Z at zero yaw, so the forward direction is (sin, cos).
        desired_x = std::sin(yaw_) * speed;
        desired_z = std::cos(yaw_) * speed;
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

    // The world has edges: stop at them rather than walking into empty space.
    const float edge = world::World::kHalfSize - kEdgeMargin;
    if (player_x_ > edge || player_x_ < -edge) {
        player_x_ = std::fmax(-edge, std::fmin(edge, player_x_));
        velocity_x_ = 0.0f;
    }
    if (player_z_ > edge || player_z_ < -edge) {
        player_z_ = std::fmax(-edge, std::fmin(edge, player_z_));
        velocity_z_ = 0.0f;
    }

    // Walls: push the warrior out of each one, and drop the velocity into it.
    for (const world::Box& wall : world.walls()) {
        const float nx = std::fmax(wall.min[0], std::fmin(wall.max[0], player_x_));
        const float nz = std::fmax(wall.min[2], std::fmin(wall.max[2], player_z_));
        float dx = player_x_ - nx;
        float dz = player_z_ - nz;
        const float d2 = dx * dx + dz * dz;
        if (d2 >= kPlayerRadius * kPlayerRadius) {
            continue;
        }
        if (d2 < 1e-8f) {
            // Inside the wall: move out through its near face, on the -Z side.
            player_z_ = wall.min[2] - kPlayerRadius;
            velocity_z_ = std::fmin(velocity_z_, 0.0f);
            continue;
        }
        const float d = std::sqrt(d2);
        dx /= d;
        dz /= d;
        const float push = kPlayerRadius - d;
        player_x_ += dx * push;
        player_z_ += dz * push;
        const float into = velocity_x_ * dx + velocity_z_ * dz;
        if (into < 0.0f) {
            velocity_x_ -= into * dx;
            velocity_z_ -= into * dz;
        }
    }

    const bool moving = std::sqrt(velocity_x_ * velocity_x_ + velocity_z_ * velocity_z_) > kStopSpeed;
    Clip clip = crouched_ ? Clip::CrouchIdle : Clip::Idle;
    if (has_input) {
        clip = next;
    } else if (moving) {
        clip = crouched_ ? Clip::CrouchWalk : Clip::Walk;  // slowing down: keep walking until stopped
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

void GameState::placeTargets(const world::World& world) {
    world_ = &world;
    targets_.place(world);
}

float GameState::cameraLookHeight() const {
    return look_height_;
}

bool GameState::crouched() const {
    return crouched_;
}

float GameState::nearestWallDistance(const Ray& ray) const {
    float nearest = 1e9f;
    if (world_ == nullptr) {
        return nearest;
    }
    for (const world::Box& wall : world_->walls()) {
        const float d = rayBoxDistance(ray, wall.min, wall.max);
        if (d >= 0.0f && d < nearest) {
            nearest = d;
        }
    }
    return nearest;
}

float GameState::clearCameraDistance(float wanted_distance) const {
    // Ray from the look point toward the camera; a wall on it shortens the camera's distance.
    const float cos_pitch = std::cos(camera_pitch_);
    const float dir[3] = {std::sin(camera_azimuth_) * cos_pitch, std::sin(camera_pitch_), std::cos(camera_azimuth_) * cos_pitch};
    Ray ray{};
    ray.origin[0] = camera_target_x_;
    ray.origin[1] = look_height_;
    ray.origin[2] = camera_target_z_;
    for (int c = 0; c < 3; ++c) {
        ray.direction[c] = dir[c];
    }
    const float wall = nearestWallDistance(ray);
    if (wall >= wanted_distance) {
        return wanted_distance;
    }
    return std::fmax(0.6f, wall - kCameraWallMargin);
}

Ray GameState::cameraRay() const {
    // The same eye position the renderer uses, including the aim pull-in and walls.
    const float aim = weapon_.aim();
    const float distance = clearCameraDistance(camera_distance_ * (1.0f - 0.35f * aim));
    const float horizontal = distance * std::cos(camera_pitch_);
    const float look[3] = {camera_target_x_, look_height_, camera_target_z_};
    const float eye[3] = {
        look[0] + horizontal * std::sin(camera_azimuth_),
        look[1] + distance * std::sin(camera_pitch_),
        look[2] + horizontal * std::cos(camera_azimuth_),
    };
    Ray ray{};
    float length = 0.0f;
    for (int c = 0; c < 3; ++c) {
        ray.origin[c] = eye[c];
        ray.direction[c] = look[c] - eye[c];
        length += ray.direction[c] * ray.direction[c];
    }
    length = std::sqrt(length);
    for (int c = 0; c < 3; ++c) {
        ray.direction[c] /= length;
    }
    return ray;
}

const Targets& GameState::targets() const {
    return targets_;
}

void GameState::setAutoRecenter(bool enabled) {
    auto_recenter_ = enabled;
}

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
        case Clip::CrouchWalk:
            return "CrouchWalk";
        case Clip::CrouchIdle:
            return "CrouchIdle";
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

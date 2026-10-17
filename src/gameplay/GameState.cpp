#include "gameplay/GameState.h"

#include "app/InputState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <cmath>

namespace gameplay {
namespace {

constexpr float kMovingSpeedThreshold = 0.05f;
constexpr float kTurnRate = 12.0f;
constexpr float kPi = 3.14159265358979323846f;

// Moves an angle toward a target along the shortest arc.
float approachAngle(float current, float target, float amount) {
    float diff = std::fmod(target - current + kPi, 2.0f * kPi);
    if (diff < 0.0f) {
        diff += 2.0f * kPi;
    }
    diff -= kPi;
    return current + diff * amount;
}

} // namespace

void GameState::init() {}

void GameState::update(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time) {
    const float old_x = player_x_;
    const float old_z = player_z_;
    physics.integratePlayer(input, world, delta_time, player_x_, player_z_);

    const float dx = player_x_ - old_x;
    const float dz = player_z_ - old_z;
    const float speed = delta_time > 0.0f ? std::sqrt(dx * dx + dz * dz) / delta_time : 0.0f;
    const bool moving = speed > kMovingSpeedThreshold;

    const Clip next = !moving ? Clip::Idle : (input.keys.shift ? Clip::Run : Clip::Walk);
    if (next != clip_) {
        clip_ = next;
        clip_time_ = 0.0f;
    }
    clip_time_ += delta_time;

    // Mouse or touch orbit (radians per pixel), and wheel zoom.
    constexpr float kOrbitSpeed = 0.006f;
    orbit_yaw_ -= input.look_dx * kOrbitSpeed;
    camera_pitch_ = std::fmin(1.1f, std::fmax(-0.15f, camera_pitch_ + input.look_dy * kOrbitSpeed));
    camera_distance_ = std::fmin(6.0f, std::fmax(1.5f, camera_distance_ - input.zoom_steps * 0.3f));

    if (moving) {
        yaw_ = approachAngle(yaw_, std::atan2(dx, dz), std::fmin(1.0f, kTurnRate * delta_time));
    }
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
        case Clip::Idle:
        default:
            return "Idle";
    }
}

float GameState::clipTime() const {
    return clip_time_;
}

float GameState::cameraAzimuth() const {
    constexpr float kPi = 3.14159265358979323846f;
    // Behind the player: the player faces yaw_, so the camera sits at yaw_ + pi.
    return yaw_ + kPi + orbit_yaw_;
}

float GameState::cameraPitch() const {
    return camera_pitch_;
}

float GameState::cameraDistance() const {
    return camera_distance_;
}

} // namespace gameplay

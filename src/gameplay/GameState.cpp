#include "gameplay/GameState.h"

#include "app/InputState.h"
#include "assets/CharacterModel.h"
#include "math/Geometry.h"
#include "math/Mat4.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
// A press that arrives this early before the next shot is allowed still fires.
constexpr float kFireBuffer = 0.15f;
// Camera kick per shot, in radians, and how fast it settles.
constexpr float kRecoilKick = 0.012f;
constexpr float kRecoilSettle = 12.0f;
// Sight view: the eye at the grip-side shoulder, the look point this far ahead along the aim.
// The rifle grip in the warrior's own frame (feet at the origin, facing +Z), measured
// from the aim pose. Carrying it through the warrior's terrain frame keeps it in reach on slopes.
constexpr float kGripModel[3] = {-0.12f, 1.38f, 0.2f};
// The bore sits this far above the grip (matches the rifle's grip offset in the renderer).
constexpr float kGripBelowBore = 0.05f;
// Aimed sight: no turn of the aim (kAimYaw = 0 keeps the verified sight view).
constexpr float kAimYaw = 0.0f;
// Shift the view toward the warrior's right so the centred crosshair sits to his right.
constexpr float kOrbitShift = 0.4f;
// Aim from a third-person shoulder view, far enough back to keep the shoulder visible.
constexpr float kSightBack = 2.5f;
constexpr float kSightUp = 0.3f;
constexpr float kSightRight = 0.55f;
constexpr float kSightRange = 20.0f;
// Lens distance kept between the camera and a wall it is pulled in front of.
constexpr float kCameraWallMargin = 0.3f;
constexpr float kTurnRate = 10.0f;             // radians per second

// Velocity ramps toward the desired velocity. Stopping is as quick as starting:
// full walk speed takes about 0.07 s either way.
constexpr float kAcceleration = 25.0f;         // meters per second squared
constexpr float kDeceleration = 25.0f;
constexpr float kStopSpeed = 0.05f;
constexpr float kEdgeMargin = 0.5f;            // the warrior stops this far inside the city edge

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
// The inspection camera (F3) zooms closer, to check the hands on the model.
constexpr float kInspectDistance = 1.0f;
constexpr float kInspectMinDistance = 0.8f;

// Driving camera: a chase camera behind the car that always comes back behind it.
constexpr float kCarMinDistance = 4.0f;
constexpr float kCarMaxDistance = 11.0f;
constexpr float kCarRecenterDelay = 0.8f;      // seconds after the last mouse or stick look
constexpr float kCarRecenterSpeed = 2.6f;      // radians per second at speed
constexpr float kCarCameraTargetTime = 0.06f;  // a stiffer follow than on foot: cars are fast
constexpr float kCarLookAbove = 1.0f;          // metres above the car's centre of mass
constexpr float kCarFovBase = 4.0f * kPi / 180.0f;
constexpr float kCarFovAtSpeed = 12.0f * kPi / 180.0f;
constexpr float kCarFovFullSpeed = 40.0f;      // m/s

// Collisions between cars, and how hard a crash must be to shake the camera.
constexpr float kCarRestitution = 0.25f;
constexpr float kCrashImpulse = 1800.0f;       // N s
constexpr float kCrashImpulseFull = 16000.0f;  // N s for the strongest shake
constexpr float kBulletImpulse = 40.0f;        // N s pushed into a car a bullet hits

// Camera shake from trauma (Eiserloh, "Juicing Your Cameras With Math", GDC 2016).
constexpr float kShotTrauma = 0.07f;
constexpr float kTraumaDecay = 1.3f;           // per second
constexpr float kShakeMaxAngle = 0.035f;       // radians
constexpr float kShakeMaxOffset = 0.06f;       // metres
constexpr float kShakeFrequency = 22.0f;

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

bool GameState::spawnWarrior(physics::Physics& physics, const world::World& world,
    const assets::CharacterModel& character) {
    warrior_character_ = &character;
    const math::Mat4 root = math::multiply(world.groundFrame(2.0f, 3.5f),
        math::rotationY(kPi - character.frontYaw()));
    assets::CharacterModel::PoseRequest request;
    character.evaluatePose(request, root,
        [&world](float x, float z) { return world.groundHeight(x, z); }, warrior_pose_);
    warrior_ragdoll_active_ = physics.startRagdoll(character, warrior_pose_);
    if (warrior_ragdoll_active_) {
        warrior_ragdoll_active_ = physics.ragdollSkinPose(warrior_pose_);
    }
    return warrior_ragdoll_active_;
}

void GameState::update(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time) {
    world_ = &world;
    if (input.ragdoll_pressed && warrior_character_ != nullptr) {
        spawnWarrior(physics, world, *warrior_character_);
    }
    if (warrior_ragdoll_active_) {
        physics.updateRagdoll(delta_time);
        warrior_ragdoll_active_ = physics.ragdollSkinPose(warrior_pose_);
    }
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
    if (input.inspect_pressed && !driving()) {
        inspecting_ = !inspecting_;
        distance_target_ = inspecting_ ? kInspectDistance : kMinDistance;
    }
    if (driving()) {
        car_distance_target_ = std::fmin(kCarMaxDistance, std::fmax(kCarMinDistance, car_distance_target_ - input.zoom_steps * 0.6f));
    } else {
        const float min_distance = inspecting_ ? kInspectMinDistance : kMinDistance;
        distance_target_ = std::fmin(kMaxDistance, std::fmax(min_distance, distance_target_ - input.zoom_steps * 0.3f));
    }

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

    // Use: get in or out of a car; on foot with no car in reach it is the punch key.
    if (input.use_pressed) {
        if (driving() || vehicleInReach() >= 0) {
            enterOrExit(world);
        } else {
            weapon_.attack();
        }
    }

    // Recenter. On foot: slowly behind the warrior, only once the player has stopped
    // orbiting and stopped moving, and only when enabled. Driving: always, after a short
    // pause, and faster at speed, as a chase camera.
    const bool has_movement_key = moves.keys.w || moves.keys.a || moves.keys.s || moves.keys.d ||
        moves.move_x != 0.0f || moves.move_y != 0.0f;
    float recenter_rate = 0.0f;
    float behind = yaw_ + kPi;
    if (driving()) {
        const physics::Vehicle& car = vehicles_[driven_];
        behind = car.yaw() + kPi;
        if (idle_orbit_time_ > kCarRecenterDelay) {
            recenter_rate = kCarRecenterSpeed * (0.35f + std::fmin(1.0f, std::fabs(car.forwardSpeed()) / 12.0f));
        }
    } else if (auto_recenter_ && !has_movement_key && idle_orbit_time_ > kRecenterDelay) {
        recenter_rate = kRecenterSpeed;
    }
    if (recenter_rate > 0.0f) {
        const float offset = angleBetween(camera_goal_azimuth_, behind);
        const float step = recenter_rate * delta_time;
        camera_goal_azimuth_ += std::fabs(offset) < step ? offset : (offset > 0.0f ? step : -step);
    }

    // The camera springs toward its goal. Input is read against that view.
    const float camera_goal_unwrapped = camera_azimuth_ + angleBetween(camera_azimuth_, camera_goal_azimuth_);
    camera_azimuth_ = smoothDamp(camera_azimuth_, camera_goal_unwrapped, camera_azimuth_speed_, kCameraAzimuthTime, delta_time);

    // Weapons: switch, punch, and aim. These are independent of locomotion.
    if (input.weapon_request >= 0) {
        weapon_.request(static_cast<WeaponSlot>(input.weapon_request));
    }
    if (input.attack_pressed && !driving()) {
        weapon_.attack();
    }

    if (driving()) {
        weapon_.setAiming(false);
        fire_buffer_ = 0.0f;
        crouched_ = false;
        velocity_x_ = 0.0f;
        velocity_z_ = 0.0f;
        clip_ = Clip::Idle;
        clip_time_ += delta_time;
    } else {
        updateOnFoot(input, physics, world, delta_time, moves);
    }
    last_shot_.age += delta_time;
    recoil_kick_ = approach(recoil_kick_, 0.0f, smoothingFactor(kRecoilSettle, delta_time));
    targets_.update(delta_time);
    weapon_.update(delta_time);

    updateVehicles(input, world, delta_time);

    // The camera's target: the warrior, leading along his velocity; or the car as drawn.
    float follow_x = player_x_;
    float follow_z = player_z_;
    float look_x = velocity_x_ * kLookAheadTime;
    float look_z = velocity_z_ * kLookAheadTime;
    float target_time = kCameraTargetTime;
    float look_goal = crouched_ ? kCrouchLookHeight : kStandLookHeight;
    if (driving()) {
        const physics::Vehicle& car = vehicles_[driven_];
        const math::Vec3 p = car.renderPosition();
        player_x_ = car.position().x;
        player_z_ = car.position().z;
        yaw_ = car.yaw();
        follow_x = p.x;
        follow_z = p.z;
        look_x = 0.0f;
        look_z = 0.0f;
        target_time = kCarCameraTargetTime;
        look_goal = p.y + kCarLookAbove;
    }
    look_height_ = approach(look_height_, look_goal, smoothingFactor(8.0f, delta_time));
    const float look_length = std::sqrt(look_x * look_x + look_z * look_z);
    if (look_length > kLookAheadMax) {
        look_x *= kLookAheadMax / look_length;
        look_z *= kLookAheadMax / look_length;
    }
    camera_target_x_ = smoothDamp(camera_target_x_, follow_x + look_x, camera_target_x_speed_, target_time, delta_time);
    camera_target_z_ = smoothDamp(camera_target_z_, follow_z + look_z, camera_target_z_speed_, target_time, delta_time);

    const float zoom_factor = smoothingFactor(8.0f, delta_time);
    camera_pitch_ = approach(camera_pitch_, pitch_target_, zoom_factor);
    camera_distance_ = approach(camera_distance_, driving() ? car_distance_target_ : distance_target_, zoom_factor);

    // At speed the view widens a little, which sells the speed.
    float fov_goal = 0.0f;
    if (driving()) {
        const float speed = std::fabs(vehicles_[driven_].forwardSpeed());
        fov_goal = kCarFovBase + kCarFovAtSpeed * std::fmin(1.0f, speed / kCarFovFullSpeed);
    }
    fov_boost_ = approach(fov_boost_, fov_goal, smoothingFactor(3.0f, delta_time));

    trauma_ = std::fmax(0.0f, trauma_ - kTraumaDecay * delta_time);
    shake_time_ += delta_time;
}

void GameState::updateOnFoot(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time,
    const app::InputState& moves) {
    // Fire while the button is held, and take a press that came just before the
    // next shot was allowed.
    if (input.fire_pressed) {
        fire_buffer_ = kFireBuffer;
    }
    fire_buffer_ = std::max(0.0f, fire_buffer_ - delta_time);
    if ((input.fire_held || fire_buffer_ > 0.0f) && weapon_.fire()) {
        fire_buffer_ = 0.0f;
        const Ray ray = cameraRay();
        int hit_vehicle = -1;
        const float reach = std::min(Targets::kRange, nearestWallDistance(ray, -1, &hit_vehicle));
        float impact = reach;
        const bool hit = targets_.shoot(ray, reach, &impact);
        last_shot_.age = 0.0f;
        last_shot_.hit = hit;
        for (int c = 0; c < 3; ++c) {
            last_shot_.impact[c] = ray.origin[c] + ray.direction[c] * impact;
        }
        if (!hit && hit_vehicle >= 0) {
            // A bullet nudges the car it hits.
            const math::Vec3 at{last_shot_.impact[0], last_shot_.impact[1], last_shot_.impact[2]};
            const math::Vec3 dir{ray.direction[0], ray.direction[1], ray.direction[2]};
            vehicles_[hit_vehicle].applyImpulse(dir * kBulletImpulse, at);
        }
        recoil_kick_ += kRecoilKick;
        addTrauma(kShotTrauma);
    }
    weapon_.setAiming(input.aim_held);

    if (input.crouch_pressed) {
        crouched_ = !crouched_;
    }

    float dir_x = 0.0f;
    float dir_z = 0.0f;
    physics.inputDirection(moves, camera_azimuth_, dir_x, dir_z);
    const bool has_input = dir_x != 0.0f || dir_z != 0.0f;

    // The body turns toward the input, and moves only along its own facing once
    // it faces the input, so the clip always matches the motion on screen.
    float desired_x = 0.0f;
    float desired_z = 0.0f;
    Clip next = Clip::Walk;
    // With the rifle aimed or firing, the body faces the camera's direction, so
    // shots and the view agree. While aiming, only movement toward the camera counts.
    const bool facing_camera = weapon_.active() == WeaponSlot::Rifle && (weapon_.aim() > 0.5f || weapon_.firing());
    const float camera_heading = camera_azimuth_ + kPi;  // the view looks along azimuth + pi
    if (facing_camera) {
        yaw_ = turnToward(yaw_, camera_heading, kTurnRate * delta_time);
    }
    if (has_input && facing_camera) {
        const float along = dir_x * std::sin(camera_heading) + dir_z * std::cos(camera_heading);
        const float amount = std::fmin(1.0f, std::fmax(0.0f, along));
        next = crouched_ ? Clip::CrouchWalk : Clip::Walk;
        const float speed = (crouched_ ? kCrouchSpeed : kWalkSpeed) * amount;
        desired_x = std::sin(yaw_) * speed;
        desired_z = std::cos(yaw_) * speed;
    } else if (has_input) {
        const float input_heading = std::atan2(dir_x, dir_z);
        yaw_ = turnToward(yaw_, input_heading, kTurnRate * delta_time);
        const float align = std::fmax(0.0f, std::cos(angleBetween(yaw_, input_heading)));
        const bool running = input.keys.shift || input.run_held;
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

    // The city has edges: stop at them rather than walking into empty space.
    const float edge = world::World::kCityHalfSize - kEdgeMargin;
    if (player_x_ > edge || player_x_ < -edge) {
        player_x_ = std::fmax(-edge, std::fmin(edge, player_x_));
        velocity_x_ = 0.0f;
    }
    if (player_z_ > edge || player_z_ < -edge) {
        player_z_ = std::fmax(-edge, std::fmin(edge, player_z_));
        velocity_z_ = 0.0f;
    }

    // Walls and buildings near the warrior: push him out of each, and drop the
    // velocity into it. The grid hands back only the boxes around him.
    static thread_local std::vector<int> nearby;
    nearby.clear();
    world.boxesNear(player_x_ - kPlayerRadius, player_z_ - kPlayerRadius, player_x_ + kPlayerRadius, player_z_ + kPlayerRadius, nearby);
    for (int index : nearby) {
        const world::Box& wall = world.walls()[index];
        float nx = 0.0f;
        float nz = 0.0f;
        float depth = 0.0f;
        if (!math::circleBoxPush(player_x_, player_z_, kPlayerRadius, wall.min, wall.max, nx, nz, depth)) {
            continue;
        }
        player_x_ += nx * depth;
        player_z_ += nz * depth;
        const float into = velocity_x_ * nx + velocity_z_ * nz;
        if (into < 0.0f) {
            velocity_x_ -= into * nx;
            velocity_z_ -= into * nz;
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
}

void GameState::updateVehicles(const app::InputState& input, const world::World& world, float delta_time) {
    for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i) {
        physics::VehicleInput controls;
        controls.parked = i != driven_;
        if (i == driven_) {
            const float steer_keys = (input.keys.d ? 1.0f : 0.0f) - (input.keys.a ? 1.0f : 0.0f);
            controls.throttle = std::fmax(input.keys.w ? 1.0f : 0.0f, input.throttle);
            controls.brake = std::fmax(input.keys.s ? 1.0f : 0.0f, input.brake);
            controls.steer = std::fmax(-1.0f, std::fmin(1.0f, steer_keys + input.move_x));
            controls.handbrake = input.handbrake_held;
        }
        vehicles_[i].update(controls, world, delta_time);
        if (i == driven_ && vehicles_[i].lastImpact() > kCrashImpulse) {
            addTrauma(std::fmin(0.7f, vehicles_[i].lastImpact() / kCrashImpulseFull));
        }
    }

    // Cars against each other.
    for (size_t i = 0; i < vehicles_.size(); ++i) {
        for (size_t j = i + 1; j < vehicles_.size(); ++j) {
            physics::Vehicle& a = vehicles_[i];
            physics::Vehicle& b = vehicles_[j];
            if (a.asleep() && b.asleep()) {
                continue;
            }
            const math::Vec3 d = b.position() - a.position();
            if (d.x * d.x + d.z * d.z > 36.0f) {
                continue;
            }
            collideVehiclePair(a, b);
        }
    }

    for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i) {
        physics::Vehicle& car = vehicles_[i];
        // A moving car knocks the practice targets over.
        if (!car.asleep() && math::length(car.velocity()) > 1.0f) {
            for (int c = 0; c < physics::Vehicle::kCircles; ++c) {
                float x = 0.0f;
                float z = 0.0f;
                float r = 0.0f;
                car.footprintCircle(c, x, z, r);
                targets_.knockDownInCircle(x, z, r);
            }
        }
        // The warrior on foot cannot walk through a car.
        if (!driving()) {
            float nx = 0.0f;
            float nz = 0.0f;
            float depth = 0.0f;
            if (car.circleOverlap(player_x_, player_z_, kPlayerRadius, nx, nz, depth)) {
                player_x_ += nx * depth;
                player_z_ += nz * depth;
                const float into = velocity_x_ * nx + velocity_z_ * nz;
                if (into < 0.0f) {
                    velocity_x_ -= into * nx;
                    velocity_z_ -= into * nz;
                }
            }
        }
    }
}

void GameState::collideVehiclePair(physics::Vehicle& a, physics::Vehicle& b) {
    for (int ia = 0; ia < physics::Vehicle::kCircles; ++ia) {
        for (int ib = 0; ib < physics::Vehicle::kCircles; ++ib) {
            float ax, az, ar, bx, bz, br;
            a.footprintCircle(ia, ax, az, ar);
            b.footprintCircle(ib, bx, bz, br);
            const float dx = bx - ax;
            const float dz = bz - az;
            const float d2 = dx * dx + dz * dz;
            const float reach = ar + br;
            if (d2 >= reach * reach) {
                continue;
            }
            const float d = std::sqrt(d2);
            const math::Vec3 n = d > 1e-5f ? math::Vec3{dx / d, 0.0f, dz / d} : math::Vec3{1.0f, 0.0f, 0.0f};
            const float depth = reach - d;
            a.wake();
            b.wake();
            a.translate(n * (-0.5f * depth));
            b.translate(n * (0.5f * depth));
            const math::Vec3 contact{ax + n.x * ar, 0.5f * (a.position().y + b.position().y), az + n.z * ar};
            const float vn = math::dot(b.velocityAt(contact) - a.velocityAt(contact), n);
            if (vn >= 0.0f) {
                continue;
            }
            const float k = a.inverseMassAlong(contact, n) + b.inverseMassAlong(contact, n);
            const float j = -(1.0f + kCarRestitution) * vn / k;
            a.applyImpulse(n * -j, contact);
            b.applyImpulse(n * j, contact);
            const physics::Vehicle* driven = driving() ? &vehicles_[driven_] : nullptr;
            if ((driven == &a || driven == &b) && j > kCrashImpulse) {
                addTrauma(std::fmin(0.7f, j / kCrashImpulseFull));
            }
        }
    }
}

int GameState::vehicleInReach() const {
    if (driving()) {
        return -1;
    }
    // Reach is measured to the car's body, not its centre: a door is in reach from
    // anywhere along the side of a 4.4 m car.
    int best = -1;
    float best_gap = kEnterReach;
    for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i) {
        const physics::Vehicle& car = vehicles_[i];
        float nx = 0.0f;
        float nz = 0.0f;
        float depth = 0.0f;
        if (car.up().y > 0.5f && car.circleOverlap(player_x_, player_z_, kEnterReach, nx, nz, depth)) {
            const float gap = kEnterReach - depth;  // metres from the body's footprint
            if (gap < best_gap) {
                best = i;
                best_gap = gap;
            }
        }
    }
    return best;
}

void GameState::enterOrExit(const world::World& world) {
    if (!driving()) {
        const int car = vehicleInReach();
        if (car < 0) {
            return;
        }
        driven_ = car;
        vehicles_[car].wake();
        crouched_ = false;
        inspecting_ = false;
        weapon_.setAiming(false);
        idle_orbit_time_ = 0.0f;
        return;
    }
    const physics::Vehicle& car = vehicles_[driven_];
    if (std::fabs(car.forwardSpeed()) > kExitMaxSpeed) {
        return;
    }
    // Step out at the driver's door (the car's left, local +X), or the other side, or
    // behind, or in front: the first spot clear of walls and other cars.
    const float yaw = car.yaw();
    const float left_x = std::cos(yaw);
    const float left_z = -std::sin(yaw);
    const float fwd_x = std::sin(yaw);
    const float fwd_z = std::cos(yaw);
    const float side = car.spec().half_extents.x + kPlayerRadius + 0.25f;
    const float end = car.spec().half_extents.z + kPlayerRadius + 0.3f;
    const float offsets[4][2] = {
        {left_x * side, left_z * side}, {-left_x * side, -left_z * side}, {-fwd_x * end, -fwd_z * end}, {fwd_x * end, fwd_z * end}};
    std::vector<int> nearby;
    for (const auto& o : offsets) {
        const float x = car.position().x + o[0];
        const float z = car.position().z + o[1];
        const float edge = world::World::kCityHalfSize - kEdgeMargin;
        bool clear = std::fabs(x) < edge && std::fabs(z) < edge;
        nearby.clear();
        world.boxesNear(x - kPlayerRadius, z - kPlayerRadius, x + kPlayerRadius, z + kPlayerRadius, nearby);
        for (int index : nearby) {
            float nx, nz, depth;
            if (math::circleBoxPush(x, z, kPlayerRadius, world.walls()[index].min, world.walls()[index].max, nx, nz, depth)) {
                clear = false;
            }
        }
        for (const physics::Vehicle& other : vehicles_) {
            float nx, nz, depth;
            if (&other != &car && other.circleOverlap(x, z, kPlayerRadius, nx, nz, depth)) {
                clear = false;
            }
        }
        if (clear) {
            driven_ = -1;
            setPlayerPosition(x, z, yaw);
            return;
        }
    }
}

void GameState::addTrauma(float amount) {
    trauma_ = std::fmin(1.0f, trauma_ + amount);
}

void GameState::setPlayerPosition(float x, float z, float yaw) {
    player_x_ = x;
    player_z_ = z;
    yaw_ = yaw;
    velocity_x_ = 0.0f;
    velocity_z_ = 0.0f;
    clip_ = Clip::Idle;
    clip_time_ = 0.0f;
}

void GameState::spawnVehicles(const world::World& world) {
    world_ = &world;
    vehicles_.clear();
    driven_ = -1;
    for (const world::ParkingSpot& spot : world.city().parking()) {
        physics::Vehicle car;
        car.reset(world, spot.x, spot.z, spot.yaw);
        vehicles_.push_back(car);
    }
}

float GameState::cameraFov() const {
    // Keep the same wide third-person field of view while aiming; the shoulder
    // offset changes framing without switching to a zoomed sight view.
    return 60.0f * kPi / 180.0f + fov_boost_;
}

void GameState::renderCameraView(float eye[3], float look[3]) const {
    cameraView(eye, look);
    const float shake = trauma_ * trauma_;
    if (shake <= 1e-4f) {
        return;
    }
    // Smooth noise, not random jitter: a few incommensurate sines per axis.
    auto noise = [](float t) { return 0.55f * std::sin(t) + 0.3f * std::sin(2.31f * t + 1.7f) + 0.15f * std::sin(4.79f * t + 0.3f); };
    const float t = shake_time_ * kShakeFrequency;
    const float yaw_offset = kShakeMaxAngle * shake * noise(t);
    const float pitch_offset = kShakeMaxAngle * shake * noise(t + 31.7f);
    const float lift = kShakeMaxOffset * shake * noise(t + 77.1f);
    float f[3] = {look[0] - eye[0], look[1] - eye[1], look[2] - eye[2]};
    const float length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (length < 1e-4f) {
        return;
    }
    for (float& c : f) c /= length;
    // right = f x up, up' = right x f
    float r[3] = {-f[2], 0.0f, f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-4f) {
        return;
    }
    r[0] /= rl;
    r[2] /= rl;
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    for (int c = 0; c < 3; ++c) {
        look[c] += (r[c] * yaw_offset + u[c] * pitch_offset) * length;
        eye[c] += u[c] * lift;
        look[c] += u[c] * lift;
    }
}

void GameState::cleanup() {
    warrior_character_ = nullptr;
    warrior_ragdoll_active_ = false;
}

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

float GameState::nearestWallDistance(const Ray& ray, int skip_vehicle, int* hit_vehicle) const {
    float nearest = 1e9f;
    if (hit_vehicle != nullptr) {
        *hit_vehicle = -1;
    }
    if (world_ != nullptr) {
        nearest = world_->raycast(ray.origin, ray.direction, nearest);
    }
    for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i) {
        if (i == skip_vehicle) {
            continue;
        }
        const float d = vehicles_[i].rayDistance(ray.origin, ray.direction);
        if (d >= 0.0f && d < nearest) {
            nearest = d;
            if (hit_vehicle != nullptr) {
                *hit_vehicle = i;
            }
        }
    }
    return nearest;
}

float GameState::clearCameraDistance(float wanted_distance) const {
    // Ray from the look point toward the camera; a wall on it shortens the camera's distance.
    const float pitch = cameraPitch();
    const float cos_pitch = std::cos(pitch);
    const float dir[3] = {std::sin(camera_azimuth_) * cos_pitch, std::sin(pitch), std::cos(camera_azimuth_) * cos_pitch};
    Ray ray{};
    ray.origin[0] = camera_target_x_;
    ray.origin[1] = look_height_;
    ray.origin[2] = camera_target_z_;
    for (int c = 0; c < 3; ++c) {
        ray.direction[c] = dir[c];
    }
    const float wall = nearestWallDistance(ray, driven_);
    if (wall >= wanted_distance) {
        return wanted_distance;
    }
    return std::fmax(0.6f, wall - kCameraWallMargin);
}

void GameState::sightGrip(float out[3]) const {
    // The warrior's frame: on the terrain, tilted to its normal, turned to his yaw.
    math::Mat4 frame = math::identity();
    if (world_ != nullptr) {
        frame = math::multiply(world_->groundFrame(player_x_, player_z_), math::rotationY(yaw_));
    }
    for (int c = 0; c < 3; ++c) {
        out[c] = frame.m[c] * kGripModel[0] + frame.m[4 + c] * kGripModel[1] + frame.m[8 + c] * kGripModel[2] + frame.m[12 + c];
    }
}

void GameState::cameraView(float eye[3], float look[3]) const {
    const float pitch = cameraPitch();
    const float az = camera_azimuth_;

    // Orbit: look at the target, with the eye behind it at the camera distance,
    // pulled in by walls.
    const float distance = clearCameraDistance(camera_distance_);
    const float horizontal = distance * std::cos(pitch);
    // Shift the view target right of the warrior, placing the centred crosshair
    // just to the right of the player.
    const float orbit_right[3] = {std::cos(az), 0.0f, -std::sin(az)};
    const float orbit_look[3] = {
        camera_target_x_ + orbit_right[0] * kOrbitShift,
        look_height_,
        camera_target_z_ + orbit_right[2] * kOrbitShift,
    };
    const float orbit_eye[3] = {
        orbit_look[0] + horizontal * std::sin(az),
        orbit_look[1] + distance * std::sin(pitch),
        orbit_look[2] + horizontal * std::cos(az),
    };

    // Shoulder aim: look along the rifle's shot line from behind its grip. The wider
    // field of view keeps the warrior's shoulder visible instead of filling the view.
    const float forward[3] = {-std::sin(az) * std::cos(pitch), -std::sin(pitch), -std::cos(az) * std::cos(pitch)};
    float grip[3];
    sightGrip(grip);
    // The rifle points along the view, so the crosshair marks where the barrel and the
    // shots go. The eye sits over the grip-side shoulder, outside the body, not on the bore:
    // the arm is held at chest height, so an eye on the bore line would be inside the body.
    const float bore[3] = {grip[0], grip[1] + kGripBelowBore, grip[2]};
    const float right[3] = {std::cos(az), 0.0f, -std::sin(az)};
    const float sight_eye[3] = {
        bore[0] - forward[0] * kSightBack + right[0] * kSightRight,
        bore[1] - forward[1] * kSightBack + kSightUp,
        bore[2] - forward[2] * kSightBack + right[2] * kSightRight,
    };
    // Keep the sight line aligned with the view direction.
    float aim_dir[3] = {forward[0] - right[0] * kAimYaw, forward[1], forward[2] - right[2] * kAimYaw};
    const float aim_len = std::sqrt(aim_dir[0] * aim_dir[0] + aim_dir[1] * aim_dir[1] + aim_dir[2] * aim_dir[2]);
    for (float& c : aim_dir) c /= aim_len;
    const float sight_look[3] = {
        sight_eye[0] + aim_dir[0] * kSightRange,
        sight_eye[1] + aim_dir[1] * kSightRange,
        sight_eye[2] + aim_dir[2] * kSightRange,
    };

    // Blend from the orbit view to the sight view as the rifle comes up.
    const float w = weapon_.active() == WeaponSlot::Rifle ? weapon_.aim() : 0.0f;
    for (int c = 0; c < 3; ++c) {
        eye[c] = orbit_eye[c] + (sight_eye[c] - orbit_eye[c]) * w;
        look[c] = orbit_look[c] + (sight_look[c] - orbit_look[c]) * w;
    }
}

Ray GameState::cameraRay() const {
    // The same eye and look point the renderer uses, so shots follow the view.
    float eye[3];
    float look[3];
    cameraView(eye, look);
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

const GameState::ShotEvent& GameState::lastShot() const {
    return last_shot_;
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
    return camera_pitch_ + recoil_kick_;
}

float GameState::cameraDistance() const {
    return camera_distance_;
}

} // namespace gameplay

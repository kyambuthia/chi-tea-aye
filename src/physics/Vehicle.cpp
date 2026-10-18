#include "physics/Vehicle.h"

#include "math/Geometry.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace physics {
namespace {

using math::Quat;
using math::Vec3;

constexpr float kGravity = 9.81f;
constexpr float kAngularDamping = 0.35f;     // per second: keeps spins from ringing forever
constexpr float kTractionLimit = 0.85f;      // drive force at most this share of a wheel's grip
constexpr float kMinSlipSpeed = 3.0f;        // m/s: below this, slip is measured against this speed
constexpr float kBuildingRestitution = 0.2f;
constexpr float kBuildingFriction = 0.35f;
constexpr float kGroundRestitution = 0.05f;
constexpr float kGroundFriction = 0.6f;
constexpr float kRecoverAfter = 2.5f;        // seconds on its side or roof before it is set upright
constexpr float kEdgeMargin = 2.4f;          // metres inside the city edge

float sign(float v) {
    return v < 0.0f ? -1.0f : 1.0f;
}

Vec3 groundNormal(const world::World& world, float x, float z) {
    const float eps = 0.1f;
    const float sx = (world.groundHeight(x + eps, z) - world.groundHeight(x - eps, z)) / (2.0f * eps);
    const float sz = (world.groundHeight(x, z + eps) - world.groundHeight(x, z - eps)) / (2.0f * eps);
    return math::normalize(Vec3{-sx, 1.0f, -sz});
}

// Lateral force share of the peak for a slip angle: linear up to the peak, then a
// gentle fall, so a car past the limit slides rather than snapping.
float lateralCurve(float slip, float peak, float falloff) {
    const float a = std::fabs(slip);
    if (a < peak) {
        return a / peak;
    }
    return 1.0f - falloff * std::fmin(1.0f, (a - peak) / (3.0f * peak));
}

} // namespace

Vehicle::Vehicle(const VehicleSpec& spec) : spec_(spec) {
    const Vec3 size = spec_.half_extents * 2.0f;
    const float m = spec_.mass;
    // Solid box inertia about the centre of mass.
    inertia_inverse_body_ = {
        12.0f / (m * (size.y * size.y + size.z * size.z)),
        12.0f / (m * (size.x * size.x + size.z * size.z)),
        12.0f / (m * (size.x * size.x + size.y * size.y)),
    };
    const float xs[4] = {-spec_.track_half, spec_.track_half, -spec_.track_half, spec_.track_half};
    const float zs[4] = {spec_.axle_front, spec_.axle_front, spec_.axle_rear, spec_.axle_rear};
    for (int i = 0; i < 4; ++i) {
        wheels_[i].mount = {xs[i], spec_.mount_y, zs[i]};
        wheels_[i].front = i < 2;
    }
}

void Vehicle::reset(const world::World& world, float x, float z, float yaw) {
    orientation_ = math::quatFromYaw(yaw);
    // Ride height at rest: each wheel carries a quarter of the weight on its spring.
    const float sag = spec_.mass * kGravity * 0.25f / spec_.spring;
    const float reach = spec_.rest_length + spec_.wheel_radius - sag;
    position_ = {x, world.groundHeight(x, z) + reach - spec_.mount_y, z};
    velocity_ = {};
    angular_velocity_ = {};
    previous_position_ = position_;
    previous_orientation_ = orientation_;
    accumulator_ = 0.0f;
    upside_down_time_ = 0.0f;
    still_time_ = 0.0f;
    asleep_ = false;
    for (Wheel& w : wheels_) {
        w.compression = sag;
        w.grounded = true;
        w.steer = 0.0f;
        w.spin_rate = 0.0f;
        w.slip = 0.0f;
    }
}

void Vehicle::update(const VehicleInput& input, const world::World& world, float delta_time) {
    impact_this_update_ = 0.0f;
    const bool driven = input.throttle > 0.0f || input.brake > 0.0f || input.steer != 0.0f || input.handbrake;
    if (driven) {
        wake();
    }
    if (asleep_) {
        previous_position_ = position_;
        previous_orientation_ = orientation_;
        last_impact_ = 0.0f;
        return;
    }
    accumulator_ += std::fmin(std::fmax(delta_time, 0.0f), kMaxStepsPerUpdate * kStep);
    while (accumulator_ >= kStep) {
        previous_position_ = position_;
        previous_orientation_ = orientation_;
        step(input, world, kStep);
        accumulator_ -= kStep;
    }
    last_impact_ = impact_this_update_;

    // Sleep once settled on all four wheels with nobody driving.
    const bool still = math::length(velocity_) < 0.06f && math::length(angular_velocity_) < 0.06f && groundedWheels() == 4;
    still_time_ = (still && !driven) ? still_time_ + delta_time : 0.0f;
    if (still_time_ > kSleepAfter) {
        asleep_ = true;
        velocity_ = {};
        angular_velocity_ = {};
        accumulator_ = 0.0f;
        previous_position_ = position_;
        previous_orientation_ = orientation_;
    }
}

void Vehicle::wake() {
    asleep_ = false;
    still_time_ = 0.0f;
}

float Vehicle::inverseMassAlong(Vec3 at, Vec3 n) const {
    const Vec3 r = at - position_;
    return 1.0f / spec_.mass + math::dot(n, math::cross(inverseInertiaTimes(math::cross(r, n)), r));
}

void Vehicle::translate(Vec3 offset) {
    position_ += offset;
    previous_position_ += offset;
}

Vec3 Vehicle::forward() const {
    return math::rotate(orientation_, Vec3{0.0f, 0.0f, 1.0f});
}

Vec3 Vehicle::up() const {
    return math::rotate(orientation_, Vec3{0.0f, 1.0f, 0.0f});
}

float Vehicle::yaw() const {
    const Vec3 f = forward();
    return std::atan2(f.x, f.z);
}

float Vehicle::forwardSpeed() const {
    return math::dot(velocity_, forward());
}

int Vehicle::groundedWheels() const {
    int n = 0;
    for (const Wheel& w : wheels_) n += w.grounded ? 1 : 0;
    return n;
}

Vec3 Vehicle::inverseInertiaTimes(Vec3 v) const {
    // I_world^-1 v = R I_body^-1 R^T v
    Vec3 local = math::rotateInverse(orientation_, v);
    local = {local.x * inertia_inverse_body_.x, local.y * inertia_inverse_body_.y, local.z * inertia_inverse_body_.z};
    return math::rotate(orientation_, local);
}

Vec3 Vehicle::pointVelocity(Vec3 at) const {
    return velocity_ + math::cross(angular_velocity_, at - position_);
}

void Vehicle::applyImpulseNow(Vec3 impulse, Vec3 at) {
    velocity_ += impulse * (1.0f / spec_.mass);
    angular_velocity_ += inverseInertiaTimes(math::cross(at - position_, impulse));
}

void Vehicle::applyImpulse(Vec3 impulse, Vec3 at) {
    wake();
    applyImpulseNow(impulse, at);
}

void Vehicle::step(const VehicleInput& input, const world::World& world, float h) {
    const Vec3 up_axis = up();
    const Vec3 down = -up_axis;
    const float v_forward = forwardSpeed();
    const float quarter_mass = 0.25f * spec_.mass;

    // Steering: the lock shrinks with speed, and the wheels turn at a limited rate.
    // Positive input turns right, which is a negative yaw (yaw grows toward +X, the left).
    const float lock = spec_.max_steer / (1.0f + std::fabs(v_forward) / spec_.steer_speed_falloff);
    const float steer_goal = -std::fmax(-1.0f, std::fmin(1.0f, input.steer)) * lock;
    for (Wheel& w : wheels_) {
        if (!w.front) continue;
        const float rate = spec_.steer_rate * (std::fabs(steer_goal) < std::fabs(w.steer) ? 2.0f : 1.0f) * h;
        w.steer += std::fmax(-rate, std::fmin(rate, steer_goal - w.steer));
    }

    // Pedals: the brake pedal reverses from a standstill, and throttle while rolling
    // backward brakes first, as in most driving games.
    float drive = 0.0f;
    float braking = 0.0f;
    const float throttle = std::fmax(0.0f, std::fmin(1.0f, input.throttle));
    const float brake = std::fmax(0.0f, std::fmin(1.0f, input.brake));
    if (throttle > 0.0f) {
        if (v_forward < -0.5f) {
            braking = std::fmax(braking, throttle);
        } else {
            const float r = v_forward / spec_.top_speed;
            drive = throttle * spec_.engine_force * std::fmax(0.0f, 1.0f - r * r);
        }
    }
    if (brake > 0.0f) {
        if (v_forward > 0.5f) {
            braking = std::fmax(braking, brake);
        } else if (throttle <= 0.0f) {
            const float r = v_forward / spec_.reverse_top_speed;
            drive = -brake * spec_.reverse_force * std::fmax(0.0f, 1.0f - r * r);
        }
    }

    if (input.parked) {
        drive = 0.0f;
        braking = 1.0f;  // the brakes hold all four wheels; no reversing
    }

    Vec3 force{0.0f, -kGravity * spec_.mass, 0.0f};
    Vec3 torque{};
    auto addForce = [&](Vec3 f, Vec3 at) {
        force += f;
        torque += math::cross(at - position_, f);
    };

    for (Wheel& w : wheels_) {
        const Vec3 mount = position_ + math::rotate(orientation_, w.mount);
        const float reach = spec_.rest_length + spec_.wheel_radius;
        // Ray from the mount along the body's down axis to the height field: exact on
        // flat ground, and two refinements follow slopes.
        w.grounded = false;
        float t = 0.0f;
        if (down.y < -0.3f) {
            for (int k = 0; k < 3; ++k) {
                const Vec3 p = mount + down * t;
                t += (p.y - world.groundHeight(p.x, p.z)) / -down.y;
            }
            w.grounded = t < reach;
        }
        if (!w.grounded) {
            w.compression = 0.0f;
            w.slip = 0.0f;
            // Free wheels keep spinning, slowly losing speed; the throttle spins the rear.
            w.spin_rate *= 1.0f / (1.0f + 1.5f * h);
            if (!w.front) w.spin_rate += drive / spec_.engine_force * 30.0f * h;
            w.spin += w.spin_rate * h;
            continue;
        }
        t = std::fmax(t, 0.0f);
        const Vec3 contact = mount + down * t;
        w.compression = reach - t;

        // Suspension: spring on the compression plus a damper on its rate.
        const Vec3 v_contact = pointVelocity(contact);
        const float compress_rate = math::dot(v_contact, down);
        float load = spec_.spring * w.compression + spec_.damper * compress_rate;
        if (w.compression > spec_.rest_length) {
            load += spec_.bump_stop * (w.compression - spec_.rest_length);
        }
        load = std::fmax(0.0f, load);
        addForce(up_axis * load, contact);

        // Tyre frame on the ground plane.
        const Vec3 n = groundNormal(world, contact.x, contact.z);
        Vec3 wheel_forward = math::rotate(orientation_, Vec3{std::sin(w.steer), 0.0f, std::cos(w.steer)});
        wheel_forward = math::normalize(wheel_forward - n * math::dot(wheel_forward, n));
        const Vec3 side = math::normalize(math::cross(n, wheel_forward));
        const float v_long = math::dot(v_contact, wheel_forward);
        const float v_lat = math::dot(v_contact, side);

        // The tyre's grip is capped so a hard landing does not give superhuman traction.
        const float tyre_load = std::fmin(load, 3.0f * quarter_mass * kGravity);
        const bool locked = input.handbrake && !w.front;
        const float mu = spec_.grip * (locked ? spec_.handbrake_grip : 1.0f);
        const float limit = mu * tyre_load;
        const float stop_force = std::fabs(v_long) * quarter_mass / h;  // force that stops this wheel's share in one step

        float f_long = 0.0f;
        if (!w.front && !locked) {
            f_long += std::fmax(-kTractionLimit * spec_.grip * tyre_load, std::fmin(kTractionLimit * spec_.grip * tyre_load, 0.5f * drive));
        }
        if (braking > 0.0f) {
            f_long -= sign(v_long) * std::fmin(braking * spec_.brake_force * 0.25f, stop_force);
        }
        if (locked) {
            f_long -= sign(v_long) * std::fmin(0.8f * limit, stop_force);
        }
        f_long -= spec_.rolling * v_long;

        const float slip = std::atan2(v_lat, std::fmax(std::fabs(v_long), kMinSlipSpeed));
        w.slip = slip;
        float f_lat = -sign(slip) * limit * lateralCurve(slip, spec_.peak_slip, spec_.slide_falloff);

        // Friction circle: the tyre's total grip is shared between driving and turning.
        const float total = std::sqrt(f_long * f_long + f_lat * f_lat);
        if (total > limit && total > 0.0f) {
            const float s = limit / total;
            f_long *= s;
            f_lat *= s;
        }

        // Tyre forces act part way up toward the centre of mass: less body roll than at
        // the contact patch, and far fewer roll-overs.
        const Vec3 raised = contact + up_axis * (math::dot(position_ - contact, up_axis) * (1.0f - spec_.roll_influence));
        addForce(wheel_forward * f_long + side * f_lat, raised);

        w.spin_rate = locked ? 0.0f : v_long / spec_.wheel_radius;
        w.spin += w.spin_rate * h;
    }

    // Air drag.
    force -= velocity_ * (math::length(velocity_) * spec_.drag);

    // Semi-implicit Euler: velocities first, then contacts, then positions.
    velocity_ += force * (h / spec_.mass);
    angular_velocity_ += inverseInertiaTimes(torque) * h;
    angular_velocity_ *= 1.0f / (1.0f + kAngularDamping * h);

    collideGround(world);
    collideBoxes(world);

    position_ += velocity_ * h;
    orientation_ = math::integrate(orientation_, angular_velocity_, h);

    // Keep the car inside the city.
    const float edge = world::World::kCityHalfSize - kEdgeMargin;
    float* axes[2] = {&position_.x, &position_.z};
    float* speeds[2] = {&velocity_.x, &velocity_.z};
    for (int a = 0; a < 2; ++a) {
        if (std::fabs(*axes[a]) > edge) {
            *axes[a] = sign(*axes[a]) * edge;
            if (*speeds[a] * sign(*axes[a]) > 0.0f) {
                impact_this_update_ = std::fmax(impact_this_update_, std::fabs(*speeds[a]) * spec_.mass);
                *speeds[a] *= -0.2f;
            }
        }
    }

    // A car left on its side or roof is set back on its wheels after a moment.
    if (up().y < 0.25f && math::length(velocity_) < 1.5f) {
        upside_down_time_ += h;
        if (upside_down_time_ > kRecoverAfter) {
            reset(world, position_.x, position_.z, yaw());
        }
    } else {
        upside_down_time_ = 0.0f;
    }
}

void Vehicle::applyContactImpulse(Vec3 at, Vec3 normal, float restitution, float friction) {
    const Vec3 r = at - position_;
    const Vec3 v = pointVelocity(at);
    const float vn = math::dot(v, normal);
    if (vn >= 0.0f) {
        return;
    }
    const float inv_mass = 1.0f / spec_.mass;
    const float k = inv_mass + math::dot(normal, math::cross(inverseInertiaTimes(math::cross(r, normal)), r));
    const float j = -(1.0f + restitution) * vn / k;
    applyImpulseNow(normal * j, at);
    impact_this_update_ = std::fmax(impact_this_update_, j);

    // Coulomb friction along the sliding direction, at most friction * j.
    const Vec3 v_after = pointVelocity(at);
    const Vec3 tangent_v = v_after - normal * math::dot(v_after, normal);
    const float speed = math::length(tangent_v);
    if (speed < 1e-4f) {
        return;
    }
    const Vec3 t = tangent_v * (1.0f / speed);
    const float kt = inv_mass + math::dot(t, math::cross(inverseInertiaTimes(math::cross(r, t)), r));
    const float jt = std::fmin(speed / kt, friction * j);
    applyImpulseNow(t * -jt, at);
}

void Vehicle::collideGround(const world::World& world) {
    // The body box's corners must stay above the ground: this catches roll-overs and
    // hard landings that bottom out the springs.
    const Vec3 he = spec_.half_extents;
    float deepest = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const Vec3 local{(i & 1) ? he.x : -he.x, spec_.body_above_com + ((i & 2) ? he.y : -he.y), (i & 4) ? he.z : -he.z};
        const Vec3 p = position_ + math::rotate(orientation_, local);
        const float g = world.groundHeight(p.x, p.z);
        if (p.y < g) {
            deepest = std::fmax(deepest, g - p.y);
            applyContactImpulse(p, groundNormal(world, p.x, p.z), kGroundRestitution, kGroundFriction);
        }
    }
    position_.y += deepest * 0.8f;
}

void Vehicle::footprintCircle(int i, float& x, float& z, float& radius) const {
    const float zs[kCircles] = {spec_.axle_rear, 0.0f, spec_.axle_front};
    const Vec3 c = position_ + math::rotate(orientation_, Vec3{0.0f, 0.0f, zs[i]});
    x = c.x;
    z = c.z;
    radius = spec_.half_extents.x + 0.05f;
}

void Vehicle::collideBoxes(const world::World& world) {
    // The body against buildings and walls: three circles along its length, pushed
    // out of each box footprint, with an impulse at the contact.
    static thread_local std::vector<int> nearby;
    const float bottom = position_.y + spec_.body_above_com - spec_.half_extents.y;
    for (int c = 0; c < kCircles; ++c) {
        float cx = 0.0f;
        float cz = 0.0f;
        float r = 0.0f;
        footprintCircle(c, cx, cz, r);
        nearby.clear();
        world.boxesNear(cx - r, cz - r, cx + r, cz + r, nearby);
        for (int index : nearby) {
            const world::Box& box = world.walls()[index];
            if (box.max[1] < bottom + 0.1f) {
                continue;  // low enough to pass over
            }
            float nx = 0.0f;
            float nz = 0.0f;
            float depth = 0.0f;
            if (!math::circleBoxPush(cx, cz, r, box.min, box.max, nx, nz, depth)) {
                continue;
            }
            position_.x += nx * depth;
            position_.z += nz * depth;
            cx += nx * depth;
            cz += nz * depth;
            const Vec3 contact{cx - nx * r, position_.y, cz - nz * r};
            applyContactImpulse(contact, Vec3{nx, 0.0f, nz}, kBuildingRestitution, kBuildingFriction);
        }
    }
}

bool Vehicle::circleOverlap(float x, float z, float radius, float& normal_x, float& normal_z, float& depth) const {
    // Exact circle against the car's rotated footprint rectangle.
    const float yaw_now = yaw();
    const float c = std::cos(yaw_now);
    const float s = std::sin(yaw_now);
    const float dx = x - position_.x;
    const float dz = z - position_.z;
    // Into the car frame: local +Z is forward (sin yaw, cos yaw), local +X is (cos yaw, -sin yaw).
    const float lx = dx * c - dz * s;
    const float lz = dx * s + dz * c;
    const float he_x = spec_.half_extents.x;
    const float he_z = spec_.half_extents.z;
    const float box_min[3] = {-he_x, 0.0f, -he_z};
    const float box_max[3] = {he_x, 0.0f, he_z};
    float lnx = 0.0f;
    float lnz = 0.0f;
    if (!math::circleBoxPush(lx, lz, radius, box_min, box_max, lnx, lnz, depth)) {
        return false;
    }
    normal_x = lnx * c + lnz * s;
    normal_z = -lnx * s + lnz * c;
    return true;
}

float Vehicle::rayDistance(const float origin[3], const float direction[3]) const {
    const Vec3 o = math::rotateInverse(orientation_, Vec3{origin[0], origin[1], origin[2]} - position_);
    const Vec3 d = math::rotateInverse(orientation_, Vec3{direction[0], direction[1], direction[2]});
    const float lo[3] = {-spec_.half_extents.x, spec_.body_above_com - spec_.half_extents.y, -spec_.half_extents.z};
    const float hi[3] = {spec_.half_extents.x, spec_.body_above_com + spec_.half_extents.y, spec_.half_extents.z};
    const float lo_origin[3] = {o.x, o.y, o.z};
    const float lo_dir[3] = {d.x, d.y, d.z};
    return math::rayBoxDistance(lo_origin, lo_dir, lo, hi);
}

Vec3 Vehicle::renderPosition() const {
    return math::lerp(previous_position_, position_, accumulator_ / kStep);
}

Quat Vehicle::renderOrientation() const {
    return math::nlerp(previous_orientation_, orientation_, accumulator_ / kStep);
}

math::Mat4 Vehicle::bodyTransform() const {
    return math::multiply(math::toMat4(renderOrientation(), renderPosition()), math::translation(0.0f, spec_.body_above_com, 0.0f));
}

math::Mat4 Vehicle::wheelTransform(int i) const {
    const Wheel& w = wheels_[i];
    const float drop = spec_.rest_length - w.compression;
    const math::Mat4 body = math::toMat4(renderOrientation(), renderPosition());
    const math::Mat4 at = math::translation(w.mount.x, w.mount.y - drop, w.mount.z);
    return math::multiply(body, math::multiply(at, math::multiply(math::rotationY(w.steer), math::rotationX(w.spin))));
}

} // namespace physics

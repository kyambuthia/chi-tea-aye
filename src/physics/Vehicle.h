#pragma once

#include "math/Mat4.h"
#include "math/Vec3.h"

namespace world {
class World;
}

namespace physics {

// Tuning for one car. Defaults are a soft, heavy saloon: low natural frequency on the
// springs and light damping, so the body pitches under braking and rolls in turns.
struct VehicleSpec {
    float mass = 1400.0f;                       // kg
    math::Vec3 half_extents{0.9f, 0.55f, 2.2f};  // body box half sizes: width, height, length
    float body_above_com = 0.2f;                 // the box centre sits this far above the centre of mass
    float track_half = 0.78f;                    // wheel x from the centreline
    float axle_front = 1.35f;                    // front axle z
    float axle_rear = -1.35f;                    // rear axle z
    float mount_y = -0.1f;                       // wheel mounts, relative to the centre of mass
    float wheel_radius = 0.34f;
    float rest_length = 0.42f;                   // suspension travel from mount to wheel centre
    float spring = 36000.0f;                     // N/m per wheel (about 1.6 Hz on a quarter of the mass)
    float damper = 3000.0f;                      // N s/m per wheel (under-damped on purpose)
    float bump_stop = 160000.0f;                 // N/m once the wheel is past its travel
    float engine_force = 9500.0f;                // N at a standstill, falling to 0 at top speed
    float top_speed = 48.0f;                     // m/s
    float reverse_force = 5000.0f;
    float reverse_top_speed = 11.0f;
    float brake_force = 15000.0f;                // N over all four wheels
    float grip = 1.1f;                           // tyre friction coefficient
    float handbrake_grip = 0.42f;                // rear grip scale with the handbrake on: slides the tail
    float peak_slip = 0.16f;                     // radians of slip at the peak lateral force
    float slide_falloff = 0.25f;                 // lateral force lost past the peak (fraction)
    float roll_influence = 0.35f;                // 1 applies tyre forces at the contact, 0 at the centre of mass
    float max_steer = 0.62f;                     // radians at a standstill
    float steer_speed_falloff = 22.0f;           // m/s at which the steering lock halves
    float steer_rate = 3.2f;                     // radians per second the wheels turn
    float drag = 0.42f;                          // N per (m/s)^2
    float rolling = 28.0f;                       // N per m/s per wheel
};

// Driver input for one frame.
struct VehicleInput {
    float throttle = 0.0f;  // 0..1
    float brake = 0.0f;     // 0..1; at a standstill it reverses
    float steer = 0.0f;     // -1 full left .. +1 full right
    bool handbrake = false;
    bool parked = false;    // nobody at the wheel: all four wheels held, so it does not roll away
};

// A raycast car: one rigid body held up by four spring-damper wheels that each cast
// a ray to the ground. Tyre forces come from the contact's slip, limited by the
// friction circle, so a car can grip, slide, and drift.
//
// The simulation runs at a fixed 120 Hz with an accumulator, independent of the
// frame rate, and the pose for drawing is interpolated between the last two steps.
class Vehicle {
public:
    static constexpr float kStep = 1.0f / 120.0f;
    static constexpr int kMaxStepsPerUpdate = 24;  // 0.2 s; a longer stall is dropped, not simulated
    static constexpr float kSleepAfter = 1.0f;     // seconds at rest before a parked car sleeps

    struct Wheel {
        math::Vec3 mount;  // body frame, relative to the centre of mass
        bool front = false;
        bool grounded = false;
        float compression = 0.0f;  // metres
        float steer = 0.0f;        // radians
        float spin = 0.0f;         // radians, for drawing
        float spin_rate = 0.0f;    // radians per second
        float slip = 0.0f;         // radians of lateral slip, for effects
    };

    explicit Vehicle(const VehicleSpec& spec = VehicleSpec{});

    // Puts the car on the ground at rest, facing yaw (radians about +Y; 0 faces +Z).
    void reset(const world::World& world, float x, float z, float yaw);

    // Advances by delta_time in fixed steps.
    void update(const VehicleInput& input, const world::World& world, float delta_time);

    // Pose for drawing: interpolated between the last two fixed steps.
    math::Mat4 bodyTransform() const;  // the body box centre, oriented
    math::Mat4 wheelTransform(int wheel) const;
    math::Vec3 renderPosition() const;  // centre of mass
    math::Quat renderOrientation() const;

    // Current (not interpolated) state, for gameplay.
    math::Vec3 position() const { return position_; }
    math::Quat orientation() const { return orientation_; }
    math::Vec3 velocity() const { return velocity_; }
    math::Vec3 angularVelocity() const { return angular_velocity_; }
    math::Vec3 forward() const;  // body +Z in the world
    math::Vec3 up() const;
    float yaw() const;            // heading of the body's forward on the ground
    float forwardSpeed() const;   // m/s along the body's forward; negative reversing
    int groundedWheels() const;
    const Wheel& wheel(int i) const { return wheels_[i]; }
    const VehicleSpec& spec() const { return spec_; }

    // The largest collision impulse (N s) since the last update, for camera shake and sound.
    float lastImpact() const { return last_impact_; }

    // Applies an impulse (N s) at a world point, e.g. from a bullet or another car. Wakes the car.
    void applyImpulse(math::Vec3 impulse, math::Vec3 at);

    // Contact helpers for resolving car-against-car hits outside the car.
    math::Vec3 velocityAt(math::Vec3 at) const { return pointVelocity(at); }
    // 1 / effective mass for an impulse along unit n at a world point.
    float inverseMassAlong(math::Vec3 at, math::Vec3 n) const;
    void translate(math::Vec3 offset);

    // A car at rest with no driver stops simulating until something touches it.
    bool asleep() const { return asleep_; }
    void wake();

    // Overlap of a circle on the ground (e.g. the warrior) with the car's footprint.
    // On overlap returns true with the push-out direction and depth.
    bool circleOverlap(float x, float z, float radius, float& normal_x, float& normal_z, float& depth) const;

    // Distance along a ray to the car's body box, or -1 when it misses.
    float rayDistance(const float origin[3], const float direction[3]) const;

    // Footprint circles (centre x, z and radius) used against buildings and other cars.
    static constexpr int kCircles = 3;
    void footprintCircle(int i, float& x, float& z, float& radius) const;

private:
    void step(const VehicleInput& input, const world::World& world, float h);
    void collideGround(const world::World& world);
    void collideBoxes(const world::World& world);
    void applyContactImpulse(math::Vec3 at, math::Vec3 normal, float restitution, float friction);
    math::Vec3 inverseInertiaTimes(math::Vec3 v) const;  // world-frame I^-1 v
    math::Vec3 pointVelocity(math::Vec3 at) const;
    void applyImpulseNow(math::Vec3 impulse, math::Vec3 at);

    VehicleSpec spec_;
    Wheel wheels_[4];
    math::Vec3 inertia_inverse_body_;  // diagonal
    math::Vec3 position_;
    math::Quat orientation_;
    math::Vec3 velocity_;
    math::Vec3 angular_velocity_;
    math::Vec3 previous_position_;
    math::Quat previous_orientation_;
    float accumulator_ = 0.0f;
    float last_impact_ = 0.0f;
    float impact_this_update_ = 0.0f;
    float upside_down_time_ = 0.0f;
    float still_time_ = 0.0f;
    bool asleep_ = false;
};

} // namespace physics

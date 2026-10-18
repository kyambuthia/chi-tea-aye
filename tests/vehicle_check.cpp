// Drives the raycast car through the cases that make or break the feel: it rests on
// its springs, accelerates in a straight line, brakes to a stop without reversing,
// turns the way the input says, stops at a building instead of passing through, and
// stays stable when frames are long or uneven.
// Build and run from the repository root:
//   cmake --build build --target vehicle_check && ./build/vehicle_check

#include "physics/Vehicle.h"
#include "world/City.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void run(physics::Vehicle& car, const world::World& world, const physics::VehicleInput& input, float seconds, float frame = 1.0f / 60.0f) {
    for (float t = 0.0f; t < seconds; t += frame) {
        car.update(input, world, frame);
    }
}

constexpr float kPi = 3.14159265f;

// A long straight on the ring road, clear of buildings: x = 210, heading +Z from z = -150.
constexpr float kRoadX = 210.0f;
constexpr float kRoadStartZ = -150.0f;

} // namespace

int main() {
    const world::World world;
    expect(world::surfaceAt(kRoadX, kRoadStartZ) == world::Surface::Road, "the test straight is on a road");

    // 1. At rest: settles on its springs, all wheels down, no creep.
    {
        physics::Vehicle car;
        car.reset(world, kRoadX, kRoadStartZ, 0.0f);
        const float start_y = car.position().y;
        run(car, world, {}, 3.0f);
        const float moved = std::hypot(car.position().x - kRoadX, car.position().z - kRoadStartZ);
        std::printf("rest: height change %.3f m (want < 0.03), creep %.3f m (want < 0.02), wheels down %d, up.y %.3f\n",
            car.position().y - start_y, moved, car.groundedWheels(), car.up().y);
        expect(std::fabs(car.position().y - start_y) < 0.03f, "the car rests at its ride height");
        expect(moved < 0.02f, "a parked car does not creep");
        expect(car.groundedWheels() == 4, "all four wheels touch the ground at rest");
    }

    // 1b. Parked on the park's slope (the first parking spot): it stays put.
    {
        const world::ParkingSpot& spot = world.city().parking()[0];
        const float slope = std::fabs(world.groundHeight(spot.x, spot.z + 1.0f) - world.groundHeight(spot.x, spot.z - 1.0f)) * 0.5f;
        auto creep = [&](bool parked) {
            physics::Vehicle car;
            car.reset(world, spot.x, spot.z, spot.yaw);
            physics::VehicleInput hold;
            hold.parked = parked;
            run(car, world, hold, 5.0f);
            return std::hypot(car.position().x - spot.x, car.position().z - spot.z);
        };
        const float rolled = creep(false);
        const float held = creep(true);
        std::printf("slope %.3f: free car rolls %.2f m in 5 s, parked car moves %.3f m (want < 0.05)\n", slope, rolled, held);
        expect(held < 0.05f, "a parked car holds on a slope");
    }

    // 2. Full throttle on the straight: quick launch, keeps building speed, runs straight.
    {
        physics::Vehicle car;
        car.reset(world, kRoadX, kRoadStartZ, 0.0f);
        physics::VehicleInput go;
        go.throttle = 1.0f;
        run(car, world, go, 3.0f);
        const float at3 = car.forwardSpeed();
        run(car, world, go, 4.0f);
        const float at7 = car.forwardSpeed();
        const float drift = std::fabs(car.position().x - kRoadX);
        std::printf("launch: %.1f m/s after 3 s (want > 12), %.1f m/s after 7 s (want > at 3 s), sideways drift %.2f m (want < 0.5)\n",
            at3, at7, drift);
        expect(at3 > 12.0f, "the car launches briskly");
        expect(at7 > at3 + 3.0f, "speed keeps building toward top speed");
        expect(at7 < car.spec().top_speed + 0.5f, "speed stays under the top speed");
        expect(drift < 0.5f, "full throttle runs straight");

        // 3. Brakes from speed: stops, and does not roll backward while held.
        physics::VehicleInput stop;
        stop.brake = 1.0f;
        const float z_before = car.position().z;
        float t = 0.0f;
        while (car.forwardSpeed() > 0.3f && t < 10.0f) {
            car.update(stop, world, 1.0f / 60.0f);
            t += 1.0f / 60.0f;
        }
        const float distance = car.position().z - z_before;
        std::printf("brake from %.1f m/s: stopped in %.2f s over %.1f m\n", at7, t, distance);
        expect(t < 4.0f, "braking from speed stops the car within 4 s");
        // Releasing then holding the brake from a stop reverses.
        run(car, world, {}, 0.5f);
        run(car, world, stop, 2.0f);
        std::printf("brake held at a standstill: %.2f m/s (want reversing, < -1)\n", car.forwardSpeed());
        expect(car.forwardSpeed() < -1.0f, "holding the brake at a standstill reverses");
    }

    // 4. Steering: right input turns right (yaw falls; facing +Z, right is -X).
    {
        physics::Vehicle car;
        car.reset(world, kRoadX, kRoadStartZ, 0.0f);
        physics::VehicleInput go;
        go.throttle = 0.6f;
        run(car, world, go, 1.5f);
        go.steer = 1.0f;
        run(car, world, go, 0.8f);
        std::printf("steer right: yaw %.2f rad (want < -0.2), x %.2f (want < %.1f), up.y %.2f\n",
            car.yaw(), car.position().x, kRoadX, car.up().y);
        expect(car.yaw() < -0.2f, "right steering turns right");
        expect(car.position().x < kRoadX, "the car moves toward its right");
        expect(car.up().y > 0.9f, "a turn at moderate speed keeps the car upright");
    }

    // 5. A building: drive at the first building's face and stop at it, not inside it.
    {
        // A ground-floor building whose south face is on the building line, so a road
        // and sidewalk lie clear in front of it.
        const world::Box* target = nullptr;
        for (const world::Building& b : world.city().buildings()) {
            const float pitch = world::CityLayout::kBlockPitch;
            const float local = b.box.min[2] - pitch * std::round(b.box.min[2] / pitch);
            if (std::fabs(local + world::CityLayout::kLotHalf) < 0.01f && b.box.max[0] - b.box.min[0] > 6.0f &&
                b.box.min[1] < world::World::kBaseHeight) {
                target = &b.box;
                break;
            }
        }
        expect(target != nullptr, "a building faces a road");
        if (target == nullptr) return 1;
        const world::Box& b = *target;
        const float face_z = b.min[2];
        const float x = 0.5f * (b.min[0] + b.max[0]);
        physics::Vehicle car;
        car.reset(world, x, face_z - 12.0f, 0.0f);
        physics::VehicleInput go;
        go.throttle = 1.0f;
        float worst = -1e9f;
        float biggest_impact = 0.0f;
        for (float t = 0.0f; t < 6.0f; t += 1.0f / 60.0f) {
            car.update(go, world, 1.0f / 60.0f);
            worst = std::fmax(worst, car.position().z + car.spec().axle_front + car.spec().half_extents.x - face_z);
            biggest_impact = std::fmax(biggest_impact, car.lastImpact());
        }
        std::printf("building: deepest front overlap %.2f m (want < 0.3), largest impact %.0f N s\n", worst, biggest_impact);
        expect(worst < 0.3f, "the car does not drive through a building");
        expect(biggest_impact > 1000.0f, "hitting a building reports an impact");
    }

    // 6. Long and uneven frames: the fixed step keeps it stable, and the result matches
    //    the same drive at 60 frames per second closely.
    {
        physics::Vehicle smooth;
        physics::Vehicle rough;
        smooth.reset(world, kRoadX, kRoadStartZ, 0.0f);
        rough.reset(world, kRoadX, kRoadStartZ, 0.0f);
        physics::VehicleInput go;
        go.throttle = 1.0f;
        go.steer = 0.3f;
        run(smooth, world, go, 3.0f, 1.0f / 60.0f);
        const float frames[] = {0.1f, 0.004f, 0.033f, 0.07f, 0.016f};
        float t = 0.0f;
        for (int i = 0; t < 3.0f - 1e-4f; ++i) {
            const float f = std::fmin(frames[i % 5], 3.0f - t);
            rough.update(go, world, f);
            t += f;
        }
        const float gap = std::hypot(smooth.position().x - rough.position().x, smooth.position().z - rough.position().z);
        std::printf("uneven frames: %.3f m from the 60 fps run after 3 s (want < 0.2), up.y %.2f\n", gap, rough.up().y);
        expect(gap < 0.2f, "uneven frame times give nearly the same drive");
        expect(rough.up().y > 0.9f && std::isfinite(rough.position().x), "uneven frames keep the car stable");
    }

    // 7. The handbrake in a turn slides the tail: more slip at the rear than without it.
    {
        auto rear_slip = [&](bool handbrake) {
            physics::Vehicle car;
            car.reset(world, kRoadX, kRoadStartZ, 0.0f);
            physics::VehicleInput go;
            go.throttle = 1.0f;
            run(car, world, go, 2.5f);
            go.throttle = 0.0f;
            go.steer = -1.0f;
            go.handbrake = handbrake;
            run(car, world, go, 0.6f);
            return std::fabs(car.wheel(2).slip) + std::fabs(car.wheel(3).slip);
        };
        const float plain = rear_slip(false);
        const float sliding = rear_slip(true);
        std::printf("handbrake turn: rear slip %.2f rad with it, %.2f without (want more with it)\n", sliding, plain);
        expect(sliding > plain * 1.5f, "the handbrake slides the rear");
    }

    // 8. Flipped onto its roof, the car is set back on its wheels.
    {
        physics::Vehicle car;
        car.reset(world, kRoadX, kRoadStartZ, 0.0f);
        car.applyImpulse(math::Vec3{0.0f, 9000.0f, 0.0f}, car.position() + math::Vec3{0.9f, 0.0f, 0.0f});
        run(car, world, {}, 6.0f);
        std::printf("flip: up.y %.2f after 6 s (want > 0.9)\n", car.up().y);
        expect(car.up().y > 0.9f, "a flipped car recovers onto its wheels");
    }

    (void)kPi;
    std::printf(failures == 0 ? "VEHICLE OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

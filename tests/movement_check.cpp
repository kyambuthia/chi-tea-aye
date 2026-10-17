// Drives the gameplay state with fixed-step input and checks movement: the player
// goes where the input points on screen, taps register, starting and stopping are
// quick, the world edge holds, and the clip matches the motion after an orbit.
// Build and run from the repository root:
//   cmake --build build --target movement_check && ./build/movement_check

#include "app/InputState.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kStep = 1.0f / 60.0f;

struct Sim {
    gameplay::GameState game;
    physics::Physics physics;
    world::World world;
    app::InputState input;

    // Runs whole frames. Taps are one-frame events, as the app delivers them.
    void step(float seconds) {
        const int frames = static_cast<int>(seconds / kStep + 0.5f);
        for (int i = 0; i < frames; ++i) {
            game.update(input, physics, world, kStep);
            input.look_dx = 0.0f;
            input.look_dy = 0.0f;
            input.tapped = app::KeyState{};
        }
    }

    float distanceFromOrigin() const {
        return std::sqrt(game.playerX() * game.playerX() + game.playerZ() * game.playerZ());
    }
};

// Heading (radians, measured from +Z toward +X) of a ground vector.
float headingOf(float x, float z) {
    return std::atan2(x, z);
}

float angularError(float a, float b) {
    float d = std::fmod(a - b + kPi, 2.0f * kPi);
    if (d < 0.0f) d += 2.0f * kPi;
    return std::fabs(d - kPi) * 180.0f / kPi;
}

// The ground direction a key points on screen, given the camera azimuth.
void screenDirection(float azimuth, float& forward_x, float& forward_z, float& right_x, float& right_z) {
    forward_x = -std::sin(azimuth);
    forward_z = -std::cos(azimuth);
    right_x = -forward_z;
    right_z = forward_x;
}

} // namespace

int main() {
    int failures = 0;

    // 1. Forward from the default camera: the player moves straight ahead.
    {
        Sim sim;
        sim.input.keys.w = true;
        sim.step(1.5f);
        const float error = angularError(headingOf(sim.game.playerX(), sim.game.playerZ()), 0.0f);
        std::printf("forward: moved heading %.1f deg off the camera forward (want < 3), speed %.2f m/s\n",
            error, sim.distanceFromOrigin() / 1.5f);
        if (error > 3.0f) ++failures;
    }

    // 2. Strafe right: the body turns right and walks there. Measured after the
    //    turn (the first half second is the turn), so the motion and the facing
    //    both end up on screen right.
    {
        Sim sim;
        sim.input.keys.d = true;
        sim.step(0.5f);
        const float x0 = sim.game.playerX();
        const float z0 = sim.game.playerZ();
        sim.step(0.5f);
        float fx, fz, rx, rz;
        screenDirection(sim.game.cameraAzimuth(), fx, fz, rx, rz);
        const float moved_error = angularError(headingOf(sim.game.playerX() - x0, sim.game.playerZ() - z0), headingOf(rx, rz));
        const float facing_error = angularError(sim.game.playerYaw(), headingOf(rx, rz));
        std::printf("right: moved %.1f deg off screen right (want < 3), facing %.1f deg off it (want < 3)\n",
            moved_error, facing_error);
        if (moved_error > 3.0f || facing_error > 3.0f) ++failures;
    }

    // 3. Orbit a quarter turn, let the camera settle, then walk forward: the player
    //    goes where the camera now points, and the clip is Walk, not a sideways one.
    {
        Sim sim;
        sim.input.orbit_drag = true;
        sim.input.look_dx = -261.0f;  // about a quarter turn at 0.006 rad per pixel
        sim.step(1.0f / 60.0f);
        sim.input.orbit_drag = false;
        sim.step(2.0f);

        sim.input.keys.w = true;
        sim.step(0.5f);
        const float x0 = sim.game.playerX();
        const float z0 = sim.game.playerZ();
        sim.step(1.5f);
        float fx, fz, rx, rz;
        screenDirection(sim.game.cameraAzimuth(), fx, fz, rx, rz);
        const float moved = headingOf(sim.game.playerX() - x0, sim.game.playerZ() - z0);
        const float error = angularError(moved, headingOf(fx, fz));
        const bool walking = std::strcmp(sim.game.clipName(), "Walk") == 0;
        std::printf("orbit then forward: moved %.1f deg off the new screen forward (want < 5), clip %s\n",
            error, sim.game.clipName());
        if (error > 5.0f || !walking) ++failures;
    }

    // 4. Taps: a press and release inside one frame still moves the player, and
    //    movement follows the press time (a 40 ms tap moves about ten times a 5 ms one).
    {
        Sim sim;
        for (int i = 0; i < 10; ++i) {
            sim.input.tapped.w = true;  // pressed and released within one frame
            sim.step(kStep);
            sim.step(0.2f);
        }
        const float short_taps = sim.distanceFromOrigin();

        Sim longer;
        for (int i = 0; i < 10; ++i) {
            longer.input.tapped.w = true;
            longer.input.keys.w = true;
            longer.step(0.04f);
            longer.input.keys.w = false;
            longer.step(0.2f);
        }
        const float long_taps = longer.distanceFromOrigin();
        std::printf("10 taps of one frame: %.3f m; 10 taps of 40 ms: %.3f m (full speed would be 0.66 m)\n",
            short_taps, long_taps);
        if (short_taps <= 0.05f || long_taps <= 2.0f * short_taps) ++failures;
    }

    // 5. Start and stop: full walk speed within 0.15 s, and at most 0.10 m of slide.
    {
        Sim sim;
        sim.input.keys.w = true;
        sim.step(0.15f);
        const float speed_at_start = sim.distanceFromOrigin() / 0.15f;
        sim.step(1.0f);
        sim.input.keys.w = false;
        const float before_stop = sim.distanceFromOrigin();
        sim.step(1.0f);
        const float slide = sim.distanceFromOrigin() - before_stop;
        std::printf("start: average %.2f m/s over the first 0.15 s (want > 1.3); stop slide %.3f m (want < 0.10)\n",
            speed_at_start, slide);
        if (speed_at_start < 1.3f || slide > 0.10f) ++failures;
    }

    // 6. The world edge holds: walking into it stops the player inside the terrain.
    {
        Sim sim;
        sim.input.keys.w = true;
        sim.step(30.0f);
        const float edge = world::World::kHalfSize - 0.5f;
        std::printf("edge: stopped at z = %.2f (want %.2f)\n", sim.game.playerZ(), edge);
        if (std::fabs(sim.game.playerZ() - edge) > 0.01f) ++failures;
    }

    std::printf(failures == 0 ? "MOVEMENT OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

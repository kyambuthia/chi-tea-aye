// Drives the gameplay state with fixed-step input and checks that the player
// moves where the input points on screen: straight ahead of the camera, after
// strafing, and after the camera is orbited with the mouse.
// Build and run from the repository root:
//   cmake --build build --target movement_check && ./build/movement_check

#include "app/InputState.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kStep = 1.0f / 60.0f;

struct Sim {
    gameplay::GameState game;
    physics::Physics physics;
    world::World world;
    app::InputState input;
    float last_x = 0.0f;
    float last_z = 0.0f;

    void step(float seconds) {
        const int frames = static_cast<int>(seconds / kStep + 0.5f);
        for (int i = 0; i < frames; ++i) {
            last_x = game.playerX();
            last_z = game.playerZ();
            game.update(input, physics, world, kStep);
            input.look_dx = 0.0f;
            input.look_dy = 0.0f;
        }
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
        const float moved = headingOf(sim.game.playerX(), sim.game.playerZ());
        // Camera is behind the player at rest, so screen forward is the facing, +Z.
        const float error = angularError(moved, 0.0f);
        std::printf("forward: moved heading %.1f deg off the camera forward (want < 3), speed %.2f m/s\n",
            error, std::sqrt(sim.game.playerX() * sim.game.playerX() + sim.game.playerZ() * sim.game.playerZ()) / 1.5f);
        if (error > 3.0f) ++failures;
    }

    // 2. Strafe right: movement follows the camera's right, facing is kept.
    {
        Sim sim;
        sim.input.keys.d = true;
        sim.step(1.0f);
        float fx, fz, rx, rz;
        screenDirection(sim.game.cameraAzimuth(), fx, fz, rx, rz);
        const float error = angularError(headingOf(sim.game.playerX(), sim.game.playerZ()), headingOf(rx, rz));
        const float facing_drift = angularError(sim.game.playerYaw(), 0.0f);
        std::printf("strafe right: moved %.1f deg off screen right (want < 3), facing drift %.1f deg (want < 3)\n",
            error, facing_drift);
        if (error > 3.0f || facing_drift > 3.0f) ++failures;
    }

    // 3. Orbit the camera a quarter turn, let it settle, then walk forward: the
    //    player must go where the camera now points, not where it pointed before.
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
        std::printf("orbit then forward: moved %.1f deg off the new screen forward (want < 5), facing %.1f deg from it\n",
            error, angularError(sim.game.playerYaw(), headingOf(fx, fz)));
        if (error > 5.0f) ++failures;
    }

    std::printf(failures == 0 ? "MOVEMENT OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

#include "physics/Physics.h"

#include "app/InputState.h"
#include "world/World.h"

namespace physics {

void Physics::init() {}

void Physics::integratePlayer(const app::InputState& input, const world::World& world, float delta_time, float& x, float& y) const {
    (void)world;

    // Matched to the planted-foot speed of the Walk and Run clips so feet do not slide.
    const float speed = input.keys.shift ? 4.0f : 1.65f;
    float dx = 0.0f;
    float dy = 0.0f;

    if (input.keys.a) {
        dx -= 1.0f;
    }
    if (input.keys.d) {
        dx += 1.0f;
    }
    if (input.keys.w) {
        dy += 1.0f;
    }
    if (input.keys.s) {
        dy -= 1.0f;
    }

    x += dx * speed * delta_time;
    y += dy * speed * delta_time;
}

void Physics::cleanup() {}

} // namespace physics

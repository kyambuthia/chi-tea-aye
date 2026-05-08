#include "physics/Physics.h"

#include "app/InputState.h"
#include "world/World.h"

namespace physics {

void Physics::init() {}

void Physics::integratePlayer(const app::InputState& input, const world::World& world, float delta_time, float& x, float& y) const {
    (void)world;

    constexpr float speed = 1.5f;
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

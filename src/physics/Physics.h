#pragma once

namespace app {
struct InputState;
}

namespace world {
class World;
}

namespace physics {

class Physics {
public:
    void init();
    void integratePlayer(const app::InputState& input, const world::World& world, float delta_time, float& x, float& y) const;
    void cleanup();
};

} // namespace physics

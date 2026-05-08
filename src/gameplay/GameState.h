#pragma once

namespace app {
struct InputState;
}

namespace physics {
class Physics;
}

namespace world {
class World;
}

namespace gameplay {

class GameState {
public:
    void init();
    void update(const app::InputState& input, physics::Physics& physics, const world::World& world, float delta_time);
    void cleanup();

    float playerX() const;
    float playerY() const;

private:
    float player_x_ = 0.0f;
    float player_y_ = 0.0f;
};

} // namespace gameplay

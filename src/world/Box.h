#pragma once

namespace world {

// An axis-aligned box standing on the terrain: a wall, a building, or another obstacle.
struct Box {
    float min[3];
    float max[3];
};

} // namespace world

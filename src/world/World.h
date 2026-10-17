#pragma once

#include "math/Mat4.h"

#include <vector>

namespace world {

// An axis-aligned box standing on the terrain: a wall or other obstacle.
struct Box {
    float min[3];
    float max[3];
};

class World {
public:
    // Height of the flat base ground; the terrain varies around it.
    static constexpr float kBaseHeight = -0.52f;
    // The terrain spans [-kHalfSize, kHalfSize] on both ground axes.
    static constexpr float kHalfSize = 20.0f;

    void init();
    void cleanup();

    // Terrain height (meters, +Y up) at a ground position.
    float groundHeight(float x, float z) const;

    // Placement for a character standing at (x, z): on the ground, tilted to the
    // terrain normal. The character's yaw is applied after this.
    math::Mat4 groundFrame(float x, float z) const;

    // Walls the warrior cannot pass, and that block shots and the camera.
    const std::vector<Box>& walls() const;
};

} // namespace world

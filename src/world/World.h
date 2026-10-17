#pragma once

namespace world {

class World {
public:
    // Height of the flat base ground; the terrain varies around it.
    static constexpr float kBaseHeight = -0.52f;

    void init();
    void cleanup();

    // Terrain height (meters, +Y up) at a ground position.
    float groundHeight(float x, float z) const;
};

} // namespace world

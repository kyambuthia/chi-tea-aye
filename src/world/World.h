#pragma once

#include "math/Mat4.h"
#include "world/Box.h"
#include "world/City.h"
#include "world/SpatialGrid.h"

#include <vector>

namespace world {

// A solid box with what the renderer needs to draw it.
struct SolidStyle {
    float color[3];
    float seed;     // 0..1 facade variation
    bool building;  // buildings get windows; walls are plain
};

class World {
public:
    // Height of the flat base ground; the park's terrain varies around it.
    static constexpr float kBaseHeight = -0.52f;
    // The park's rolling terrain spans [-kHalfSize, kHalfSize] on both ground axes.
    static constexpr float kHalfSize = 20.0f;
    // World boundary; the warrior and cars stop just inside.
    static constexpr float kCityHalfSize = CityLayout::kHalfExtent;
    static constexpr uint32_t kCitySeed = 0x6c69626cu;

    // The playable scene can use open terrain while city simulations still build
    // their deterministic layout.
    explicit World(bool city_enabled = true);

    void init();
    void cleanup();

    // Terrain height (metres, +Y up) at a ground position. Hills in the park, flat streets.
    float groundHeight(float x, float z) const;

    // Placement for a character standing at (x, z): on the ground, tilted to the
    // terrain normal. The character's yaw is applied after this.
    math::Mat4 groundFrame(float x, float z) const;

    // Every solid box: the practice walls first, then the buildings. They stop the
    // warrior and cars, and block shots and the camera.
    const std::vector<Box>& walls() const { return solids_; }
    // Draw style for each box in walls(), in the same order.
    const std::vector<SolidStyle>& wallStyles() const { return styles_; }
    // How many of walls() are the practice range's low walls (the rest are buildings).
    int practiceWallCount() const { return practice_walls_; }

    const City& city() const { return city_; }
    bool cityEnabled() const { return city_enabled_; }

    // Indices into walls() of boxes whose footprint overlaps the rectangle. Appends to out.
    void boxesNear(float min_x, float min_z, float max_x, float max_z, std::vector<int>& out) const;

    // Distance along a ray (unit direction) to the first solid box, or max_distance
    // when none is hit within it. hit_index, when given, gets the box (or -1).
    float raycast(const float origin[3], const float direction[3], float max_distance, int* hit_index = nullptr) const;

private:
    bool city_enabled_ = true;
    City city_;
    std::vector<Box> solids_;
    std::vector<SolidStyle> styles_;
    int practice_walls_ = 0;
    SpatialGrid grid_;
};

} // namespace world

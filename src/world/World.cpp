#include "world/World.h"

#include <cmath>
#include <vector>

namespace world {
namespace {

// A low wall across the path north of the start point, and a short one to the east.
// The warrior can crouch behind either, or stand and shoot over the low one.
// Heights are absolute, and the bottoms sit below the lowest ground. The low wall's
// top (0.9) is between the crouched shot line (0.7) and the standing one (1.1), so
// shots from a crouch are blocked and standing shots pass over. The other wall
// (1.2) blocks both.
const std::vector<Box> kWalls = {
    {{-7.0f, -1.0f, 4.6f}, {1.0f, 0.9f, 5.0f}},
    {{6.0f, -1.0f, 3.0f}, {7.0f, 1.2f, 4.0f}},
};

} // namespace

const std::vector<Box>& World::walls() const {
    return kWalls;
}

void World::init() {}

void World::cleanup() {}

float World::groundHeight(float x, float z) const {
    // Two overlapping swells: gentle slopes and bumps, flat at the origin.
    return kBaseHeight + 0.18f * std::sin(0.9f * x) * std::cos(0.7f * z) +
        0.12f * std::sin(0.37f * x + 1.3f * z);
}

math::Mat4 World::groundFrame(float x, float z) const {
    const float eps = 0.05f;
    const float slope_x = (groundHeight(x + eps, z) - groundHeight(x - eps, z)) / (2.0f * eps);
    const float slope_z = (groundHeight(x, z + eps) - groundHeight(x, z - eps)) / (2.0f * eps);
    const float len = std::sqrt(slope_x * slope_x + 1.0f + slope_z * slope_z);
    const float normal[3] = {-slope_x / len, 1.0f / len, -slope_z / len};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    return math::multiply(math::translation(x, groundHeight(x, z), z), math::rotationBetween(up, normal));
}

} // namespace world

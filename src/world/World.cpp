#include "world/World.h"

#include <cmath>

namespace world {

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

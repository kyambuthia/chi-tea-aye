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

} // namespace world

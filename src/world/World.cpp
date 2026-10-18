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
const Box kPracticeWalls[] = {
    {{-7.0f, -1.0f, 4.6f}, {1.0f, 0.9f, 5.0f}},
    {{6.0f, -1.0f, 3.0f}, {7.0f, 1.2f, 4.0f}},
};
constexpr float kWallColor[3] = {0.46f, 0.43f, 0.39f};

// Grid cells about the size of a lot keep each query to a handful of boxes.
constexpr float kGridCell = 15.0f;

// The park's hills fade out over its last few metres, so its edge meets the flat street.
constexpr float kHillsFullTo = 14.0f;
constexpr float kHillsGoneAt = 19.5f;

float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

World::World() {
    city_.generate(kCitySeed);
    for (const Box& wall : kPracticeWalls) {
        solids_.push_back(wall);
        styles_.push_back({{kWallColor[0], kWallColor[1], kWallColor[2]}, 0.0f, false});
    }
    practice_walls_ = static_cast<int>(solids_.size());
    for (const Building& b : city_.buildings()) {
        solids_.push_back(b.box);
        styles_.push_back({{b.color[0], b.color[1], b.color[2]}, b.seed, true});
    }
    grid_.build(solids_, kCityHalfSize, kGridCell);
}

void World::init() {}

void World::cleanup() {}

float World::groundHeight(float x, float z) const {
    // Two overlapping swells inside the park: gentle slopes and bumps, flat at the origin.
    const float edge = std::fmax(std::fabs(x), std::fabs(z));
    const float hills = 1.0f - smoothstep(kHillsFullTo, kHillsGoneAt, edge);
    if (hills <= 0.0f) {
        return kBaseHeight;
    }
    return kBaseHeight + hills * (0.18f * std::sin(0.9f * x) * std::cos(0.7f * z) + 0.12f * std::sin(0.37f * x + 1.3f * z));
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

void World::boxesNear(float min_x, float min_z, float max_x, float max_z, std::vector<int>& out) const {
    grid_.query(min_x, min_z, max_x, max_z, out);
}

float World::raycast(const float origin[3], const float direction[3], float max_distance, int* hit_index) const {
    return grid_.raycast(solids_, origin, direction, max_distance, hit_index);
}

} // namespace world

#include "world/City.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>

namespace world {
namespace {

using L = CityLayout;

// Small, fast, deterministic generator (PCG-style hash of a counter), so the city is
// the same on every platform and every run.
struct Rng {
    uint32_t state;
    uint32_t next() {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int pick(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

uint32_t hashBlock(uint32_t seed, int bx, int bz) {
    uint32_t h = seed ^ 0x9e3779b9u;
    h ^= static_cast<uint32_t>(bx + 1000) * 0x85ebca6bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(bz + 1000) * 0xc2b2ae35u;
    h *= 0x27d4eb2fu;
    return h ^ (h >> 15);
}

// Facade palette: brick, brownstone, concrete, sandstone, dark glass, pale render.
constexpr float kPalette[][3] = {
    {0.46f, 0.25f, 0.20f}, {0.36f, 0.24f, 0.19f}, {0.52f, 0.51f, 0.49f}, {0.62f, 0.55f, 0.43f},
    {0.22f, 0.27f, 0.32f}, {0.70f, 0.68f, 0.63f}, {0.40f, 0.38f, 0.36f}, {0.55f, 0.36f, 0.27f},
};
constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

constexpr float kFloorHeight = 3.2f;
constexpr float kAlley = 1.5f;  // half the gap kept between neighbouring buildings

float localCoord(float v) {
    return v - L::kBlockPitch * std::round(v / L::kBlockPitch);
}

} // namespace

Surface surfaceAt(float x, float z) {
    if (std::fabs(x) > L::kHalfExtent || std::fabs(z) > L::kHalfExtent) {
        return Surface::Outside;
    }
    const float lx = std::fabs(localCoord(x));
    const float lz = std::fabs(localCoord(z));
    if (lx > L::kBlockHalf || lz > L::kBlockHalf) {
        return Surface::Road;
    }
    if (lx > L::kLotHalf || lz > L::kLotHalf) {
        return Surface::Sidewalk;
    }
    const bool park = std::round(x / L::kBlockPitch) == 0.0f && std::round(z / L::kBlockPitch) == 0.0f;
    return park ? Surface::Park : Surface::Lot;
}

float nearestRoadCentre(float v) {
    const float half = 0.5f * L::kBlockPitch;
    return half + L::kBlockPitch * std::round((v - half) / L::kBlockPitch);
}

void City::generate(uint32_t seed) {
    buildings_.clear();
    lamps_.clear();
    parking_.clear();
    for (int bz = -L::kBlockRadius; bz <= L::kBlockRadius; ++bz) {
        for (int bx = -L::kBlockRadius; bx <= L::kBlockRadius; ++bx) {
            generateBlock(bx, bz, seed);
        }
    }

    // Parked cars: one in the park beside the start point, so a car is in view at once,
    // and the rest in the curb lane on their right (traffic drives on the right).
    // Facing +Z the right side is -X; facing +X it is +Z.
    constexpr float kPi = 3.14159265f;
    const float curb = L::kRoadWidth * 0.5f - L::kLaneWidth * 0.5f;  // 6 m from the centreline
    const float road = 0.5f * L::kBlockPitch;                          // the roads around the park
    parking_.push_back({-12.0f, 0.0f, 0.0f});
    parking_.push_back({road - curb, 6.0f, 0.0f});
    parking_.push_back({road + curb, -12.0f, kPi});
    parking_.push_back({-road + curb, 10.0f, kPi});
    parking_.push_back({4.0f, road + curb, 0.5f * kPi});
    parking_.push_back({-10.0f, -road - curb, -0.5f * kPi});
    parking_.push_back({road - curb, 70.0f, 0.0f});
    parking_.push_back({3.0f * road - curb, -40.0f, 0.0f});
}

void City::generateBlock(int bx, int bz, uint32_t seed) {
    const float cx = bx * L::kBlockPitch;
    const float cz = bz * L::kBlockPitch;

    // Street lamps every ~15 m along each curb, 0.8 m in from it.
    const float lamp_line = L::kBlockHalf - 0.8f;
    for (int side = 0; side < 4; ++side) {
        for (float t = -15.0f; t <= 15.01f; t += 15.0f) {
            float x = cx;
            float z = cz;
            if (side == 0) { x += lamp_line; z += t; }
            if (side == 1) { x -= lamp_line; z += t; }
            if (side == 2) { z += lamp_line; x += t; }
            if (side == 3) { z -= lamp_line; x += t; }
            lamps_.push_back({{x - 0.09f, World::kBaseHeight, z - 0.09f}, {x + 0.09f, World::kBaseHeight + 5.5f, z + 0.09f}});
        }
    }

    if (bx == 0 && bz == 0) {
        return;  // the park
    }

    Rng rng{hashBlock(seed, bx, bz)};
    // Downtown sits near the park: taller buildings near the centre, falling off outward.
    const float distance = std::sqrt(cx * cx + cz * cz);
    const float downtown = std::exp(-distance / 110.0f);

    // Split the block into a grid of lots with jittered divisions.
    const int nx = 1 + rng.pick(3);
    const int nz = 1 + rng.pick(3);
    float xs[4];
    float zs[4];
    xs[0] = cx - L::kLotHalf;
    xs[nx] = cx + L::kLotHalf;
    zs[0] = cz - L::kLotHalf;
    zs[nz] = cz + L::kLotHalf;
    const float span = 2.0f * L::kLotHalf;
    for (int i = 1; i < nx; ++i) xs[i] = xs[0] + span * (static_cast<float>(i) / nx + rng.range(-0.08f, 0.08f));
    for (int i = 1; i < nz; ++i) zs[i] = zs[0] + span * (static_cast<float>(i) / nz + rng.range(-0.08f, 0.08f));

    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (rng.uniform() < 0.1f) {
                continue;  // an empty lot: a plaza or a car park
            }
            // Alleys between lots; at the sidewalk the facade sits on the building line.
            const float x0 = xs[i] + (i > 0 ? kAlley : 0.0f);
            const float x1 = xs[i + 1] - (i < nx - 1 ? kAlley : 0.0f);
            const float z0 = zs[j] + (j > 0 ? kAlley : 0.0f);
            const float z1 = zs[j + 1] - (j < nz - 1 ? kAlley : 0.0f);
            if (x1 - x0 < 4.0f || z1 - z0 < 4.0f) {
                continue;
            }
            const float base_height = rng.range(8.0f, 16.0f) + downtown * rng.range(0.0f, 75.0f);
            const float height = std::max(2.0f, std::round(base_height / kFloorHeight)) * kFloorHeight;
            Building b{};
            // Bottoms sit below the lowest ground so no gap shows on slopes.
            b.box = {{x0, World::kBaseHeight - 1.0f, z0}, {x1, World::kBaseHeight + height, z1}};
            const float* c = kPalette[rng.pick(kPaletteSize)];
            const float tint = rng.range(0.9f, 1.1f);
            b.color[0] = c[0] * tint;
            b.color[1] = c[1] * tint;
            b.color[2] = c[2] * tint;
            b.seed = rng.uniform();
            buildings_.push_back(b);

            // Tall buildings may step back: a narrower tower on top.
            if (height > 30.0f && rng.uniform() < 0.55f) {
                const float inset_x = (x1 - x0) * rng.range(0.15f, 0.28f);
                const float inset_z = (z1 - z0) * rng.range(0.15f, 0.28f);
                const float top = std::round(height * rng.range(0.25f, 0.5f) / kFloorHeight) * kFloorHeight;
                Building t = b;
                t.box = {{x0 + inset_x, b.box.max[1] - 0.01f, z0 + inset_z},
                    {x1 - inset_x, b.box.max[1] + std::max(kFloorHeight, top), z1 - inset_z}};
                t.seed = rng.uniform();
                buildings_.push_back(t);
            }
        }
    }
}

} // namespace world

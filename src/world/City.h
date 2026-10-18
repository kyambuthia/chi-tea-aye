#pragma once

#include "world/Box.h"

#include <cstdint>
#include <vector>

namespace world {

// The city grid: square blocks with roads between them and a ring road around the
// outside. The centre block is the park (the practice range). Units are metres.
//
// Block centres sit at multiples of kBlockPitch; road centrelines at kBlockPitch / 2
// plus multiples of kBlockPitch. Each road has two 4 m lanes in each direction.
struct CityLayout {
    static constexpr int kBlockRadius = 3;          // blocks -3..3 on each axis
    static constexpr float kBlockPitch = 60.0f;
    static constexpr float kRoadWidth = 16.0f;
    static constexpr float kSidewalk = 3.0f;
    static constexpr float kLaneWidth = 4.0f;
    static constexpr float kBlockHalf = 0.5f * (kBlockPitch - kRoadWidth);  // 22: block edge (curb)
    static constexpr float kLotHalf = kBlockHalf - kSidewalk;              // 19: building line
    // The outer edge of the ring road.
    static constexpr float kHalfExtent = kBlockRadius * kBlockPitch + kBlockHalf + kRoadWidth;
};

enum class Surface { Road, Sidewalk, Lot, Park, Outside };

// What the ground is at a point. Shared by gameplay and (mirrored in GLSL) the road shader.
Surface surfaceAt(float x, float z);

// The nearest road centreline coordinate to v (on one axis).
float nearestRoadCentre(float v);

struct Building {
    Box box;
    float color[3];
    float seed;  // 0..1, varies the facade (window pattern, lit windows)
};

// A place a car is parked: centre on the ground, and its heading (radians about +Y, 0 faces +Z).
struct ParkingSpot {
    float x;
    float z;
    float yaw;
};

class City {
public:
    // Deterministic: the same seed builds the same city.
    void generate(uint32_t seed);

    const std::vector<Building>& buildings() const { return buildings_; }
    // Street lamps: thin render-only boxes along the sidewalks.
    const std::vector<Box>& lamps() const { return lamps_; }
    const std::vector<ParkingSpot>& parking() const { return parking_; }

private:
    void generateBlock(int bx, int bz, uint32_t seed);

    std::vector<Building> buildings_;
    std::vector<Box> lamps_;
    std::vector<ParkingSpot> parking_;
};

} // namespace world

#pragma once

#include <array>

namespace world {
class World;
}

namespace gameplay {

// A ray with a unit-length direction.
struct Ray {
    float origin[3];
    float direction[3];
};

// A cube target standing on the ground. A hit knocks it down; it stands again
// after a while.
struct Target {
    float center[3];  // the middle of the box, so it stands on the ground at center - height / 2
    float size;       // width and depth, metres
    float height;     // metres
    bool standing;
    float time_down;  // seconds since it was knocked down
};

// The shooting range: targets in front of the spawn point, and the shots fired at them.
class Targets {
public:
    static constexpr int kCount = 6;
    static constexpr float kRange = 80.0f;       // metres: farther targets are out of reach
    static constexpr float kRespawnTime = 3.0f;  // seconds a target stays down
    static constexpr float kFallTime = 0.35f;    // seconds a hit target takes to tip over

    // Places the targets on the terrain and resets the score.
    void place(const world::World& world);
    void update(float delta_time);

    // One shot along the ray. Knocks down the nearest standing target it hits.
    // Returns true on a hit.
    // max_distance is how far the shot can reach, e.g. up to the first wall it meets.
    // impact_distance, when given, receives where the shot ended: the hit, or max_distance.
    bool shoot(const Ray& ray, float max_distance = kRange, float* impact_distance = nullptr);

    // Knocks down every standing target whose footprint a ground circle overlaps (a car
    // driving through). Returns how many fell. These do not count as shots or hits.
    int knockDownInCircle(float x, float z, float radius);

    const std::array<Target, kCount>& targets() const { return targets_; }
    int shots() const { return shots_; }
    int hits() const { return hits_; }

private:
    std::array<Target, kCount> targets_{};
    int shots_ = 0;
    int hits_ = 0;
};

// Distance along the ray to an axis-aligned box, or -1 when the ray misses it.
// A ray that starts inside the box reports zero.
float rayBoxDistance(const Ray& ray, const float box_min[3], const float box_max[3]);

} // namespace gameplay

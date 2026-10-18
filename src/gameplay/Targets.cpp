#include "gameplay/Targets.h"

#include "math/Geometry.h"
#include "world/World.h"

#include <cmath>
#include <limits>

namespace gameplay {
namespace {

// Ground positions (x, z) in front of the spawn point, in two rows and one row farther back.
constexpr float kLayout[Targets::kCount][2] = {
    {-6.0f, 8.0f}, {-2.0f, 10.0f}, {2.0f, 8.0f}, {6.0f, 10.0f}, {-4.0f, 14.0f}, {4.0f, 14.0f},
};
// Pop-up targets, two metres tall, so a level shot at chest height hits them.
constexpr float kTargetWidth = 0.8f;
constexpr float kTargetHeight = 2.0f;

} // namespace

void Targets::place(const world::World& world) {
    enabled_ = true;
    for (int i = 0; i < kCount; ++i) {
        Target& target = targets_[i];
        target.size = kTargetWidth;
        target.height = kTargetHeight;
        target.center[0] = kLayout[i][0];
        target.center[2] = kLayout[i][1];
        target.center[1] = world.groundHeight(target.center[0], target.center[2]) + kTargetHeight * 0.5f;
        target.standing = true;
        target.time_down = 0.0f;
    }
    shots_ = 0;
    hits_ = 0;
}

void Targets::update(float delta_time) {
    if (!enabled_) return;
    for (Target& target : targets_) {
        if (target.standing) {
            continue;
        }
        target.time_down += delta_time;
        if (target.time_down >= kRespawnTime) {
            target.standing = true;
            target.time_down = 0.0f;
        }
    }
}

bool Targets::shoot(const Ray& ray, float max_distance, float* impact_distance) {
    if (!enabled_) {
        if (impact_distance != nullptr) *impact_distance = max_distance;
        return false;
    }
    ++shots_;
    if (impact_distance != nullptr) {
        *impact_distance = max_distance;
    }
    int nearest = -1;
    float nearest_distance = max_distance;
    for (int i = 0; i < kCount; ++i) {
        const Target& target = targets_[i];
        if (!target.standing) {
            continue;
        }
        const float half_width = target.size * 0.5f;
        const float half_height = target.height * 0.5f;
        const float box_min[3] = {target.center[0] - half_width, target.center[1] - half_height, target.center[2] - half_width};
        const float box_max[3] = {target.center[0] + half_width, target.center[1] + half_height, target.center[2] + half_width};
        const float distance = rayBoxDistance(ray, box_min, box_max);
        if (distance >= 0.0f && distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    if (nearest < 0) {
        return false;
    }
    targets_[nearest].standing = false;
    targets_[nearest].time_down = 0.0f;
    ++hits_;
    if (impact_distance != nullptr) {
        *impact_distance = nearest_distance;
    }
    return true;
}

int Targets::knockDownInCircle(float x, float z, float radius) {
    if (!enabled_) return 0;
    int fallen = 0;
    for (Target& target : targets_) {
        if (!target.standing) {
            continue;
        }
        const float half = target.size * 0.5f;
        const float lo[3] = {target.center[0] - half, 0.0f, target.center[2] - half};
        const float hi[3] = {target.center[0] + half, 0.0f, target.center[2] + half};
        float nx = 0.0f;
        float nz = 0.0f;
        float depth = 0.0f;
        if (math::circleBoxPush(x, z, radius, lo, hi, nx, nz, depth)) {
            target.standing = false;
            target.time_down = 0.0f;
            ++fallen;
        }
    }
    return fallen;
}

float rayBoxDistance(const Ray& ray, const float box_min[3], const float box_max[3]) {
    return math::rayBoxDistance(ray.origin, ray.direction, box_min, box_max);
}

} // namespace gameplay

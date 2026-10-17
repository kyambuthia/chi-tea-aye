// Checks the shooting range: ray against box, a shot knocking down the nearest
// standing target, misses, and targets standing again after the respawn time.
// Build and run from the repository root:
//   cmake --build build --target target_check && ./build/target_check

#include "gameplay/Targets.h"
#include "world/World.h"

#include <cstdio>

using gameplay::Ray;
using gameplay::Targets;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// A ray from (x, y, z) pointing along +Z.
Ray forward(float x, float y, float z) {
    Ray ray{};
    ray.origin[0] = x;
    ray.origin[1] = y;
    ray.origin[2] = z;
    ray.direction[2] = 1.0f;
    return ray;
}

} // namespace

int main() {
    // Ray against a box.
    {
        const float lo[3] = {-0.5f, -0.5f, 4.0f};
        const float hi[3] = {0.5f, 0.5f, 5.0f};
        const float hit = gameplay::rayBoxDistance(forward(0, 0, 0), lo, hi);
        expect(hit > 3.99f && hit < 4.01f, "a ray straight at a box reports the near face");
        expect(gameplay::rayBoxDistance(forward(2, 0, 0), lo, hi) < 0.0f, "a ray beside the box misses");
        expect(gameplay::rayBoxDistance(forward(0, 0, 6), lo, hi) < 0.0f, "a box behind the ray is a miss");
        expect(gameplay::rayBoxDistance(forward(0, 0, 4.5f), lo, hi) == 0.0f, "a ray starting inside reports zero");
    }

    // Shots against the range.
    {
        world::World world;
        Targets targets;
        targets.place(world);
        expect(targets.hits() == 0 && targets.shots() == 0, "the score starts at zero");

        // Aim at the first target's centre from in front of it.
        const auto& first = targets.targets()[0];
        Ray at_first = forward(first.center[0], first.center[1], first.center[2] - 5.0f);
        expect(targets.shoot(at_first), "a shot at a target hits it");
        expect(!targets.targets()[0].standing, "the hit target is knocked down");
        expect(targets.hits() == 1 && targets.shots() == 1, "the hit is counted");

        expect(!targets.shoot(at_first), "a shot at a knocked-down target misses");
        expect(targets.shots() == 2 && targets.hits() == 1, "the miss is counted as a shot only");

        // Nothing in the way: a shot into the sky misses.
        expect(!targets.shoot(forward(0.0f, 30.0f, 0.0f)), "a shot over the range misses");

        // The respawn: the target stands again after the respawn time.
        targets.update(Targets::kRespawnTime * 0.5f);
        expect(!targets.targets()[0].standing, "the target stays down before the respawn time");
        targets.update(Targets::kRespawnTime);
        expect(targets.targets()[0].standing, "the target stands again after the respawn time");
    }

    // A wall in the way stops the shot: a target beyond the wall's distance is a miss.
    {
        world::World world;
        Targets targets;
        targets.place(world);
        const auto& first = targets.targets()[0];
        Ray at_first = forward(first.center[0], first.center[1], first.center[2] - 5.0f);
        expect(!targets.shoot(at_first, 2.0f), "a wall nearer than the target stops the shot");
        expect(targets.targets()[0].standing, "the target behind the wall stays standing");
        expect(targets.shots() == 1 && targets.hits() == 0, "the blocked shot still counts as a shot");
    }

    // A shot along one target's line hits that target, and leaves the others standing.
    {
        world::World world;
        Targets targets;
        targets.place(world);
        Ray along = forward(-4.0f, targets.targets()[4].center[1], 0.0f);
        expect(targets.shoot(along), "a shot along the line hits the target on it");
        expect(!targets.targets()[4].standing, "the target on the line is the one knocked down");
        expect(targets.targets()[1].standing, "targets off the line stay standing");
    }

    std::printf(failures == 0 ? "TARGET CHECK OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

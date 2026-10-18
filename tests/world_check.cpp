// Checks the city and its spatial grid: grid queries and rays agree with a brute-force
// search, buildings stay off the roads and out of the park, parking spots are clear,
// the park meets the flat streets, and the same seed builds the same city.
// Build and run from the repository root:
//   cmake --build build --target world_check && ./build/world_check

#include "math/Geometry.h"
#include "world/City.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// Deterministic test points.
struct Lcg {
    uint32_t s = 12345u;
    float next(float lo, float hi) {
        s = s * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(s >> 8) / 16777216.0f;
    }
};

} // namespace

int main() {
    const world::World world;
    const auto& boxes = world.walls();
    const float extent = world::World::kCityHalfSize;
    std::printf("city: %zu solid boxes (%d practice walls), %zu lamps, %zu parking spots\n", boxes.size(),
        world.practiceWallCount(), world.city().lamps().size(), world.city().parking().size());
    expect(boxes.size() > 100, "the city has buildings");

    // 1. Grid queries return exactly the boxes a brute-force footprint test finds.
    {
        Lcg rng;
        int mismatches = 0;
        std::vector<int> found;
        for (int i = 0; i < 2000; ++i) {
            const float x = rng.next(-extent, extent);
            const float z = rng.next(-extent, extent);
            const float r = rng.next(0.1f, 12.0f);
            found.clear();
            world.boxesNear(x - r, z - r, x + r, z + r, found);
            std::sort(found.begin(), found.end());
            expect(std::adjacent_find(found.begin(), found.end()) == found.end(), "a query reports each box once");
            for (int b = 0; b < static_cast<int>(boxes.size()); ++b) {
                const bool overlaps = boxes[b].min[0] <= x + r && boxes[b].max[0] >= x - r && boxes[b].min[2] <= z + r &&
                    boxes[b].max[2] >= z - r;
                if (overlaps && !std::binary_search(found.begin(), found.end(), b)) ++mismatches;
            }
        }
        std::printf("grid queries: %d boxes missed over 2000 queries (want 0)\n", mismatches);
        expect(mismatches == 0, "grid queries find every overlapping box");
    }

    // 2. Grid rays hit the same nearest box at the same distance as testing every box.
    {
        Lcg rng;
        int mismatches = 0;
        for (int i = 0; i < 3000; ++i) {
            const float o[3] = {rng.next(-extent, extent), rng.next(-0.5f, 40.0f), rng.next(-extent, extent)};
            float d[3] = {rng.next(-1.0f, 1.0f), rng.next(-0.4f, 0.4f), rng.next(-1.0f, 1.0f)};
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (float& c : d) c /= len;
            const float max_distance = rng.next(1.0f, 300.0f);
            float brute = max_distance;
            for (const world::Box& b : boxes) {
                const float t = math::rayBoxDistance(o, d, b.min, b.max);
                if (t >= 0.0f && t < brute) brute = t;
            }
            const float grid = world.raycast(o, d, max_distance);
            if (std::fabs(grid - brute) > 1e-3f) ++mismatches;
        }
        std::printf("grid rays: %d of 3000 disagree with brute force (want 0)\n", mismatches);
        expect(mismatches == 0, "grid rays match brute force");
    }

    // 3. Buildings sit inside their lots: off the roads and sidewalks, out of the park.
    {
        int bad = 0;
        for (const world::Building& b : world.city().buildings()) {
            const float cx = 0.5f * (b.box.min[0] + b.box.max[0]);
            const float cz = 0.5f * (b.box.min[2] + b.box.max[2]);
            const float corners[4][2] = {{b.box.min[0] + 0.01f, b.box.min[2] + 0.01f}, {b.box.max[0] - 0.01f, b.box.min[2] + 0.01f},
                {b.box.min[0] + 0.01f, b.box.max[2] - 0.01f}, {b.box.max[0] - 0.01f, b.box.max[2] - 0.01f}};
            if (world::surfaceAt(cx, cz) != world::Surface::Lot) ++bad;
            for (const auto& c : corners) {
                if (world::surfaceAt(c[0], c[1]) != world::Surface::Lot) ++bad;
            }
        }
        std::printf("buildings outside their lots: %d (want 0)\n", bad);
        expect(bad == 0, "every building stands on a lot");
    }

    // 4. Parking spots are on a road or in the park, with room for a car around them.
    {
        int bad = 0;
        std::vector<int> near;
        for (const world::ParkingSpot& p : world.city().parking()) {
            const world::Surface s = world::surfaceAt(p.x, p.z);
            if (s != world::Surface::Road && s != world::Surface::Park) ++bad;
            near.clear();
            world.boxesNear(p.x - 3.0f, p.z - 3.0f, p.x + 3.0f, p.z + 3.0f, near);
            for (int i : near) {
                float nx, nz, depth;
                if (math::circleBoxPush(p.x, p.z, 2.6f, boxes[i].min, boxes[i].max, nx, nz, depth)) ++bad;
            }
        }
        std::printf("blocked or misplaced parking spots: %d (want 0)\n", bad);
        expect(bad == 0, "parking spots are clear and on a road");
    }

    // 5. The park's hills meet the street: flat at the park edge and on every road.
    {
        float worst = 0.0f;
        for (float t = -25.0f; t <= 25.0f; t += 0.5f) {
            worst = std::fmax(worst, std::fabs(world.groundHeight(19.6f, t) - world::World::kBaseHeight));
            worst = std::fmax(worst, std::fabs(world.groundHeight(t, -19.6f) - world::World::kBaseHeight));
            worst = std::fmax(worst, std::fabs(world.groundHeight(30.0f, t * 4.0f) - world::World::kBaseHeight));
        }
        std::printf("ground at the park edge and on roads: %.4f m off the street level (want 0)\n", worst);
        expect(worst < 1e-5f, "the park edge and roads are flat");
        expect(world.groundHeight(3.0f, 2.0f) != world::World::kBaseHeight, "the park keeps its hills");
    }

    // 6. Determinism: the same seed builds the same city.
    {
        world::City a;
        world::City b;
        a.generate(world::World::kCitySeed);
        b.generate(world::World::kCitySeed);
        bool same = a.buildings().size() == b.buildings().size();
        for (size_t i = 0; same && i < a.buildings().size(); ++i) {
            same = a.buildings()[i].box.max[1] == b.buildings()[i].box.max[1] && a.buildings()[i].box.min[0] == b.buildings()[i].box.min[0];
        }
        world::City c;
        c.generate(world::World::kCitySeed + 1);
        const bool differs = c.buildings().size() != a.buildings().size() ||
            c.buildings()[0].box.max[1] != a.buildings()[0].box.max[1];
        expect(same, "the same seed builds the same city");
        expect(differs, "a different seed builds a different city");
    }

    // 7. Surface classes at known points.
    {
        expect(world::surfaceAt(0.0f, 0.0f) == world::Surface::Park, "the origin is the park");
        expect(world::surfaceAt(30.0f, 0.0f) == world::Surface::Road, "x = 30 is a road");
        expect(world::surfaceAt(20.5f, 0.0f) == world::Surface::Sidewalk, "the park's edge is a sidewalk");
        expect(world::surfaceAt(60.0f, 60.0f) == world::Surface::Lot, "a block centre is a lot");
        expect(world::surfaceAt(extent + 1.0f, 0.0f) == world::Surface::Outside, "past the ring road is outside");
        expect(world::nearestRoadCentre(41.0f) == 30.0f && world::nearestRoadCentre(-80.0f) == -90.0f, "road centrelines");
    }

    std::printf(failures == 0 ? "WORLD OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

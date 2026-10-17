// Measures how well the feet meet uneven terrain, with and without foot IK.
// For each frame, the lowest skinned vertex should sit on the ground under it.
// Build and run from the repository root:
//   cmake --build build --target foot_contact_check && ./build/foot_contact_check

#include "assets/CharacterModel.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

struct Stats {
    double mean = 0.0;
    double worst = 0.0;
    int frames = 0;
    int within = 0;
};

Stats measure(const assets::CharacterModel& model, const world::World& world, const char* clip,
    float duration, bool with_ik) {
    Stats stats;
    const int frames = 60;
    std::vector<math::Mat4> joints(assets::kMaxJoints);
    const math::Mat4 root = math::translation(0.0f, world.groundHeight(0.0f, 0.0f), 0.0f);
    const assets::GroundHeightFn ground = with_ik
        ? assets::GroundHeightFn([&world](float x, float z) { return world.groundHeight(x, z); })
        : assets::GroundHeightFn();

    for (int f = 0; f < frames; ++f) {
        model.evaluatePose(clip, duration * f / frames, root, ground, joints.data());

        // Lowest skinned vertex, and the ground under it.
        float best_y = 1e9f;
        float best_x = 0.0f;
        float best_z = 0.0f;
        for (const auto& v : model.mesh().vertices) {
            float o[3] = {0.0f, 0.0f, 0.0f};
            for (int k = 0; k < 4; ++k) {
                const float w = v.weights[k];
                if (w == 0.0f) continue;
                const math::Mat4& m = joints[static_cast<int>(v.joints[k])];
                for (int c = 0; c < 3; ++c) {
                    o[c] += w * (m.m[c] * v.position[0] + m.m[4 + c] * v.position[1] + m.m[8 + c] * v.position[2] + m.m[12 + c]);
                }
            }
            if (o[1] < best_y) {
                best_y = o[1];
                best_x = o[0];
                best_z = o[2];
            }
        }
        // The sole's offset from the ankle: the lowest vertex sits at ground height.
        const double error = std::fabs(best_y - world.groundHeight(best_x, best_z));
        stats.mean += error;
        if (error > stats.worst) stats.worst = error;
        if (error < 0.03) ++stats.within;
        ++stats.frames;
    }
    stats.mean /= stats.frames;
    return stats;
}

} // namespace

int main() {
    std::ifstream file("assets/characters/african_warrior.glb", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assets::CharacterModel model;
    if (!model.load(bytes.data(), bytes.size())) {
        std::printf("FAIL: could not load model\n");
        return 1;
    }
    world::World world;

    struct ClipCase { const char* name; float duration; };
    const ClipCase clips[] = {{"Idle", 9.967f}, {"Walk", 1.067f}, {"Run", 0.733f},
        {"WalkBackwards", 1.233f}, {"StrafeLeft", 0.967f}};

    int failures = 0;
    std::printf("%-14s %-24s %-24s\n", "clip", "IK off (mean/worst m)", "IK on (mean/worst m)");
    for (const ClipCase& c : clips) {
        const Stats off = measure(model, world, c.name, c.duration, false);
        const Stats on = measure(model, world, c.name, c.duration, true);
        std::printf("%-14s %.3f / %.3f           %.3f / %.3f  (frames within 3 cm: %d/%d)\n",
            c.name, off.mean, off.worst, on.mean, on.worst, on.within, on.frames);
        // IK must not make contact worse on average.
        if (on.mean > off.mean + 1e-3) {
            ++failures;
            std::printf("  FAIL: foot IK increased ground error for %s\n", c.name);
        }
    }
    std::printf(failures == 0 ? "FOOT CONTACT OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

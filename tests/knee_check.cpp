// Measures how far each knee bends over each locomotion clip, with foot IK on
// and off. The interior angle at the knee is 180 degrees when straight.
// Build and run from the repository root:
//   cmake --build build --target knee_check && ./build/knee_check

#include "assets/CharacterModel.h"
#include "world/World.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec {
    float x, y, z;
};

Vec translation(const math::Mat4& m) {
    return {m.m[12], m.m[13], m.m[14]};
}

// Interior angle at knee, in degrees, between the thigh and the shin.
float kneeAngle(const Vec& hip, const Vec& knee, const Vec& ankle) {
    const Vec a{hip.x - knee.x, hip.y - knee.y, hip.z - knee.z};
    const Vec b{ankle.x - knee.x, ankle.y - knee.y, ankle.z - knee.z};
    const float la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const float lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    const float cosine = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb);
    return std::acos(std::fmax(-1.0f, std::fmin(1.0f, cosine))) * 180.0f / kPi;
}

// Minimum knee angle (most bent) over a clip, for one leg.
float minKneeAngle(const assets::CharacterModel& model, const world::World& world, const char* clip,
    bool with_ik, int up, int leg, int foot) {
    const float duration = model.clipDuration(clip);
    const math::Mat4 root = world.groundFrame(0.0f, 0.0f);
    const assets::GroundHeightFn ground = with_ik
        ? assets::GroundHeightFn([&world](float x, float z) { return world.groundHeight(x, z); })
        : assets::GroundHeightFn();
    std::vector<math::Mat4> joints(assets::kMaxJoints);
    float worst = 180.0f;
    const int frames = 120;
    for (int f = 0; f < frames; ++f) {
        model.evaluatePose(clip, duration * f / frames, root, ground, joints.data());
        // World positions: skin * bind world, then the root placement is already in skin.
        const Vec hip = translation(math::multiply(joints[up], model.jointBindWorld(up)));
        const Vec knee = translation(math::multiply(joints[leg], model.jointBindWorld(leg)));
        const Vec ankle = translation(math::multiply(joints[foot], model.jointBindWorld(foot)));
        const float angle = kneeAngle(hip, knee, ankle);
        if (angle < worst) worst = angle;
        if (std::getenv("KNEE_TRACE") && std::string(clip) == std::getenv("KNEE_TRACE") && f % 20 == 0) std::printf("  %s f%03d knee %.1f  hip y %.3f ankle y %.3f\n", with_ik ? "ik" : "raw", f, angle, hip.y, ankle.y);
    }
    return worst;
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
    const char* clips[] = {"Idle", "Walk", "Run", "WalkBackwards", "StrafeLeft", "StrafeRight"};
    const int up[2] = {model.jointIndex("LeftUpLeg"), model.jointIndex("RightUpLeg")};
    const int leg[2] = {model.jointIndex("LeftLeg"), model.jointIndex("RightLeg")};
    const int foot[2] = {model.jointIndex("LeftFoot"), model.jointIndex("RightFoot")};

    std::printf("%-14s %-22s %-22s\n", "clip", "IK off, min knee deg", "IK on, min knee deg");
    for (const char* clip : clips) {
        if (model.clipDuration(clip) <= 0.0f) continue;
        float off = 180.0f, on = 180.0f;
        for (int s = 0; s < 2; ++s) {
            const float a = minKneeAngle(model, world, clip, false, up[s], leg[s], foot[s]);
            const float b = minKneeAngle(model, world, clip, true, up[s], leg[s], foot[s]);
            if (a < off) off = a;
            if (b < on) on = b;
        }
        std::printf("%-14s %-22.1f %-22.1f\n", clip, off, on);
    }
    return 0;
}

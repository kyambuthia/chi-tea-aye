// Checks the left-hand IK: a reachable fore-end goal is met within 1 cm, and an
// unreachable one is approached without the arm bending through itself.
// Build and run from the repository root:
//   cmake --build build --target hand_ik_check && ./build/hand_ik_check

#include "assets/CharacterModel.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

int failures = 0;

// World position of a joint, from its skin matrix and bind pose.
void jointPosition(const assets::CharacterModel& model, const math::Mat4* skin, int joint, float out[3]) {
    const math::Mat4 world = math::multiply(skin[joint], model.jointBindWorld(joint));
    out[0] = world.m[12];
    out[1] = world.m[13];
    out[2] = world.m[14];
}

float distance3(const float a[3], const float b[3]) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
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
    const int hand = model.jointIndex("LeftHand");
    const int shoulder = model.jointIndex("LeftArm");
    if (hand < 0 || shoulder < 0) {
        std::printf("FAIL: left arm joints missing\n");
        return 1;
    }

    const math::Mat4 root = math::identity();
    std::vector<math::Mat4> joints(assets::kMaxJoints);
    const int elbow = model.jointIndex("LeftForeArm");
    if (elbow < 0) {
        std::printf("FAIL: left forearm joint missing\n");
        return 1;
    }

    // Diagnostic: with a goal at the hand's own position the hand must not move.
    model.evaluatePose("Idle", 0.5f, root, {}, joints.data());
    float plain[3];
    jointPosition(model, joints.data(), hand, plain);
    model.evaluatePose("Idle", 0.5f, root, {}, joints.data(), plain);
    float noop[3];
    jointPosition(model, joints.data(), hand, noop);
    std::printf("no-op goal moves the hand by %.1f mm\n", distance3(plain, noop) * 1000.0f);

    // Arm length from the bind pose: shoulder to elbow plus elbow to hand.
    model.evaluatePose("Idle", 0.5f, root, {}, joints.data());
    float shoulder_at[3], elbow_at[3];
    jointPosition(model, joints.data(), shoulder, shoulder_at);
    jointPosition(model, joints.data(), elbow, elbow_at);
    const float arm_length = distance3(shoulder_at, elbow_at) + distance3(elbow_at, plain);
    std::printf("arm length %.3f m\n", arm_length);

    // Reachable: a goal 60% of the arm's length in front of the shoulder and across the body.
    const float reachable[3] = {shoulder_at[0] - 0.10f, shoulder_at[1] - 0.05f, shoulder_at[2] + 0.6f * arm_length};
    model.evaluatePose("Idle", 0.5f, root, {}, joints.data(), reachable);
    float got[3];
    jointPosition(model, joints.data(), hand, got);
    const float error = distance3(got, reachable);
    std::printf("reachable goal: hand %.1f mm from the goal (want < 10 mm)\n", error * 1000.0f);
    if (error > 0.010f) ++failures;

    // Unreachable: far beyond the arm. The hand should stop at full extension,
    // about one arm length from the shoulder.
    const float far_goal[3] = {shoulder_at[0], shoulder_at[1], shoulder_at[2] + 2.0f};
    model.evaluatePose("Idle", 0.5f, root, {}, joints.data(), far_goal);
    jointPosition(model, joints.data(), hand, got);
    const float reach = distance3(got, shoulder_at);
    std::printf("far goal: hand %.3f m from the shoulder (want within 1%% of %.3f m)\n", reach, arm_length);
    if (std::fabs(reach - arm_length) > 0.01f * arm_length) ++failures;

    std::printf(failures == 0 ? "HAND IK OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

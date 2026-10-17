// Samples every warrior animation clip and checks that the poses are sane.
// Build and run from the repository root:
//   cmake --build build --target clip_check && ./build/clip_check

#include "assets/CharacterModel.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, const char* clip, float t) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL %s at t=%.3f (%s)\n", what, t, clip);
    }
}

// Skins every vertex with the pose and returns the bounding box.
void skinnedBounds(const assets::CharacterMesh& mesh, const math::Mat4* joints, float box[6], bool& finite) {
    for (int i = 0; i < 6; ++i) {
        box[i] = i < 3 ? 1e9f : -1e9f;
    }
    finite = true;
    for (const auto& v : mesh.vertices) {
        float out[3] = {0.0f, 0.0f, 0.0f};
        float wsum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
        if (wsum <= 0.0f) {
            for (int c = 0; c < 3; ++c) out[c] = v.position[c];
        } else {
            for (int k = 0; k < 4; ++k) {
                const int j = static_cast<int>(v.joints[k]);
                const float w = v.weights[k];
                if (w == 0.0f) continue;
                const math::Mat4& m = joints[j];
                const float* p = v.position;
                const float x = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
                const float y = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
                const float z = m.m[2] * p[0] + m.m[6] * p[1] + m.m[10] * p[2] + m.m[14];
                out[0] += w * x;
                out[1] += w * y;
                out[2] += w * z;
            }
        }
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(out[c])) finite = false;
            if (out[c] < box[c]) box[c] = out[c];
            if (out[c] > box[3 + c]) box[3 + c] = out[c];
        }
    }
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

    const char* clips[] = {"Idle", "Walk", "Run"};
    const float kFps = 30.0f;
    for (const char* clip : clips) {
        if (!model.hasClip(clip)) {
            ++failures;
            std::printf("FAIL: clip %s missing\n", clip);
            continue;
        }

        std::printf("%s:\n", clip);
        // Sample 3.5 s: the longest clip (Idle) is about 2.03 s.
        const int frames = static_cast<int>(3.5f * kFps);
        std::vector<math::Mat4> prev(assets::kMaxJoints);
        std::vector<math::Mat4> cur(assets::kMaxJoints);
        float min_y = 1e9f;
        float max_y = -1e9f;
        float total_motion = 0.0f;

        model.evaluatePose(clip, 0.0f, math::identity(), {}, prev.data());
        for (int f = 0; f <= frames; ++f) {
            const float t = f / kFps;
            model.evaluatePose(clip, t, math::identity(), {}, cur.data());

            bool finite = true;
            for (const auto& m : cur) {
                for (float v : m.m) {
                    if (!std::isfinite(v)) finite = false;
                }
            }
            check(finite, "non-finite joint matrix", clip, t);

            float box[6];
            bool mesh_finite = true;
            skinnedBounds(model.mesh(), cur.data(), box, mesh_finite);
            check(mesh_finite, "non-finite skinned vertex", clip, t);
            // A 1.85 m character should stay well inside this box while animating.
            check(box[1] > -0.5f && box[4] < 3.0f, "skinned mesh left the expected height range", clip, t);
            check(std::fabs(box[0]) < 2.0f && std::fabs(box[3]) < 2.0f, "skinned mesh drifted sideways", clip, t);
            if (box[1] < min_y) min_y = box[1];
            if (box[4] > max_y) max_y = box[4];

            float motion = 0.0f;
            for (int j = 0; j < 23; ++j) {
                for (int k = 0; k < 16; ++k) {
                    const float d = cur[j].m[k] - prev[j].m[k];
                    motion += d * d;
                }
            }
            total_motion += std::sqrt(motion);
            prev = cur;
        }

        // Looping: some later time should return to the start pose.
        model.evaluatePose(clip, 0.0f, math::identity(), {}, prev.data());
        std::vector<math::Mat4> wrap(assets::kMaxJoints);
        float loop_at = 0.0f;
        float best_diff = 1e9f;
        for (float t = 0.5f; t <= 10.5f; t += 0.01f) {
            model.evaluatePose(clip, t, math::identity(), {}, wrap.data());
            float diff = 0.0f;
            for (int j = 0; j < 23; ++j) {
                for (int k = 0; k < 16; ++k) {
                    diff += std::fabs(wrap[j].m[k] - prev[j].m[k]);
                }
            }
            if (diff < best_diff) {
                best_diff = diff;
                loop_at = t;
            }
        }
        const bool loops = best_diff < 0.5f;
        std::printf("  height range %.2f .. %.2f m, mean motion per frame %.4f\n",
            min_y, max_y, total_motion / frames);
        check(total_motion > 0.01f, "pose never changes", clip, 0.0f);

        // Every clip should move the whole body, not just the legs and hips.
        int moving_joints = 0;
        std::vector<math::Mat4> pose_a(assets::kMaxJoints);
        std::vector<math::Mat4> pose_b(assets::kMaxJoints);
        model.evaluatePose(clip, 0.0f, math::identity(), {}, pose_a.data());
        model.evaluatePose(clip, 0.7f, math::identity(), {}, pose_b.data());
        for (int j = 0; j < 23; ++j) {
            float d = 0.0f;
            for (int k = 0; k < 16; ++k) {
                d += std::fabs(pose_a[j].m[k] - pose_b[j].m[k]);
            }
            if (d > 1e-3f) ++moving_joints;
        }
        std::printf("  joints moving between t=0 and t=0.7s: %d of 23\n", moving_joints);
        check(moving_joints >= 20, "too few joints animate (frozen torso or limbs)", clip, 0.0f);
        std::printf("  loop point near %.2f s (pose difference %.3f)%s\n", loop_at, best_diff, loops ? "" : "  FAIL: no loop found");
        check(loops, "pose does not return to its start", clip, loop_at);
    }

    std::printf(failures == 0 ? "ALL CLIPS OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

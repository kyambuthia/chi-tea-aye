// Checks the direction each locomotion clip moves the body, from its planted
// feet. A planted foot moves opposite to the body, so the foot's velocity
// relative to the pelvis gives the body's direction. Model space: forward is +Z
// and the warrior's left is +X.
// Build and run from the repository root:
//   cmake --build build --target clip_direction_check && ./build/clip_direction_check

#include "assets/CharacterModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

struct Point {
    float x, y, z;
};

// Skinned vertex positions for one pose, in model space.
std::vector<Point> skin(const assets::CharacterModel& model, const math::Mat4* joints) {
    std::vector<Point> out;
    out.reserve(model.mesh().vertices.size());
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
        out.push_back({o[0], o[1], o[2]});
    }
    return out;
}

// Mean of the torso vertices in the pelvis band: the body's position.
Point pelvis(const std::vector<Point>& v) {
    Point c{0, 0, 0};
    int n = 0;
    for (const Point& p : v) {
        if (p.y > 0.85f && p.y < 1.10f && std::fabs(p.x) < 0.2f) {
            c.x += p.x; c.y += p.y; c.z += p.z; ++n;
        }
    }
    if (n > 0) { c.x /= n; c.y /= n; c.z /= n; }
    return c;
}

// Mean of a foot's lowest fifth of vertices. side +1 is the warrior's left (+X),
// -1 is the right (-X). Each foot is tracked on its own, so the two never mix.
Point footSole(const std::vector<Point>& v, int side) {
    std::vector<Point> foot;
    for (const Point& p : v) {
        if (p.y < 0.25f && p.x * side > 0.05f) foot.push_back(p);
    }
    std::sort(foot.begin(), foot.end(), [](const Point& a, const Point& b) { return a.y < b.y; });
    Point c{0, 0, 0};
    const size_t n = foot.size() / 5 + 1;
    for (size_t i = 0; i < n && i < foot.size(); ++i) {
        c.x += foot[i].x; c.y += foot[i].y; c.z += foot[i].z;
    }
    c.x /= n; c.y /= n; c.z /= n;
    return c;
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

    struct Case { const char* clip; float duration; float expect_x; float expect_z; };
    // Expected body direction in model space: planted feet move opposite to it.
    const Case cases[] = {
        {"Walk", 1.067f, 0.0f, 1.0f},
        {"Run", 0.733f, 0.0f, 1.0f},
        {"WalkBackwards", 1.233f, 0.0f, -1.0f},
        {"StrafeLeft", 0.967f, 1.0f, 0.0f},
        {"StrafeRight", 0.967f, -1.0f, 0.0f},
    };

    int failures = 0;
    std::vector<math::Mat4> joints(assets::kMaxJoints);
    const math::Mat4 root = math::identity();
    for (const Case& c : cases) {
        const int frames = 60;
        const float dt = c.duration / frames;
        // Per foot: relative position (sole minus pelvis) and height, for each frame.
        std::vector<Point> rel[2];
        std::vector<float> height[2];
        for (int side = 0; side < 2; ++side) {
            rel[side].resize(frames + 1);
            height[side].resize(frames + 1);
        }
        for (int f = 0; f <= frames; ++f) {
            const float t = c.duration * f / frames;
            model.evaluatePose(c.clip, t, root, {}, joints.data());
            const std::vector<Point> verts = skin(model, joints.data());
            const Point body = pelvis(verts);
            for (int side = 0; side < 2; ++side) {
                const int sign = side == 0 ? 1 : -1;
                const Point sole = footSole(verts, sign);
                rel[side][f] = {sole.x - body.x, sole.y - body.y, sole.z - body.z};
                height[side][f] = sole.y;
            }
        }

        // A planted foot moves with the ground, so the body moves opposite to it.
        // Average each foot over its planted frames only, then combine the feet.
        double sum_x = 0.0, sum_z = 0.0;
        int count = 0;
        for (int side = 0; side < 2; ++side) {
            float lowest = 1e9f;
            for (float h : height[side]) if (h < lowest) lowest = h;
            for (int f = 0; f < frames; ++f) {
                const bool planted = height[side][f] < lowest + 0.02f && height[side][f + 1] < lowest + 0.02f;
                if (!planted) continue;
                sum_x += (rel[side][f + 1].x - rel[side][f].x) / dt;
                sum_z += (rel[side][f + 1].z - rel[side][f].z) / dt;
                ++count;
            }
        }
        if (count == 0) {
            std::printf("%-14s no planted frames found: WRONG\n", c.clip);
            ++failures;
            continue;
        }
        const float body_x = static_cast<float>(-sum_x / count);
        const float body_z = static_cast<float>(-sum_z / count);
        const bool x_ok = c.expect_x == 0.0f ? std::fabs(body_x) < 0.25f * std::fabs(body_z) + 0.05f
                                             : body_x * c.expect_x > 0.0f;
        const bool z_ok = c.expect_z == 0.0f ? std::fabs(body_z) < 0.25f * std::fabs(body_x) + 0.05f
                                             : body_z * c.expect_z > 0.0f;
        std::printf("%-14s body moves (x %+.2f, z %+.2f) m/s, expected (x %+.0f, z %+.0f): %s\n",
            c.clip, body_x, body_z, c.expect_x, c.expect_z, (x_ok && z_ok) ? "ok" : "WRONG");
        if (!x_ok || !z_ok) ++failures;
    }
    std::printf(failures == 0 ? "CLIP DIRECTIONS OK\n" : "%d WRONG DIRECTION(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

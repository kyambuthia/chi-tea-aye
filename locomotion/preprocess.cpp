// Offline preprocessing: finds each foot's contacts and strides in every clip.
//   locomotion_preprocess <character.glb> <output dir>
// Writes <output dir>/<clip>.csv with one row per sample: time, each sole's height
// and speed, and whether each foot is planted. Prints the contacts and strides.

#include "assets/CharacterModel.h"
#include "locomotion/Contacts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr float kSampleRate = 30.0f;  // samples per second, the clips' frame rate

struct Point {
    float x, y, z;
};

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

// The sole of one foot: the mean of its lowest fifth of vertices. side +1 is the
// warrior's left (+X), -1 is the right (-X).
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

float distance3(const Point& a, const Point& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: locomotion_preprocess <character.glb> <output dir>\n");
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assets::CharacterModel model;
    if (!model.load(bytes.data(), bytes.size())) {
        std::printf("could not load %s\n", argv[1]);
        return 1;
    }

    const char* clips[] = {"Walk", "Run", "WalkBackwards", "StrafeLeft", "StrafeRight", "Idle"};
    const float durations[] = {1.067f, 0.733f, 1.233f, 0.967f, 0.967f, 9.967f};
    const std::string out_dir = argv[2];
    const math::Mat4 root = math::identity();
    std::vector<math::Mat4> joints(assets::kMaxJoints);
    locomotion::ContactParams params;

    for (size_t c = 0; c < sizeof(clips) / sizeof(clips[0]); ++c) {
        const int samples = static_cast<int>(durations[c] * kSampleRate + 0.5f);
        const float dt = 1.0f / kSampleRate;

        // Sole positions for both feet at every sample.
        std::vector<Point> sole[2];
        sole[0].resize(samples + 1);
        sole[1].resize(samples + 1);
        for (int i = 0; i <= samples; ++i) {
            model.evaluatePose(clips[c], i * dt, root, {}, joints.data());
            const std::vector<Point> verts = skin(model, joints.data());
            sole[0][i] = footSole(verts, 1);
            sole[1][i] = footSole(verts, -1);
        }

        // Ground: the lowest the soles get in this clip. Height is measured from it.
        float ground = 1e9f;
        for (int s = 0; s < 2; ++s) {
            for (const Point& p : sole[s]) ground = std::min(ground, p.y);
        }

        std::vector<float> height[2], speed[2];
        for (int s = 0; s < 2; ++s) {
            height[s].resize(samples + 1);
            speed[s].resize(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                height[s][i] = sole[s][i].y - ground;
                const int prev = i > 0 ? i - 1 : i;
                const int next = i < samples ? i + 1 : i;
                speed[s][i] = distance3(sole[s][next], sole[s][prev]) / (dt * (next - prev));
            }
        }

        const std::vector<locomotion::Contact> left = locomotion::detectContacts(height[0], speed[0], params);
        const std::vector<locomotion::Contact> right = locomotion::detectContacts(height[1], speed[1], params);
        std::printf("%s (%d samples at %.0f Hz)\n", clips[c], samples + 1, kSampleRate);
        const char* names[2] = {"left ", "right"};
        const std::vector<locomotion::Contact>* contacts[2] = {&left, &right};
        for (int s = 0; s < 2; ++s) {
            std::printf("  %s planted:", names[s]);
            for (const auto& k : *contacts[s]) {
                std::printf(" [%d,%d)", k.begin, k.end);
            }
            std::printf("\n  %s strides:", names[s]);
            for (const auto& st : locomotion::strides(*contacts[s])) {
                std::printf(" %d->%d", st.liftoff, st.plant);
            }
            std::printf("\n");
        }

        // Per-sample CSV for plotting and hand labelling.
        std::vector<char> planted[2];
        for (int s = 0; s < 2; ++s) {
            planted[s].assign(samples + 1, 0);
            for (const auto& k : *contacts[s]) {
                for (int i = k.begin; i < k.end; ++i) planted[s][i] = 1;
            }
        }
        const std::string path = out_dir + "/" + clips[c] + ".csv";
        FILE* csv = std::fopen(path.c_str(), "w");
        if (csv != nullptr) {
            std::fprintf(csv, "frame,time,left_height,left_speed,left_planted,right_height,right_speed,right_planted\n");
            for (int i = 0; i <= samples; ++i) {
                std::fprintf(csv, "%d,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%d\n", i, i * dt,
                    height[0][i], speed[0][i], planted[0][i], height[1][i], speed[1][i], planted[1][i]);
            }
            std::fclose(csv);
        }
    }
    return 0;
}

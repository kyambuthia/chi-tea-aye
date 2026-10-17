#pragma once

#include "assets/MeshVertex.h"
#include "math/Mat4.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

struct cgltf_data;

namespace assets {

constexpr int kMaxJoints = 32;

// Ground height (meters, +Y up) at a world ground position.
using GroundHeightFn = std::function<float(float x, float z)>;

struct CharacterMesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<uint8_t> base_color_rgba;
    int texture_width = 0;
    int texture_height = 0;
};

// A skinned glTF character (binary .glb) with its animation clips.
class CharacterModel {
public:
    CharacterModel() = default;
    ~CharacterModel();
    CharacterModel(const CharacterModel&) = delete;
    CharacterModel& operator=(const CharacterModel&) = delete;

    bool load(const void* data, size_t size);
    void cleanup();

    const CharacterMesh& mesh() const;

    // Heading (radians about +Y) of the mesh's front at zero rotation. The mesh faces -Z.
    float frontYaw() const;
    bool hasClip(const char* name) const;

    // Writes kMaxJoints skinning matrices for the named clip at the given time,
    // looping over the clip's duration. The matrices map the bind pose to the
    // posed character in world space, with the character placed by root.
    // With a ground function, the feet are solved onto the ground (foot IK) and
    // the pelvis drops when a foot needs to reach down. Unused slots are identity.
    void evaluatePose(const char* clip_name, float time_seconds, const math::Mat4& root,
        const GroundHeightFn& ground, math::Mat4* out) const;

private:
    // cgltf points into the file bytes for the GLB binary chunk, so keep a copy alive.
    std::vector<uint8_t> file_bytes_;
    cgltf_data* gltf_ = nullptr;
    CharacterMesh mesh_;

    // Skeleton, indexed by joint.
    std::vector<const void*> joint_nodes_;
    std::vector<math::Mat4> inverse_bind_;

    // Per glTF node: the rest pose of the rig as exported, and the rest pose the
    // mesh is bound to (from the inverse bind matrices). They differ when the
    // clips were authored on a different rest pose than the mesh.
    std::vector<math::Mat4> rest_local_rig_;
    std::vector<math::Mat4> rest_local_bind_;
    float front_yaw_ = 0.0f;

    // Per foot: the chain the IK solves (thigh, knee, ankle, toe), and the
    // heights of the sole below the ankle and of the toe above its lowest point.
    struct FootChain {
        int up = -1;
        int leg = -1;
        int foot = -1;
        int toe = -1;
        float ankle = 0.0f;
        float toe_offset = 0.0f;
    };
    FootChain feet_[2];
    int hips_ = -1;
    bool has_feet_ = false;

    void applyFootIk(std::vector<math::Mat4>& local, const math::Mat4& root, const GroundHeightFn& ground) const;
};

} // namespace assets

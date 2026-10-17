#pragma once

#include "assets/MeshVertex.h"
#include "math/Mat4.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct cgltf_data;

namespace assets {

constexpr int kMaxJoints = 32;

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

    // Writes kMaxJoints joint matrices for the named clip at the given time,
    // looping over the clip's duration. Unused slots are identity.
    void evaluatePose(const char* clip_name, float time_seconds, math::Mat4* out) const;

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
};

} // namespace assets

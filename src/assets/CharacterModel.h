#pragma once

#include "assets/MeshVertex.h"
#include "math/Mat4.h"

#include <ozz/base/memory/unique_ptr.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct cgltf_data;

namespace ozz::animation {
class Animation;
class Skeleton;
} // namespace ozz::animation

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

// A skinned glTF character (binary .glb) with its animation clips. The skeleton
// and clips are built as ozz-animation data; sampling and local-to-model
// evaluation run through ozz, and foot IK is applied on top.
class CharacterModel {
public:
    CharacterModel();
    ~CharacterModel();
    CharacterModel(const CharacterModel&) = delete;
    CharacterModel& operator=(const CharacterModel&) = delete;

    bool load(const void* data, size_t size);
    void cleanup();

    const CharacterMesh& mesh() const;
    bool hasClip(const char* name) const;
    // Number of joints in the skin.
    int jointCount() const;

    // Parent joint of a joint, or -1 for a root.
    int jointParent(int joint) const;

    // Bind-pose model-space transform of a joint, for diagnostics and checks.
    math::Mat4 jointBindWorld(int joint) const;

    // Skin joint index of the named bone, or -1 if the rig has no such bone.
    int jointIndex(const char* name) const;

    // Length of the named clip in seconds, or 0 if the clip is not present.
    float clipDuration(const char* name) const;

    // Heading (radians about +Y) of the mesh's front at zero rotation. The mesh faces +Z.
    float frontYaw() const;

    // Writes kMaxJoints skinning matrices for the named clip at the given time,
    // looping over the clip's duration. The matrices map the bind pose to the
    // posed character in world space, with the character placed by root.
    // With a ground function, the feet are solved onto the ground (foot IK) and
    // the pelvis drops when a foot needs to reach down. Unused slots are identity.
    void evaluatePose(const char* clip_name, float time_seconds, const math::Mat4& root,
        const GroundHeightFn& ground, math::Mat4* out) const;

private:
    // Per foot: the joints the IK solves (thigh, knee, ankle, toe), and the
    // heights of the sole below the ankle and of the toe above its lowest point.
    struct FootChain {
        int up = -1;
        int leg = -1;
        int foot = -1;
        int toe = -1;
        float ankle = 0.0f;
        float toe_offset = 0.0f;
    };

    void applyFootIk(std::vector<math::Mat4>& locals, const math::Mat4& root, const GroundHeightFn& ground) const;

    cgltf_data* gltf_ = nullptr;
    std::vector<uint8_t> file_bytes_;  // cgltf keeps pointers into these bytes
    CharacterMesh mesh_;

    // Skin joints, indexed by joint. Joint j's parent is joint_parent_[j] (or -1).
    std::vector<const void*> joint_nodes_;
    std::vector<int> joint_parent_;
    std::vector<math::Mat4> inverse_bind_;
    std::vector<math::Mat4> joint_world_bind_;  // bind pose, model space
    std::vector<math::Mat4> bind_local_;        // bind pose, relative to the parent joint

    // ozz data. The skeleton and animations are in depth-first joint order;
    // dfs_joint_ maps that order back to joint indices.
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton_;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> animations_;
    std::vector<std::string> animation_names_;
    std::vector<int> dfs_joint_;

    FootChain feet_[2];
    int hips_ = -1;
    int root_ = -1;  // the Root joint: its translation is the clip's root path
    bool has_feet_ = false;
    float front_yaw_ = 0.0f;
};

} // namespace assets

#include "assets/CharacterModel.h"

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

#include <cmath>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace assets {
namespace {

constexpr float kMeshFrontYaw = 0.0f;

// Foot targets: a foot closer than kPlantedClearance above the ground is pinned
// to it; beyond kSwingClearance it is left to the animation.
constexpr float kPlantedClearance = 0.10f;
constexpr float kSwingClearance = 0.20f;
constexpr float kMaxPelvisDrop = 0.25f;
constexpr float kPelvisShare = 0.5f;  // fraction of the lower foot's drop the pelvis takes

// Translation, rotation (x, y, z, w), and scale of a local transform.
struct Trs {
    float t[3] = {0.0f, 0.0f, 0.0f};
    float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float s[3] = {1.0f, 1.0f, 1.0f};
};

// Splits a matrix made of translation, rotation, and scale into its parts.
Trs decompose(const math::Mat4& m) {
    Trs out;
    out.t[0] = m.m[12];
    out.t[1] = m.m[13];
    out.t[2] = m.m[14];
    float rot[3][3];  // rot[row][col]
    for (int c = 0; c < 3; ++c) {
        const float len = std::sqrt(m.m[c * 4] * m.m[c * 4] + m.m[c * 4 + 1] * m.m[c * 4 + 1] + m.m[c * 4 + 2] * m.m[c * 4 + 2]);
        out.s[c] = len > 1e-9f ? len : 1.0f;
        for (int r = 0; r < 3; ++r) {
            rot[r][c] = m.m[c * 4 + r] / out.s[c];
        }
    }
    // Rotation matrix to quaternion.
    const float trace = rot[0][0] + rot[1][1] + rot[2][2];
    float x, y, z, w;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (rot[2][1] - rot[1][2]) / s;
        y = (rot[0][2] - rot[2][0]) / s;
        z = (rot[1][0] - rot[0][1]) / s;
    } else if (rot[0][0] > rot[1][1] && rot[0][0] > rot[2][2]) {
        const float s = std::sqrt(1.0f + rot[0][0] - rot[1][1] - rot[2][2]) * 2.0f;
        w = (rot[2][1] - rot[1][2]) / s;
        x = 0.25f * s;
        y = (rot[0][1] + rot[1][0]) / s;
        z = (rot[0][2] + rot[2][0]) / s;
    } else if (rot[1][1] > rot[2][2]) {
        const float s = std::sqrt(1.0f + rot[1][1] - rot[0][0] - rot[2][2]) * 2.0f;
        w = (rot[0][2] - rot[2][0]) / s;
        x = (rot[0][1] + rot[1][0]) / s;
        y = 0.25f * s;
        z = (rot[1][2] + rot[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + rot[2][2] - rot[0][0] - rot[1][1]) * 2.0f;
        w = (rot[1][0] - rot[0][1]) / s;
        x = (rot[0][2] + rot[2][0]) / s;
        y = (rot[1][2] + rot[2][1]) / s;
        z = 0.25f * s;
    }
    const float len = std::sqrt(x * x + y * y + z * z + w * w);
    out.r[0] = x / len;
    out.r[1] = y / len;
    out.r[2] = z / len;
    out.r[3] = w / len;
    return out;
}

bool readFloats(const cgltf_accessor* accessor, size_t components, std::vector<float>& out) {
    out.assign(accessor->count * components, 0.0f);
    return cgltf_accessor_unpack_floats(accessor, out.data(), out.size()) != 0;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type, int index = 0) {
    for (size_t i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& attr = prim.attributes[i];
        if (attr.type == type && attr.index == index) {
            return attr.data;
        }
    }
    return nullptr;
}

size_t nodeIndex(const cgltf_data* data, const cgltf_node* node) {
    return static_cast<size_t>(node - data->nodes);
}

// Model-space transform of joint j: its parent's model transform times its local one.
math::Mat4 jointModel(int j, const std::vector<int>& parent, const std::vector<math::Mat4>& locals, const math::Mat4& root) {
    if (parent[j] < 0) {
        return math::multiply(root, locals[j]);
    }
    return math::multiply(jointModel(parent[j], parent, locals, root), locals[j]);
}

void position(const math::Mat4& m, float out[3]) {
    out[0] = m.m[12];
    out[1] = m.m[13];
    out[2] = m.m[14];
}

void sub3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

float length3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void normalize3(float v[3]) {
    const float len = length3(v);
    if (len > 1e-9f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Applies a rotation matrix to the vector from its pivot to a point.
void rotatePoint(const math::Mat4& rotation, const float pivot[3], const float point[3], float out[3]) {
    const float d[3] = {point[0] - pivot[0], point[1] - pivot[1], point[2] - pivot[2]};
    out[0] = pivot[0] + rotation.m[0] * d[0] + rotation.m[4] * d[1] + rotation.m[8] * d[2];
    out[1] = pivot[1] + rotation.m[1] * d[0] + rotation.m[5] * d[1] + rotation.m[9] * d[2];
    out[2] = pivot[2] + rotation.m[2] * d[0] + rotation.m[6] * d[1] + rotation.m[10] * d[2];
}

// Builds the ozz skeleton's joints depth-first, recording each joint's index.
void appendJoint(ozz::animation::offline::RawSkeleton::Joint& out, int j, const std::vector<const void*>& nodes,
    const std::vector<std::vector<int>>& children, const std::vector<Trs>& rest, std::vector<int>& dfs_joint) {
    const auto* node = static_cast<const cgltf_node*>(nodes[j]);
    out.name = node->name != nullptr ? node->name : "joint";
    out.transform.translation = ozz::math::Float3(rest[j].t[0], rest[j].t[1], rest[j].t[2]);
    out.transform.rotation = ozz::math::Quaternion(rest[j].r[0], rest[j].r[1], rest[j].r[2], rest[j].r[3]);
    out.transform.scale = ozz::math::Float3(rest[j].s[0], rest[j].s[1], rest[j].s[2]);
    dfs_joint.push_back(j);
    for (int c : children[j]) {
        ozz::animation::offline::RawSkeleton::Joint child;
        appendJoint(child, c, nodes, children, rest, dfs_joint);
        out.children.push_back(child);
    }
}

} // namespace

struct CharacterModel::SampleScratch {
    std::vector<ozz::math::SoaTransform> soa;
    std::vector<math::Mat4> locals;
    std::unique_ptr<ozz::animation::SamplingJob::Context> context;
    int context_tracks = -1;
};

CharacterModel::CharacterModel() = default;

CharacterModel::~CharacterModel() {
    cleanup();
}

bool CharacterModel::load(const void* data, size_t size) {
    cleanup();

    const auto* bytes = static_cast<const uint8_t*>(data);
    file_bytes_.assign(bytes, bytes + size);

    cgltf_options options{};
    if (cgltf_parse(&options, file_bytes_.data(), file_bytes_.size(), &gltf_) != cgltf_result_success) {
        return false;
    }
    if (cgltf_load_buffers(&options, gltf_, nullptr) != cgltf_result_success) {
        return false;
    }

    const cgltf_node* skinned_node = nullptr;
    for (size_t i = 0; i < gltf_->nodes_count; ++i) {
        if (gltf_->nodes[i].mesh != nullptr && gltf_->nodes[i].skin != nullptr) {
            skinned_node = &gltf_->nodes[i];
            break;
        }
    }
    if (skinned_node == nullptr || skinned_node->mesh->primitives_count == 0) {
        return false;
    }

    const cgltf_primitive& prim = skinned_node->mesh->primitives[0];
    const cgltf_accessor* position_acc = findAttribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* normal = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor* joints = findAttribute(prim, cgltf_attribute_type_joints);
    const cgltf_accessor* weights = findAttribute(prim, cgltf_attribute_type_weights);
    if (position_acc == nullptr || joints == nullptr || weights == nullptr || prim.indices == nullptr) {
        return false;
    }

    std::vector<float> pos_data, normal_data, uv_data, joint_data, weight_data;
    if (!readFloats(position_acc, 3, pos_data) || !readFloats(joints, 4, joint_data) ||
        !readFloats(weights, 4, weight_data)) {
        return false;
    }
    if (normal != nullptr) {
        readFloats(normal, 3, normal_data);
    }
    if (uv != nullptr) {
        readFloats(uv, 2, uv_data);
    }

    mesh_.vertices.resize(position_acc->count);
    for (size_t i = 0; i < position_acc->count; ++i) {
        MeshVertex& v = mesh_.vertices[i];
        std::memcpy(v.position, &pos_data[i * 3], sizeof(v.position));
        if (normal != nullptr) {
            std::memcpy(v.normal, &normal_data[i * 3], sizeof(v.normal));
        } else {
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
        }
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        if (uv != nullptr) {
            std::memcpy(v.uv, &uv_data[i * 2], sizeof(v.uv));
        } else {
            v.uv[0] = v.uv[1] = 0.0f;
        }
        std::memcpy(v.joints, &joint_data[i * 4], sizeof(v.joints));
        std::memcpy(v.weights, &weight_data[i * 4], sizeof(v.weights));
    }

    mesh_.indices.resize(prim.indices->count);
    for (size_t i = 0; i < prim.indices->count; ++i) {
        mesh_.indices[i] = static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, i));
    }

    if (prim.material != nullptr && prim.material->has_pbr_metallic_roughness) {
        const cgltf_texture* texture = prim.material->pbr_metallic_roughness.base_color_texture.texture;
        if (texture != nullptr && texture->image != nullptr && texture->image->buffer_view != nullptr) {
            const cgltf_buffer_view* view = texture->image->buffer_view;
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(cgltf_buffer_view_data(view), static_cast<int>(view->size),
                &width, &height, &channels, 4);
            if (pixels != nullptr) {
                mesh_.texture_width = width;
                mesh_.texture_height = height;
                mesh_.base_color_rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
                stbi_image_free(pixels);
            }
        }
    }

    // Skin joints, their parents, and the bind pose they are measured against.
    const cgltf_skin* skin = skinned_node->skin;
    if (skin->joints_count > static_cast<size_t>(kMaxJoints)) {
        return false;
    }
    const size_t node_count = gltf_->nodes_count;
    const size_t joint_count = skin->joints_count;
    std::vector<int> joint_of_node(node_count, -1);
    joint_nodes_.clear();
    for (size_t j = 0; j < joint_count; ++j) {
        joint_nodes_.push_back(skin->joints[j]);
        joint_of_node[nodeIndex(gltf_, skin->joints[j])] = static_cast<int>(j);
    }
    joint_parent_.assign(joint_count, -1);
    for (size_t j = 0; j < joint_count; ++j) {
        const cgltf_node* parent = skin->joints[j]->parent;
        while (parent != nullptr && joint_of_node[nodeIndex(gltf_, parent)] < 0) {
            parent = parent->parent;
        }
        if (parent != nullptr) {
            joint_parent_[j] = joint_of_node[nodeIndex(gltf_, parent)];
        }
    }

    inverse_bind_.assign(joint_count, math::identity());
    if (skin->inverse_bind_matrices != nullptr) {
        std::vector<float> ibm;
        if (!readFloats(skin->inverse_bind_matrices, 16, ibm)) {
            return false;
        }
        for (size_t j = 0; j < joint_count; ++j) {
            std::memcpy(inverse_bind_[j].m, &ibm[j * 16], sizeof(inverse_bind_[j].m));
        }
    }

    joint_world_bind_.resize(joint_count);
    bind_local_.resize(joint_count);
    for (size_t j = 0; j < joint_count; ++j) {
        joint_world_bind_[j] = math::affineInverse(inverse_bind_[j]);
    }
    for (size_t j = 0; j < joint_count; ++j) {
        bind_local_[j] = joint_parent_[j] >= 0
            ? math::multiply(math::affineInverse(joint_world_bind_[joint_parent_[j]]), joint_world_bind_[j])
            : joint_world_bind_[j];
    }

    // Children lists, and each joint's rest transform for the ozz skeleton.
    std::vector<std::vector<int>> children(joint_count);
    std::vector<int> roots;
    for (size_t j = 0; j < joint_count; ++j) {
        if (joint_parent_[j] >= 0) {
            children[joint_parent_[j]].push_back(static_cast<int>(j));
        } else {
            roots.push_back(static_cast<int>(j));
        }
    }
    std::vector<Trs> rest(joint_count);
    for (size_t j = 0; j < joint_count; ++j) {
        rest[j] = decompose(bind_local_[j]);
    }

    // ozz skeleton, with joints in depth-first order.
    dfs_joint_.clear();
    ozz::animation::offline::RawSkeleton raw_skeleton;
    for (int r : roots) {
        ozz::animation::offline::RawSkeleton::Joint root_joint;
        appendJoint(root_joint, r, joint_nodes_, children, rest, dfs_joint_);
        raw_skeleton.roots.push_back(root_joint);
    }
    ozz::animation::offline::SkeletonBuilder skeleton_builder;
    skeleton_ = skeleton_builder(raw_skeleton);
    if (!skeleton_ || skeleton_->num_joints() != static_cast<int>(joint_count)) {
        return false;
    }

    // ozz animations: one track per joint, in depth-first order. Paths a clip
    // does not key hold the joint's bind pose.
    for (size_t a = 0; a < gltf_->animations_count; ++a) {
        const cgltf_animation& anim = gltf_->animations[a];
        float duration = 0.0f;
        for (size_t c = 0; c < anim.channels_count; ++c) {
            const cgltf_accessor* input = anim.channels[c].sampler->input;
            float last = 0.0f;
            if (input->count > 0) {
                cgltf_accessor_read_float(input, input->count - 1, &last, 1);
            }
            if (last > duration) {
                duration = last;
            }
        }
        if (duration <= 0.0f || anim.name == nullptr) {
            continue;
        }

        ozz::animation::offline::RawAnimation raw;
        raw.duration = duration;
        raw.tracks.resize(joint_count);
        for (size_t k = 0; k < joint_count; ++k) {
            const int j = dfs_joint_[k];
            const cgltf_node* node = static_cast<const cgltf_node*>(joint_nodes_[j]);
            auto& track = raw.tracks[k];
            for (size_t c = 0; c < anim.channels_count; ++c) {
                const cgltf_animation_channel& channel = anim.channels[c];
                if (channel.target_node != node) {
                    continue;
                }
                const cgltf_animation_sampler* sampler = channel.sampler;
                const size_t keys = sampler->input->count;
                for (size_t i = 0; i < keys; ++i) {
                    float time = 0.0f;
                    cgltf_accessor_read_float(sampler->input, i, &time, 1);
                    float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    if (channel.target_path == cgltf_animation_path_type_rotation) {
                        cgltf_accessor_read_float(sampler->output, i, v, 4);
                        track.rotations.push_back({time, ozz::math::Quaternion(v[0], v[1], v[2], v[3])});
                    } else {
                        cgltf_accessor_read_float(sampler->output, i, v, 3);
                        const ozz::math::Float3 value(v[0], v[1], v[2]);
                        if (channel.target_path == cgltf_animation_path_type_translation) {
                            track.translations.push_back({time, value});
                        } else if (channel.target_path == cgltf_animation_path_type_scale) {
                            track.scales.push_back({time, value});
                        }
                    }
                }
            }
            // Components with no keys hold the bind pose.
            if (track.translations.empty()) {
                track.translations.push_back({0.0f, ozz::math::Float3(rest[j].t[0], rest[j].t[1], rest[j].t[2])});
            }
            if (track.rotations.empty()) {
                track.rotations.push_back({0.0f, ozz::math::Quaternion(rest[j].r[0], rest[j].r[1], rest[j].r[2], rest[j].r[3])});
            }
            if (track.scales.empty()) {
                track.scales.push_back({0.0f, ozz::math::Float3(rest[j].s[0], rest[j].s[1], rest[j].s[2])});
            }
        }

        ozz::animation::offline::AnimationBuilder animation_builder;
        ozz::unique_ptr<ozz::animation::Animation> built = animation_builder(raw);
        if (!built) {
            continue;
        }
        animations_.push_back(std::move(built));
        animation_names_.push_back(anim.name);
    }

    // Foot IK: the leg chains and the sole's height below each ankle, measured
    // from the bind pose's foot and toe vertices.
    auto findJoint = [&](const char* name) -> int {
        for (size_t j = 0; j < joint_count; ++j) {
            const auto* node = static_cast<const cgltf_node*>(joint_nodes_[j]);
            if (node->name != nullptr && std::strcmp(node->name, name) == 0) {
                return static_cast<int>(j);
            }
        }
        return -1;
    };
    hips_ = findJoint("Hips");
    root_ = findJoint("Root");
    left_arm_[0] = findJoint("LeftArm");
    left_arm_[1] = findJoint("LeftForeArm");
    left_arm_[2] = findJoint("LeftHand");
    const char* sides[2][3] = {{"LeftUpLeg", "LeftLeg", "LeftFoot"}, {"RightUpLeg", "RightLeg", "RightFoot"}};
    const char* toes[2] = {"LeftToeBase", "RightToeBase"};
    has_feet_ = hips_ >= 0;
    for (int side = 0; side < 2 && has_feet_; ++side) {
        FootChain& chain = feet_[side];
        chain.up = findJoint(sides[side][0]);
        chain.leg = findJoint(sides[side][1]);
        chain.foot = findJoint(sides[side][2]);
        chain.toe = findJoint(toes[side]);
        if (chain.up < 0 || chain.leg < 0 || chain.foot < 0 || chain.toe < 0) {
            has_feet_ = false;
            break;
        }
        const float foot_y = joint_world_bind_[chain.foot].m[13];
        const float toe_y = joint_world_bind_[chain.toe].m[13];
        float lowest_foot = foot_y;
        float lowest_toe = toe_y;
        for (const MeshVertex& v : mesh_.vertices) {
            const int dominant = static_cast<int>(v.weights[0] >= v.weights[1] ? v.joints[0] : v.joints[1]);
            if (dominant == chain.foot && v.position[1] < lowest_foot) {
                lowest_foot = v.position[1];
            }
            if (dominant == chain.toe && v.position[1] < lowest_toe) {
                lowest_toe = v.position[1];
            }
        }
        chain.ankle = foot_y - lowest_foot;
        chain.toe_offset = toe_y - lowest_toe;
    }

    front_yaw_ = kMeshFrontYaw;
    return true;
}

void CharacterModel::cleanup() {
    if (gltf_ != nullptr) {
        cgltf_free(gltf_);
        gltf_ = nullptr;
    }
    file_bytes_.clear();
    file_bytes_.shrink_to_fit();
    mesh_ = CharacterMesh{};
    joint_nodes_.clear();
    joint_parent_.clear();
    inverse_bind_.clear();
    joint_world_bind_.clear();
    bind_local_.clear();
    skeleton_.reset();
    animations_.clear();
    animation_names_.clear();
    dfs_joint_.clear();
    front_yaw_ = 0.0f;
    has_feet_ = false;
    hips_ = -1;
    root_ = -1;
}

const CharacterMesh& CharacterModel::mesh() const {
    return mesh_;
}

float CharacterModel::frontYaw() const {
    return front_yaw_;
}

bool CharacterModel::hasClip(const char* name) const {
    for (const std::string& clip : animation_names_) {
        if (clip == name) {
            return true;
        }
    }
    return false;
}

int CharacterModel::jointCount() const {
    return static_cast<int>(joint_nodes_.size());
}

int CharacterModel::jointParent(int joint) const {
    return joint >= 0 && joint < static_cast<int>(joint_parent_.size()) ? joint_parent_[joint] : -1;
}

math::Mat4 CharacterModel::jointBindWorld(int joint) const {
    return joint >= 0 && joint < static_cast<int>(joint_world_bind_.size()) ? joint_world_bind_[joint] : math::identity();
}

int CharacterModel::jointIndex(const char* name) const {
    for (size_t j = 0; j < joint_nodes_.size(); ++j) {
        const auto* node = static_cast<const cgltf_node*>(joint_nodes_[j]);
        if (node->name != nullptr && std::strcmp(node->name, name) == 0) {
            return static_cast<int>(j);
        }
    }
    return -1;
}

float CharacterModel::clipDuration(const char* name) const {
    for (size_t a = 0; a < animation_names_.size(); ++a) {
        if (animation_names_[a] == name) {
            return animations_[a]->duration();
        }
    }
    return 0.0f;
}

void CharacterModel::evaluatePose(const char* clip_name, float time_seconds, const math::Mat4& root,
    const GroundHeightFn& ground, math::Mat4* out, const float* left_hand_goal) const {
    for (int j = 0; j < kMaxJoints; ++j) {
        out[j] = math::identity();
    }
    if (!skeleton_) {
        return;
    }

    // Sample the clip with ozz.
    size_t clip = animation_names_.size();
    for (size_t a = 0; a < animation_names_.size(); ++a) {
        if (animation_names_[a] == clip_name) {
            clip = a;
            break;
        }
    }
    if (clip == animation_names_.size()) {
        return;
    }
    const ozz::animation::Animation& animation = *animations_[clip];
    const float duration = animation.duration();
    const float t = duration > 0.0f ? std::fmod(time_seconds, duration) : 0.0f;

    const int joint_count = skeleton_->num_joints();
    if (!scratch_) {
        scratch_ = std::make_unique<SampleScratch>();
    }
    SampleScratch& scratch = *scratch_;
    if (!scratch.context || scratch.context_tracks != joint_count) {
        scratch.context = std::make_unique<ozz::animation::SamplingJob::Context>(joint_count);
        scratch.context_tracks = joint_count;
    }
    std::vector<ozz::math::SoaTransform>& soa = scratch.soa;
    soa.resize(skeleton_->num_soa_joints());
    ozz::animation::SamplingJob sampling;
    sampling.animation = &animation;
    sampling.context = scratch.context.get();
    sampling.ratio = duration > 0.0f ? t / duration : 0.0f;
    sampling.output = ozz::span<ozz::math::SoaTransform>(soa.data(), soa.size());
    if (!sampling.Run()) {
        return;
    }

    // Unpack the sampled locals, one joint at a time, in depth-first order.
    std::vector<math::Mat4>& locals = scratch.locals;
    locals.resize(joint_count);
    for (int k = 0; k < joint_count; ++k) {
        const ozz::math::SoaTransform& s = soa[k / 4];
        const int lane = k % 4;
        float tx[4], ty[4], tz[4], qx[4], qy[4], qz[4], qw[4], sx[4], sy[4], sz[4];
        ozz::math::StorePtrU(s.translation.x, tx);
        ozz::math::StorePtrU(s.translation.y, ty);
        ozz::math::StorePtrU(s.translation.z, tz);
        ozz::math::StorePtrU(s.rotation.x, qx);
        ozz::math::StorePtrU(s.rotation.y, qy);
        ozz::math::StorePtrU(s.rotation.z, qz);
        ozz::math::StorePtrU(s.rotation.w, qw);
        ozz::math::StorePtrU(s.scale.x, sx);
        ozz::math::StorePtrU(s.scale.y, sy);
        ozz::math::StorePtrU(s.scale.z, sz);
        const float t3[3] = {tx[lane], ty[lane], tz[lane]};
        const float r4[4] = {qx[lane], qy[lane], qz[lane], qw[lane]};
        const float s3[3] = {sx[lane], sy[lane], sz[lane]};
        locals[dfs_joint_[k]] = math::fromTrs(t3, r4, s3);
    }

    // The root path is not applied here: the game places the character itself.
    // Keep the Root joint at its bind translation so the body stays in place.
    if (root_ >= 0) {
        locals[root_].m[12] = bind_local_[root_].m[12];
        locals[root_].m[13] = bind_local_[root_].m[13];
        locals[root_].m[14] = bind_local_[root_].m[14];
    }

    if (ground && has_feet_) {
        applyFootIk(locals, root, ground);
    }

    if (left_hand_goal != nullptr && left_arm_[0] >= 0) {
        solveTwoBone(locals, root, left_arm_[0], left_arm_[1], left_arm_[2], left_hand_goal);
    }

    for (int j = 0; j < joint_count && j < kMaxJoints; ++j) {
        out[j] = math::multiply(jointModel(j, joint_parent_, locals, root), inverse_bind_[j]);
    }
}

// Two-bone IK: moves the end joint to goal, bending the middle joint on the side
// it already bends toward. Root and joint chain are in model space.
void CharacterModel::solveTwoBone(std::vector<math::Mat4>& locals, const math::Mat4& root, int up, int mid, int end,
    const float goal[3]) const {
    auto worldOf = [&](int j) { return jointModel(j, joint_parent_, locals, root); };
    auto setWorld = [&](int j, const math::Mat4& world) {
        const math::Mat4 parent = joint_parent_[j] >= 0 ? worldOf(joint_parent_[j]) : root;
        locals[j] = math::multiply(math::affineInverse(parent), world);
    };

    float hip[3], knee[3], ankle[3];
    position(worldOf(up), hip);
    position(worldOf(mid), knee);
    position(worldOf(end), ankle);

    // Aim the foot at the ground under it, partially for a swinging foot.

    // Two-bone solve in the plane through hip, knee, and goal.
    float hip_to_knee[3], knee_to_ankle[3], hip_to_goal[3];
    sub3(knee, hip, hip_to_knee);
    sub3(ankle, knee, knee_to_ankle);
    sub3(goal, hip, hip_to_goal);
    const float upper = length3(hip_to_knee);
    const float lower = length3(knee_to_ankle);
    float reach = length3(hip_to_goal);
    const float max_reach = (upper + lower) * 0.999f;
    if (reach > max_reach) {
        reach = max_reach;
    }
    if (reach < 1e-4f) {
        return;
    }
    float dir[3] = {hip_to_goal[0], hip_to_goal[1], hip_to_goal[2]};
    normalize3(dir);

    const float cos_hip = (upper * upper + reach * reach - lower * lower) / (2.0f * upper * reach);
    const float sin_hip = std::sqrt(std::fmax(0.0f, 1.0f - cos_hip * cos_hip));

    // The knee keeps the side it already bends toward.
    float plane_normal[3];
    cross3(dir, hip_to_knee, plane_normal);
    if (length3(plane_normal) < 1e-6f) {
        // Knee on the line to the goal: bend along the model's front.
        const float forward[3] = {0.0f, 0.0f, 1.0f};
        cross3(dir, forward, plane_normal);
    }
    normalize3(plane_normal);
    float bend[3];
    cross3(plane_normal, dir, bend);
    if (dot3(bend, hip_to_knee) < 0.0f) {
        bend[0] = -bend[0];
        bend[1] = -bend[1];
        bend[2] = -bend[2];
    }
    normalize3(bend);

    float new_knee[3], new_goal[3];
    for (int c = 0; c < 3; ++c) {
        new_knee[c] = hip[c] + upper * (cos_hip * dir[c] + sin_hip * bend[c]);
        new_goal[c] = hip[c] + reach * dir[c];
    }

    // Rotate the thigh so the knee lands on new_knee, then the shin so the
    // ankle lands on the goal. Each rotation pivots on its joint.
    float knee_dir[3], new_knee_dir[3];
    sub3(knee, hip, knee_dir);
    sub3(new_knee, hip, new_knee_dir);
    normalize3(knee_dir);
    normalize3(new_knee_dir);
    const math::Mat4 thigh_rotation = math::rotateAbout(math::rotationBetween(knee_dir, new_knee_dir), hip);

    float ankle_after[3];
    rotatePoint(thigh_rotation, hip, ankle, ankle_after);
    float shin_from[3], shin_to[3];
    sub3(ankle_after, new_knee, shin_from);
    sub3(new_goal, new_knee, shin_to);
    normalize3(shin_from);
    normalize3(shin_to);
    const math::Mat4 shin_rotation = math::rotateAbout(math::rotationBetween(shin_from, shin_to), new_knee);

    // All three worlds are read before any is set, so the order cannot matter.
    const math::Mat4 new_up = math::multiply(thigh_rotation, worldOf(up));
    const math::Mat4 new_leg = math::multiply(shin_rotation, math::multiply(thigh_rotation, worldOf(mid)));
    const math::Mat4 new_foot = math::multiply(shin_rotation, math::multiply(thigh_rotation, worldOf(end)));
    setWorld(up, new_up);
    setWorld(mid, new_leg);
    setWorld(end, new_foot);
}

void CharacterModel::applyFootIk(std::vector<math::Mat4>& locals, const math::Mat4& root, const GroundHeightFn& ground) const {
    auto worldOf = [&](int j) { return jointModel(j, joint_parent_, locals, root); };
    // Sets a joint's model transform by changing its local transform.
    auto setWorld = [&](int j, const math::Mat4& world) {
        const math::Mat4 parent = joint_parent_[j] >= 0 ? worldOf(joint_parent_[j]) : root;
        locals[j] = math::multiply(math::affineInverse(parent), world);
    };

    // How far each foot must move to meet the ground, weighted by how close it
    // already is: a planted foot follows the ground fully, a swinging one not at all.
    float weight[2] = {0.0f, 0.0f};
    float shift = 0.0f;
    for (int side = 0; side < 2; ++side) {
        const FootChain& chain = feet_[side];
        float p[3];
        position(worldOf(chain.foot), p);
        const float target_y = ground(p[0], p[2]) + chain.ankle;
        const float clearance = p[1] - target_y;
        const float t = (kSwingClearance - clearance) / (kSwingClearance - kPlantedClearance);
        weight[side] = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float needed = weight[side] * (target_y - p[1]);
        if (needed < shift) {
            shift = needed;
        }
    }

    // Drop the pelvis by half of the lower foot's shortfall, and let the foot
    // make up the rest. Dropping by all of it bends the other knee, which is
    // also on a different height. Never raise the pelvis.
    shift *= kPelvisShare;
    if (shift < -kMaxPelvisDrop) {
        shift = -kMaxPelvisDrop;
    }
    if (shift < 0.0f) {
        setWorld(hips_, math::multiply(math::translation(0.0f, shift, 0.0f), worldOf(hips_)));
    }

    for (int side = 0; side < 2; ++side) {
        if (weight[side] <= 0.0f) {
            continue;
        }
        const FootChain& chain = feet_[side];

        // Aim the foot at the ground under it, partially for a swinging foot.
        float goal_ankle[3];
        position(worldOf(chain.foot), goal_ankle);
        const float ground_y = ground(goal_ankle[0], goal_ankle[2]) + chain.ankle;
        const float goal[3] = {goal_ankle[0], goal_ankle[1] + weight[side] * (ground_y - goal_ankle[1]), goal_ankle[2]};
        solveTwoBone(locals, root, chain.up, chain.leg, chain.foot, goal);

        // Pitch the foot about the ankle so the toe meets the ground too. The
        // toe is a child of the foot, so it moves with it.
        float ankle_now[3], toe_now[3];
        position(worldOf(chain.foot), ankle_now);
        position(worldOf(chain.toe), toe_now);
        const float toe_goal_y = ground(toe_now[0], toe_now[2]) + chain.toe_offset;
        const float toe_clearance = toe_now[1] - toe_goal_y;
        if (toe_clearance < 0.0f) {  // the toe is below the ground: pitch it up
            float from[3], to[3];
            sub3(toe_now, ankle_now, from);
            to[0] = from[0];
            to[1] = from[1] - toe_clearance;
            to[2] = from[2];
            normalize3(from);
            normalize3(to);
            // Small adjustments only: a bigger pitch means the ground is not under this foot.
            if (dot3(from, to) > std::cos(0.6f)) {
                const math::Mat4 pitch = math::rotateAbout(math::rotationBetween(from, to), ankle_now);
                setWorld(chain.foot, math::multiply(pitch, worldOf(chain.foot)));
            }
        }
    }
}

} // namespace assets

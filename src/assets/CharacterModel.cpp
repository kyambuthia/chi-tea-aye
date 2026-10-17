#include "assets/CharacterModel.h"

#include <cmath>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace assets {
namespace {

// The mesh faces +Z (verified with counter-clockwise front faces).
constexpr float kMeshFrontYaw = 0.0f;

struct Trs {
    float t[3] = {0.0f, 0.0f, 0.0f};
    float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float s[3] = {1.0f, 1.0f, 1.0f};
};

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

math::Mat4 worldFromLocal(const cgltf_data* data, const cgltf_node* node, const std::vector<math::Mat4>& local) {
    const math::Mat4& self = local[nodeIndex(data, node)];
    if (node->parent == nullptr) {
        return self;
    }
    return math::multiply(worldFromLocal(data, node->parent, local), self);
}

// Spherical interpolation between unit quaternions stored as x, y, z, w.
void slerp(const float a[4], const float b[4], float t, float out[4]) {
    float cos_theta = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float end[4] = {b[0], b[1], b[2], b[3]};
    if (cos_theta < 0.0f) {
        cos_theta = -cos_theta;
        for (float& v : end) {
            v = -v;
        }
    }

    float w0 = 1.0f - t;
    float w1 = t;
    if (cos_theta < 0.9995f) {
        const float theta = std::acos(cos_theta);
        const float sin_theta = std::sin(theta);
        w0 = std::sin((1.0f - t) * theta) / sin_theta;
        w1 = std::sin(t * theta) / sin_theta;
    }

    float len = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = a[i] * w0 + end[i] * w1;
        len += out[i] * out[i];
    }
    len = std::sqrt(len);
    for (int i = 0; i < 4; ++i) {
        out[i] /= len;
    }
}

// Finds the keyframe pair around time t and writes the interpolated value.
void sampleChannel(const cgltf_animation_channel& channel, float t, Trs& trs) {
    const cgltf_animation_sampler* sampler = channel.sampler;
    const size_t key_count = sampler->input->count;
    if (key_count == 0) {
        return;
    }

    size_t k = 0;
    while (k + 1 < key_count) {
        float next_time = 0.0f;
        cgltf_accessor_read_float(sampler->input, k + 1, &next_time, 1);
        if (t < next_time) {
            break;
        }
        ++k;
    }

    size_t components = 3;
    if (channel.target_path == cgltf_animation_path_type_rotation) {
        components = 4;
    }

    float time0 = 0.0f;
    float time1 = 0.0f;
    cgltf_accessor_read_float(sampler->input, k, &time0, 1);
    size_t k1 = k + 1 < key_count ? k + 1 : k;
    cgltf_accessor_read_float(sampler->input, k1, &time1, 1);
    float alpha = 0.0f;
    if (time1 > time0) {
        alpha = (t - time0) / (time1 - time0);
    }
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    float v0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float v1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    cgltf_accessor_read_float(sampler->output, k, v0, components);
    cgltf_accessor_read_float(sampler->output, k1, v1, components);

    if (sampler->interpolation == cgltf_interpolation_type_step) {
        alpha = 0.0f;
    }

    switch (channel.target_path) {
        case cgltf_animation_path_type_translation:
            for (int c = 0; c < 3; ++c) {
                trs.t[c] = v0[c] + (v1[c] - v0[c]) * alpha;
            }
            break;
        case cgltf_animation_path_type_scale:
            for (int c = 0; c < 3; ++c) {
                trs.s[c] = v0[c] + (v1[c] - v0[c]) * alpha;
            }
            break;
        case cgltf_animation_path_type_rotation:
            slerp(v0, v1, alpha, trs.r);
            break;
        default:
            break;
    }
}

} // namespace

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
    const cgltf_accessor* position = findAttribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* normal = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor* joints = findAttribute(prim, cgltf_attribute_type_joints);
    const cgltf_accessor* weights = findAttribute(prim, cgltf_attribute_type_weights);
    if (position == nullptr || joints == nullptr || weights == nullptr || prim.indices == nullptr) {
        return false;
    }

    std::vector<float> pos_data;
    std::vector<float> normal_data;
    std::vector<float> uv_data;
    std::vector<float> joint_data;
    std::vector<float> weight_data;
    if (!readFloats(position, 3, pos_data) || !readFloats(joints, 4, joint_data) ||
        !readFloats(weights, 4, weight_data)) {
        return false;
    }
    if (normal != nullptr) {
        readFloats(normal, 3, normal_data);
    }
    if (uv != nullptr) {
        readFloats(uv, 2, uv_data);
    }

    mesh_.vertices.resize(position->count);
    for (size_t i = 0; i < position->count; ++i) {
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
            int width = 0;
            int height = 0;
            int channels = 0;
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

    const cgltf_skin* skin = skinned_node->skin;
    if (skin->joints_count > static_cast<size_t>(kMaxJoints)) {
        return false;
    }
    joint_nodes_.clear();
    for (size_t j = 0; j < skin->joints_count; ++j) {
        joint_nodes_.push_back(skin->joints[j]);
    }

    inverse_bind_.assign(skin->joints_count, math::identity());
    if (skin->inverse_bind_matrices != nullptr) {
        std::vector<float> ibm;
        if (!readFloats(skin->inverse_bind_matrices, 16, ibm)) {
            return false;
        }
        for (size_t j = 0; j < skin->joints_count; ++j) {
            std::memcpy(inverse_bind_[j].m, &ibm[j * 16], sizeof(inverse_bind_[j].m));
        }
    }

    // Rest pose of the rig the clips were authored on (each node's own transform).
    const size_t node_count = gltf_->nodes_count;
    rest_local_rig_.assign(node_count, math::identity());
    for (size_t n = 0; n < node_count; ++n) {
        cgltf_float m[16];
        cgltf_node_transform_local(&gltf_->nodes[n], m);
        std::memcpy(rest_local_rig_[n].m, m, sizeof(m));
    }

    // Rest pose the mesh is bound to, from the inverse bind matrices. Joint
    // world transforms are the inverses of the bind matrices; other nodes keep
    // their rig transform.
    std::vector<int> joint_of_node(node_count, -1);
    for (size_t j = 0; j < joint_nodes_.size(); ++j) {
        joint_of_node[nodeIndex(gltf_, static_cast<const cgltf_node*>(joint_nodes_[j]))] = static_cast<int>(j);
    }
    std::vector<math::Mat4> joint_world_bind(joint_nodes_.size());
    for (size_t j = 0; j < joint_nodes_.size(); ++j) {
        joint_world_bind[j] = math::affineInverse(inverse_bind_[j]);
    }

    rest_local_bind_ = rest_local_rig_;
    for (size_t j = 0; j < joint_nodes_.size(); ++j) {
        const auto* joint = static_cast<const cgltf_node*>(joint_nodes_[j]);
        math::Mat4 parent_world = math::identity();
        if (joint->parent != nullptr) {
            const int pj = joint_of_node[nodeIndex(gltf_, joint->parent)];
            parent_world = pj >= 0 ? joint_world_bind[pj] : worldFromLocal(gltf_, joint->parent, rest_local_rig_);
        }
        rest_local_bind_[nodeIndex(gltf_, joint)] = math::multiply(math::affineInverse(parent_world), joint_world_bind[j]);
    }

    front_yaw_ = kMeshFrontYaw;

    // Foot IK: the leg chains and the sole's height below each ankle, measured
    // from the bind pose's foot and toe vertices.
    auto findNode = [&](const char* name) -> int {
        for (size_t n = 0; n < node_count; ++n) {
            if (gltf_->nodes[n].name != nullptr && std::strcmp(gltf_->nodes[n].name, name) == 0) {
                return static_cast<int>(n);
            }
        }
        return -1;
    };
    hips_ = findNode("Hips");
    const char* sides[2][3] = {{"LeftUpLeg", "LeftLeg", "LeftFoot"}, {"RightUpLeg", "RightLeg", "RightFoot"}};
    const char* toes[2] = {"LeftToeBase", "RightToeBase"};
    has_feet_ = hips_ >= 0;
    for (int side = 0; side < 2 && has_feet_; ++side) {
        FootChain& chain = feet_[side];
        chain.up = findNode(sides[side][0]);
        chain.leg = findNode(sides[side][1]);
        chain.foot = findNode(sides[side][2]);
        const int toe = findNode(toes[side]);
        if (chain.up < 0 || chain.leg < 0 || chain.foot < 0 || toe < 0 ||
            joint_of_node[chain.foot] < 0 || joint_of_node[toe] < 0) {
            has_feet_ = false;
            break;
        }
        chain.toe = toe;
        const float foot_y = worldFromLocal(gltf_, &gltf_->nodes[chain.foot], rest_local_bind_).m[13];
        const float toe_y = worldFromLocal(gltf_, &gltf_->nodes[toe], rest_local_bind_).m[13];
        float lowest_foot = foot_y;
        float lowest_toe = toe_y;
        for (const MeshVertex& v : mesh_.vertices) {
            const int dominant = static_cast<int>(v.weights[0] >= v.weights[1] ? v.joints[0] : v.joints[1]);
            if (dominant == joint_of_node[chain.foot] && v.position[1] < lowest_foot) {
                lowest_foot = v.position[1];
            }
            if (dominant == joint_of_node[toe] && v.position[1] < lowest_toe) {
                lowest_toe = v.position[1];
            }
        }
        chain.ankle = foot_y - lowest_foot;
        chain.toe_offset = toe_y - lowest_toe;
    }
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
    inverse_bind_.clear();
    rest_local_rig_.clear();
    rest_local_bind_.clear();
    front_yaw_ = 0.0f;
    has_feet_ = false;
    hips_ = -1;
}

const CharacterMesh& CharacterModel::mesh() const {
    return mesh_;
}

float CharacterModel::frontYaw() const {
    return front_yaw_;
}

bool CharacterModel::hasClip(const char* name) const {
    if (gltf_ == nullptr) {
        return false;
    }
    for (size_t i = 0; i < gltf_->animations_count; ++i) {
        if (gltf_->animations[i].name != nullptr && std::strcmp(gltf_->animations[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

void CharacterModel::evaluatePose(const char* clip_name, float time_seconds, const math::Mat4& root,
    const GroundHeightFn& ground, math::Mat4* out) const {
    for (int j = 0; j < kMaxJoints; ++j) {
        out[j] = math::identity();
    }
    if (gltf_ == nullptr) {
        return;
    }

    const size_t node_count = gltf_->nodes_count;
    std::vector<Trs> trs(node_count);
    std::vector<bool> animated(node_count, false);

    for (size_t a = 0; a < gltf_->animations_count; ++a) {
        const cgltf_animation& anim = gltf_->animations[a];
        if (anim.name == nullptr || std::strcmp(anim.name, clip_name) != 0) {
            continue;
        }

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
        const float t = duration > 0.0f ? std::fmod(time_seconds, duration) : 0.0f;

        for (size_t c = 0; c < anim.channels_count; ++c) {
            const cgltf_animation_channel& channel = anim.channels[c];
            if (channel.target_node == nullptr) {
                continue;
            }
            const size_t n = nodeIndex(gltf_, channel.target_node);
            if (!animated[n]) {
                // Start from the node's own TRS, then override the animated parts.
                const cgltf_node& node = gltf_->nodes[n];
                if (node.has_translation) std::memcpy(trs[n].t, node.translation, sizeof(trs[n].t));
                if (node.has_rotation) std::memcpy(trs[n].r, node.rotation, sizeof(trs[n].r));
                if (node.has_scale) std::memcpy(trs[n].s, node.scale, sizeof(trs[n].s));
                animated[n] = true;
            }
            sampleChannel(channel, t, trs[n]);
        }
        break;
    }

    // Animated nodes use the clip's local transform as-is. The clips are
    // absolute in the rig they were authored on, so their world poses come out
    // right even though that rig's rest pose (A-pose) differs from the mesh's
    // bind pose (T-pose); skinning maps those world poses back onto the mesh.
    std::vector<math::Mat4> local(node_count);
    for (size_t n = 0; n < node_count; ++n) {
        local[n] = animated[n] ? math::fromTrs(trs[n].t, trs[n].r, trs[n].s) : rest_local_bind_[n];
    }

    if (ground && has_feet_) {
        applyFootIk(local, root, ground);
    }

    for (size_t j = 0; j < joint_nodes_.size(); ++j) {
        const auto* joint = static_cast<const cgltf_node*>(joint_nodes_[j]);
        out[j] = math::multiply(math::multiply(root, worldFromLocal(gltf_, joint, local)), inverse_bind_[j]);
    }
}

namespace {

// Foot targets: a foot closer than kPlantedClearance above the ground is pinned
// to it; beyond kSwingClearance it is left to the animation.
constexpr float kPlantedClearance = 0.10f;
constexpr float kSwingClearance = 0.20f;
constexpr float kMaxPelvisDrop = 0.25f;

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

} // namespace

void CharacterModel::applyFootIk(std::vector<math::Mat4>& local, const math::Mat4& root, const GroundHeightFn& ground) const {
    auto worldOf = [&](int n) {
        return math::multiply(root, worldFromLocal(gltf_, &gltf_->nodes[n], local));
    };
    // Sets a node's world transform by changing its local transform.
    auto setWorld = [&](int n, const math::Mat4& world) {
        const cgltf_node* node = &gltf_->nodes[n];
        const math::Mat4 parent = node->parent != nullptr
            ? worldOf(static_cast<int>(nodeIndex(gltf_, node->parent)))
            : root;
        local[n] = math::multiply(math::affineInverse(parent), world);
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

    // Drop the pelvis so the lower foot can reach the ground. Never raise it.
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

        float hip[3], knee[3], ankle[3];
        position(worldOf(chain.up), hip);
        position(worldOf(chain.leg), knee);
        position(worldOf(chain.foot), ankle);

        // Aim the foot at the ground under it, partially for a swinging foot.
        const float ground_y = ground(ankle[0], ankle[2]) + chain.ankle;
        const float goal[3] = {ankle[0], ankle[1] + weight[side] * (ground_y - ankle[1]), ankle[2]};

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
            continue;
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
        const math::Mat4 new_up = math::multiply(thigh_rotation, worldOf(chain.up));
        const math::Mat4 new_leg = math::multiply(shin_rotation, math::multiply(thigh_rotation, worldOf(chain.leg)));
        const math::Mat4 new_foot = math::multiply(shin_rotation, math::multiply(thigh_rotation, worldOf(chain.foot)));
        setWorld(chain.up, new_up);
        setWorld(chain.leg, new_leg);
        setWorld(chain.foot, new_foot);

        // Pitch the foot about the ankle so the toe meets the ground too. The
        // toe is a child of the foot, so it moves with it.
        float ankle_now[3], toe_now[3];
        position(worldOf(chain.foot), ankle_now);
        position(worldOf(chain.toe), toe_now);
        const float toe_goal_y = ground(toe_now[0], toe_now[2]) + chain.toe_offset;
        const float toe_clearance = toe_now[1] - toe_goal_y;
        const float pitch_weight = toe_clearance < 0.0f ? 1.0f : 0.0f;  // only pull the toe up
        if (pitch_weight > 0.0f) {
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

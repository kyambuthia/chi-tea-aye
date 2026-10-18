#include "physics/Physics.h"

#include "app/InputState.h"
#include "assets/CharacterModel.h"
#include "math/Mat4.h"
#include "world/World.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace physics {
namespace {

constexpr JPH::ObjectLayer kStaticLayer = 0;
constexpr JPH::ObjectLayer kMovingLayer = 1;
constexpr JPH::BroadPhaseLayer kStaticBroadPhase(0);
constexpr JPH::BroadPhaseLayer kMovingBroadPhase(1);
constexpr float kPhysicsStep = 1.0f / 60.0f;
constexpr int kMaxStepsPerFrame = 6;

bool g_jolt_registered = false;

void traceJolt(const char* format, ...) {
    std::fputs("[Jolt] ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

bool assertJolt(const char* expression, const char* message, const char* file, JPH::uint line) {
    std::fprintf(stderr, "[Jolt] assertion failed: %s (%s) at %s:%u\n", expression,
        message != nullptr ? message : "no message", file, line);
    return false;
}

void registerJolt() {
    if (g_jolt_registered) return;
    JPH::RegisterDefaultAllocator();
    JPH::Trace = traceJolt;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertJolt;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    g_jolt_registered = true;
}

JPH::Quat rotationOf(const math::Mat4& matrix) {
    const float m00 = matrix.m[0], m01 = matrix.m[4], m02 = matrix.m[8];
    const float m10 = matrix.m[1], m11 = matrix.m[5], m12 = matrix.m[9];
    const float m20 = matrix.m[2], m21 = matrix.m[6], m22 = matrix.m[10];
    const float trace = m00 + m11 + m22;
    float x, y, z, w;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (m21 - m12) / s;
        y = (m02 - m20) / s;
        z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        w = (m21 - m12) / s;
        x = 0.25f * s;
        y = (m01 + m10) / s;
        z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        w = (m02 - m20) / s;
        x = (m01 + m10) / s;
        y = 0.25f * s;
        z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        w = (m10 - m01) / s;
        x = (m02 + m20) / s;
        y = (m12 + m21) / s;
        z = 0.25f * s;
    }
    return JPH::Quat(x, y, z, w).Normalized();
}

struct BoneShape {
    const char* name;
    float a;
    float b;
    float c;
    bool box;
    bool sphere;
};

constexpr BoneShape kBones[] = {
    {"Hips", 0.15f, 0.11f, 0.10f, true, false},
    {"Spine", 0.07f, 0.08f, 0.0f, false, false},
    {"Spine1", 0.075f, 0.085f, 0.0f, false, false},
    {"Spine2", 0.08f, 0.09f, 0.0f, false, false},
    {"Neck", 0.035f, 0.045f, 0.0f, false, false},
    {"Head", 0.105f, 0.0f, 0.0f, false, true},
    {"LeftArm", 0.10f, 0.055f, 0.0f, false, false},
    {"LeftForeArm", 0.095f, 0.047f, 0.0f, false, false},
    {"LeftHand", 0.045f, 0.0f, 0.0f, false, true},
    {"RightArm", 0.10f, 0.055f, 0.0f, false, false},
    {"RightForeArm", 0.095f, 0.047f, 0.0f, false, false},
    {"RightHand", 0.045f, 0.0f, 0.0f, false, true},
    {"LeftUpLeg", 0.16f, 0.075f, 0.0f, false, false},
    {"LeftLeg", 0.15f, 0.062f, 0.0f, false, false},
    {"LeftFoot", 0.065f, 0.045f, 0.11f, true, false},
    {"RightUpLeg", 0.16f, 0.075f, 0.0f, false, false},
    {"RightLeg", 0.15f, 0.062f, 0.0f, false, false},
    {"RightFoot", 0.065f, 0.045f, 0.11f, true, false},
};

JPH::RefConst<JPH::Shape> makeBoneShape(const BoneShape& bone) {
    if (bone.box) {
        return new JPH::BoxShape(JPH::Vec3(bone.a, bone.b, bone.c), 0.01f);
    }
    if (bone.sphere) {
        return new JPH::SphereShape(bone.a);
    }
    return new JPH::CapsuleShape(bone.a, bone.b);
}

bool isAncestor(const assets::CharacterModel& model, int possible_ancestor, int joint) {
    for (int parent = model.jointParent(joint); parent >= 0; parent = model.jointParent(parent)) {
        if (parent == possible_ancestor) return true;
    }
    return false;
}

int jointDepth(const assets::CharacterModel& model, int joint) {
    int depth = 0;
    for (int parent = model.jointParent(joint); parent >= 0; parent = model.jointParent(parent)) ++depth;
    return depth;
}

} // namespace

struct Physics::Impl {
    std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broad_phase;
    std::unique_ptr<JPH::ObjectLayerPairFilterTable> layer_filter;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broad_phase_filter;
    std::unique_ptr<JPH::PhysicsSystem> system;
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    std::vector<JPH::BodyID> static_bodies;
    JPH::Ref<JPH::RagdollSettings> ragdoll_settings;
    JPH::Ref<JPH::Ragdoll> ragdoll;
    const assets::CharacterModel* character = nullptr;
    int hips_joint = -1;
    int hips_body = -1;
    std::vector<int> model_to_body;
    std::vector<int> body_to_model;
    std::vector<math::Mat4> initial_world;
    std::vector<math::Mat4> initial_local;
    std::vector<math::Mat4> body_world;
    std::vector<JPH::Mat44> ragdoll_joint_matrices;
    std::vector<math::Mat4> posed_world;
    std::vector<uint8_t> pose_state;
    math::Mat4 initial_hips_world = math::identity();
    float accumulator = 0.0f;
    bool initialized = false;

    void initialize(const world::World& world) {
        registerJolt();

        broad_phase = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(2, 2);
        broad_phase->MapObjectToBroadPhaseLayer(kStaticLayer, kStaticBroadPhase);
        broad_phase->MapObjectToBroadPhaseLayer(kMovingLayer, kMovingBroadPhase);

        layer_filter = std::make_unique<JPH::ObjectLayerPairFilterTable>(2);
        layer_filter->EnableCollision(kStaticLayer, kMovingLayer);
        layer_filter->EnableCollision(kMovingLayer, kMovingLayer);
        broad_phase_filter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
            *broad_phase, 2, *layer_filter, 2);

        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(1024, 0, 4096, 1024, *broad_phase, *broad_phase_filter, *layer_filter);
        temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
        jobs = std::make_unique<JPH::JobSystemSingleThreaded>(2048);
        addGround(world);
        addWalls(world);
        system->OptimizeBroadPhase();
        initialized = true;
    }

    void addGround(const world::World& scene) {
        // A dense grid over the rolling terrain, with a coarse flat surround that
        // matches World::groundHeight outside the park.
        std::vector<float> coordinates;
        coordinates.reserve(84);
        const float half_extent = world::World::kCityHalfSize;
        coordinates.push_back(-half_extent);
        coordinates.push_back(-world::World::kHalfSize);
        for (int i = -39; i <= 39; ++i) coordinates.push_back(0.5f * i);
        coordinates.push_back(world::World::kHalfSize);
        coordinates.push_back(half_extent);
        std::sort(coordinates.begin(), coordinates.end());
        coordinates.erase(std::unique(coordinates.begin(), coordinates.end()), coordinates.end());

        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        const uint32_t side = static_cast<uint32_t>(coordinates.size());
        vertices.reserve(side * side);
        triangles.reserve((side - 1) * (side - 1) * 2);
        for (float z : coordinates) {
            for (float x : coordinates) {
                vertices.emplace_back(x, scene.groundHeight(x, z), z);
            }
        }
        for (uint32_t z = 0; z + 1 < side; ++z) {
            for (uint32_t x = 0; x + 1 < side; ++x) {
                const uint32_t a = z * side + x;
                const uint32_t b = a + 1;
                const uint32_t c = a + side;
                const uint32_t d = c + 1;
                // This winding gives the top surface an upward-facing normal.
                triangles.emplace_back(a, c, b, 0);
                triangles.emplace_back(b, c, d, 0);
            }
        }

        JPH::MeshShapeSettings shape_settings(std::move(vertices), std::move(triangles));
        shape_settings.mActiveEdgeCosThresholdAngle = 0.98f;
        auto result = shape_settings.Create();
        if (result.HasError()) {
            traceJolt("could not create terrain collision mesh: %s", result.GetError().c_str());
            return;
        }
        JPH::BodyCreationSettings body_settings(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, kStaticLayer);
        addStaticBody(body_settings);
    }

    void addWalls(const world::World& scene) {
        for (const world::Box& wall : scene.walls()) {
            const JPH::Vec3 half((wall.max[0] - wall.min[0]) * 0.5f,
                (wall.max[1] - wall.min[1]) * 0.5f, (wall.max[2] - wall.min[2]) * 0.5f);
            if (half.GetX() <= 0.0f || half.GetY() <= 0.0f || half.GetZ() <= 0.0f) continue;
            const JPH::RVec3 center((wall.max[0] + wall.min[0]) * 0.5f,
                (wall.max[1] + wall.min[1]) * 0.5f, (wall.max[2] + wall.min[2]) * 0.5f);
            JPH::BodyCreationSettings body_settings(new JPH::BoxShape(half, 0.01f), center,
                JPH::Quat::sIdentity(), JPH::EMotionType::Static, kStaticLayer);
            addStaticBody(body_settings);
        }
    }

    void addStaticBody(const JPH::BodyCreationSettings& settings) {
        JPH::BodyID id = system->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (!id.IsInvalid()) static_bodies.push_back(id);
    }

    bool start(const assets::CharacterModel& model, const math::Mat4* skin_pose) {
        stop();
        if (!initialized || skin_pose == nullptr || model.jointCount() <= 0 || model.jointCount() > assets::kMaxJoints) {
            return false;
        }

        hips_joint = model.jointIndex("Hips");
        if (hips_joint < 0) return false;
        character = &model;
        const int joint_count = model.jointCount();
        initial_world.resize(joint_count);
        initial_local.resize(joint_count);
        for (int joint = 0; joint < joint_count; ++joint) {
            initial_world[joint] = math::multiply(skin_pose[joint], model.jointBindWorld(joint));
            const int parent = model.jointParent(joint);
            initial_local[joint] = parent >= 0
                ? math::multiply(math::affineInverse(initial_world[parent]), initial_world[joint])
                : initial_world[joint];
        }
        initial_hips_world = initial_world[hips_joint];

        struct SelectedBone {
            const BoneShape* shape;
            int joint;
            int depth;
        };
        std::vector<SelectedBone> selected;
        selected.reserve(std::size(kBones));
        for (const BoneShape& bone : kBones) {
            const int joint = model.jointIndex(bone.name);
            if (joint >= 0) selected.push_back({&bone, joint, jointDepth(model, joint)});
        }
        std::stable_sort(selected.begin(), selected.end(), [](const SelectedBone& a, const SelectedBone& b) {
            return a.depth < b.depth;
        });
        if (selected.empty() || model.jointIndex("Hips") < 0) {
            character = nullptr;
            return false;
        }

        model_to_body.assign(joint_count, -1);
        body_to_model.clear();
        ragdoll_settings = new JPH::RagdollSettings();
        ragdoll_settings->mSkeleton = new JPH::Skeleton();
        ragdoll_settings->mParts.resize(selected.size());
        for (size_t body = 0; body < selected.size(); ++body) {
            const SelectedBone& bone = selected[body];
            int parent_joint = model.jointParent(bone.joint);
            int parent_body = -1;
            while (parent_joint >= 0) {
                parent_body = model_to_body[parent_joint];
                if (parent_body >= 0) break;
                parent_joint = model.jointParent(parent_joint);
            }
            const int skeleton_joint = static_cast<int>(ragdoll_settings->mSkeleton->AddJoint(
                bone.shape->name, parent_body));
            model_to_body[bone.joint] = skeleton_joint;
            body_to_model.push_back(bone.joint);

            const math::Mat4& world_joint = initial_world[bone.joint];
            JPH::RagdollSettings::Part& part = ragdoll_settings->mParts[skeleton_joint];
            part.SetShape(makeBoneShape(*bone.shape).GetPtr());
            part.mPosition = JPH::RVec3(world_joint.m[12], world_joint.m[13], world_joint.m[14]);
            part.mRotation = rotationOf(world_joint);
            part.mMotionType = JPH::EMotionType::Dynamic;
            part.mObjectLayer = kMovingLayer;
            part.mEnhancedInternalEdgeRemoval = true;
            part.mNumPositionStepsOverride = 4;

            if (parent_body >= 0) {
                JPH::Ref<JPH::SwingTwistConstraintSettings> constraint = new JPH::SwingTwistConstraintSettings();
                constraint->mDrawConstraintSize = 0.1f;
                constraint->mPosition1 = constraint->mPosition2 = part.mPosition;
                constraint->mTwistAxis1 = constraint->mTwistAxis2 = JPH::Vec3::sAxisY();
                constraint->mPlaneAxis1 = constraint->mPlaneAxis2 = JPH::Vec3::sAxisZ();
                float swing = 0.9f;
                float twist = 0.65f;
                const std::string name(bone.shape->name);
                if (name == "Spine" || name == "Spine1" || name == "Spine2") {
                    swing = 0.4f;
                    twist = 0.35f;
                } else if (name == "Neck") {
                    swing = 0.55f;
                    twist = 0.5f;
                } else if (name.find("ForeArm") != std::string::npos || name.find("Leg") != std::string::npos) {
                    swing = 0.75f;
                    twist = 0.4f;
                } else if (name.find("UpLeg") != std::string::npos || name.find("Arm") != std::string::npos) {
                    swing = 1.15f;
                    twist = 0.8f;
                }
                constraint->mTwistMinAngle = -twist;
                constraint->mTwistMaxAngle = twist;
                constraint->mNormalHalfConeAngle = swing;
                constraint->mPlaneHalfConeAngle = swing;
                part.mToParent = constraint;
            }
        }

        ragdoll_settings->mSkeleton->CalculateParentJointIndices();
        ragdoll_settings->Stabilize();
        ragdoll_settings->CalculateConstraintPriorities();
        ragdoll_settings->DisableParentChildCollisions();
        ragdoll_settings->CalculateBodyIndexToConstraintIndex();
        hips_body = model_to_body[hips_joint];
        body_world.resize(body_to_model.size());
        ragdoll_joint_matrices.resize(body_to_model.size());
        posed_world.resize(joint_count);
        pose_state.resize(joint_count);
        ragdoll = ragdoll_settings->CreateRagdoll(1, 0, system.get());
        if (ragdoll == nullptr) {
            traceJolt("could not create ragdoll for character");
            stop();
            return false;
        }
        ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
        accumulator = 0.0f;
        updatePose();
        return true;
    }

    void update(float delta_time) {
        if (ragdoll == nullptr || system == nullptr) return;
        accumulator += std::clamp(delta_time, 0.0f, 0.1f);
        int steps = 0;
        while (accumulator >= kPhysicsStep && steps < kMaxStepsPerFrame) {
            system->Update(kPhysicsStep, 1, temp_allocator.get(), jobs.get());
            accumulator -= kPhysicsStep;
            ++steps;
        }
        if (steps == kMaxStepsPerFrame && accumulator >= kPhysicsStep) {
            accumulator = std::fmod(accumulator, kPhysicsStep);
        }
        updatePose();
    }

    void updatePose() {
        if (ragdoll == nullptr) return;
        JPH::RVec3 root_offset;
        ragdoll->GetPose(root_offset, ragdoll_joint_matrices.data());
        for (size_t body = 0; body < ragdoll_joint_matrices.size(); ++body) {
            const JPH::Mat44& matrix = ragdoll_joint_matrices[body];
            math::Mat4 world = math::identity();
            for (int column = 0; column < 3; ++column) {
                const JPH::Vec3 axis = matrix.GetColumn3(static_cast<JPH::uint>(column));
                world.m[column * 4 + 0] = axis.GetX();
                world.m[column * 4 + 1] = axis.GetY();
                world.m[column * 4 + 2] = axis.GetZ();
            }
            const JPH::Vec3 position = matrix.GetTranslation();
            world.m[12] = static_cast<float>(root_offset.GetX() + position.GetX());
            world.m[13] = static_cast<float>(root_offset.GetY() + position.GetY());
            world.m[14] = static_cast<float>(root_offset.GetZ() + position.GetZ());
            body_world[body] = world;
        }
    }

    bool getSkinPose(math::Mat4* out) {
        if (ragdoll == nullptr || character == nullptr || out == nullptr) return false;
        const int joint_count = character->jointCount();
        std::fill(pose_state.begin(), pose_state.end(), 0);
        const math::Mat4 hip_delta = math::multiply(body_world[hips_body], math::affineInverse(initial_hips_world));
        auto resolve = [&](auto&& self, int joint) -> void {
            if (pose_state[joint] == 2) return;
            if (pose_state[joint] == 1) return;
            pose_state[joint] = 1;
            const int body = model_to_body[joint];
            if (body >= 0) {
                posed_world[joint] = body_world[body];
            } else if (joint == hips_joint || isAncestor(*character, joint, hips_joint)) {
                posed_world[joint] = math::multiply(hip_delta, initial_world[joint]);
            } else {
                const int parent = character->jointParent(joint);
                if (parent >= 0) {
                    self(self, parent);
                    posed_world[joint] = math::multiply(posed_world[parent], initial_local[joint]);
                } else {
                    posed_world[joint] = math::multiply(hip_delta, initial_world[joint]);
                }
            }
            pose_state[joint] = 2;
        };
        for (int joint = 0; joint < joint_count; ++joint) resolve(resolve, joint);

        for (int joint = 0; joint < assets::kMaxJoints; ++joint) out[joint] = math::identity();
        for (int joint = 0; joint < joint_count; ++joint) {
            out[joint] = math::multiply(posed_world[joint], math::affineInverse(character->jointBindWorld(joint)));
        }
        return true;
    }

    void stop() {
        if (ragdoll != nullptr) {
            ragdoll->RemoveFromPhysicsSystem();
            ragdoll = nullptr;
        }
        ragdoll_settings = nullptr;
        character = nullptr;
        hips_joint = hips_body = -1;
        model_to_body.clear();
        body_to_model.clear();
        initial_world.clear();
        initial_local.clear();
        body_world.clear();
        ragdoll_joint_matrices.clear();
        posed_world.clear();
        pose_state.clear();
        accumulator = 0.0f;
    }

    bool hipsPosition(float& x, float& y, float& z) const {
        if (ragdoll == nullptr || hips_body < 0 || hips_body >= static_cast<int>(body_world.size())) return false;
        const math::Mat4& hips = body_world[hips_body];
        x = hips.m[12];
        y = hips.m[13];
        z = hips.m[14];
        return true;
    }

    void shutdown() {
        stop();
        if (system != nullptr) {
            JPH::BodyInterface& bodies = system->GetBodyInterface();
            for (JPH::BodyID id : static_bodies) {
                bodies.RemoveBody(id);
                bodies.DestroyBody(id);
            }
            static_bodies.clear();
        }
        system.reset();
        temp_allocator.reset();
        jobs.reset();
        broad_phase_filter.reset();
        layer_filter.reset();
        broad_phase.reset();
        if (g_jolt_registered) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
            g_jolt_registered = false;
        }
        initialized = false;
    }
};

Physics::Physics() = default;

Physics::~Physics() {
    cleanup();
}

void Physics::init(const world::World& world) {
    cleanup();
    impl_ = std::make_unique<Impl>();
    impl_->initialize(world);
}

void Physics::inputDirection(const app::InputState& input, float camera_azimuth, float& dir_x, float& dir_z) const {
    // Camera forward on the ground: from the camera toward the player.
    const float forward_x = -std::sin(camera_azimuth);
    const float forward_z = -std::cos(camera_azimuth);
    // Right is forward rotated a quarter turn clockwise seen from above.
    const float right_x = -forward_z;
    const float right_z = forward_x;

    // Keys and an analog stick add up; the result is capped at unit length below.
    const float input_x = (input.keys.d ? 1.0f : 0.0f) - (input.keys.a ? 1.0f : 0.0f) + input.move_x;
    const float input_z = (input.keys.w ? 1.0f : 0.0f) - (input.keys.s ? 1.0f : 0.0f) + input.move_y;

    dir_x = input_x * right_x + input_z * forward_x;
    dir_z = input_x * right_z + input_z * forward_z;
    const float length = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    if (length > 1.0f) {
        dir_x /= length;
        dir_z /= length;
    }
}

void Physics::integratePlayer(float dir_x, float dir_z, float speed, float delta_time, float& x, float& z) const {
    x += dir_x * speed * delta_time;
    z += dir_z * speed * delta_time;
}

bool Physics::startRagdoll(const assets::CharacterModel& character, const math::Mat4* skin_pose) {
    return impl_ != nullptr && impl_->start(character, skin_pose);
}

void Physics::updateRagdoll(float delta_time) {
    if (impl_ != nullptr) impl_->update(delta_time);
}

void Physics::stopRagdoll() {
    if (impl_ != nullptr) impl_->stop();
}

bool Physics::ragdollActive() const {
    return impl_ != nullptr && impl_->ragdoll != nullptr;
}

const assets::CharacterModel* Physics::ragdollCharacter() const {
    return impl_ != nullptr ? impl_->character : nullptr;
}

bool Physics::ragdollSkinPose(math::Mat4* out) {
    return impl_ != nullptr && impl_->getSkinPose(out);
}

bool Physics::ragdollHipsPosition(float& x, float& y, float& z) const {
    return impl_ != nullptr && impl_->hipsPosition(x, y, z);
}

void Physics::cleanup() {
    if (impl_ != nullptr) impl_->shutdown();
    impl_.reset();
}

} // namespace physics

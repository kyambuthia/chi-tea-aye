#pragma once

#include <memory>

namespace assets {
class CharacterModel;
}

namespace math {
struct Mat4;
}

namespace app {
struct InputState;
}

namespace world {
class World;
}

namespace physics {

class Physics {
public:
    Physics();
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    void init(const world::World& world);

    // Ground direction from the input, relative to the camera: W moves away
    // from it, S toward it, and A and D strafe. Unit length, or zero for no input.
    void inputDirection(const app::InputState& input, float camera_azimuth, float& dir_x, float& dir_z) const;

    // Moves the player along a direction at a speed in meters per second.
    void integratePlayer(float dir_x, float dir_z, float speed, float delta_time, float& x, float& z) const;

    // Starts a ragdoll from the character's current world-space skin pose.
    bool startRagdoll(const assets::CharacterModel& character, const math::Mat4* skin_pose);
    void updateRagdoll(float delta_time);
    void stopRagdoll();
    bool ragdollActive() const;
    const assets::CharacterModel* ragdollCharacter() const;
    bool ragdollSkinPose(math::Mat4* out);
    bool ragdollHipsPosition(float& x, float& y, float& z) const;

    void cleanup();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace physics

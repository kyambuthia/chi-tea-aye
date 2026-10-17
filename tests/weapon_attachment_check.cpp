// Checks that a weapon's model transform composes correctly with the hand:
// the grip lands on the hand, and the prop turns with the hand.
// Build and run from the repository root:
//   cmake --build build --target weapon_attachment_check && ./build/weapon_attachment_check

#include "gameplay/WeaponAttachment.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;
int failures = 0;

void expectNear(float got, float want, float tolerance, const char* what) {
    if (std::fabs(got - want) > tolerance) {
        ++failures;
        std::printf("  FAIL %s: got %.4f want %.4f\n", what, got, want);
    }
}

// Applies a transform to a point (affine).
void apply(const math::Mat4& m, const float p[3], float out[3]) {
    for (int r = 0; r < 3; ++r) {
        out[r] = m.m[0 * 4 + r] * p[0] + m.m[1 * 4 + r] * p[1] + m.m[2 * 4 + r] * p[2] + m.m[3 * 4 + r];
    }
}

} // namespace

int main() {
    // Hand at (1, 2, 3), turned 90 degrees about Y: its local +X points along world -Z.
    const math::Mat4 hand = math::multiply(math::translation(1.0f, 2.0f, 3.0f), math::rotationY(kPi * 0.5f));
    // Grip offset: the prop's grip sits 0.1 m out along the hand's +X.
    const math::Mat4 grip = math::translation(0.1f, 0.0f, 0.0f);

    const math::Mat4 model = gameplay::makeWeaponModelTransform(hand, grip);

    // The grip (origin of the prop) lands at hand + 0.1 along the hand's +X, which is world -Z.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float at[3];
    apply(model, origin, at);
    expectNear(at[0], 1.0f, 1e-4f, "grip x");
    expectNear(at[1], 2.0f, 1e-4f, "grip y");
    expectNear(at[2], 2.9f, 1e-4f, "grip z");

    // The prop's own +Z (barrel) follows the hand's rotation: hand Z maps to world +X.
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    float barrel[3];
    apply(model, forward, barrel);
    expectNear(barrel[0] - at[0], 1.0f, 1e-4f, "barrel follows hand x");
    expectNear(barrel[2] - at[2], 0.0f, 1e-4f, "barrel follows hand z");

    // An identity grip puts the prop at the hand itself.
    const math::Mat4 bare = gameplay::makeWeaponModelTransform(hand, math::identity());
    apply(bare, origin, at);
    expectNear(at[0], 1.0f, 1e-4f, "identity grip x");
    expectNear(at[1], 2.0f, 1e-4f, "identity grip y");
    expectNear(at[2], 3.0f, 1e-4f, "identity grip z");

    // Aimed rifle: the grip lands on the hand, the muzzle runs along the aim, and
    // the basis is a proper rotation (the rifle is not mirrored).
    {
        const float grip_world[3] = {1.0f, 2.0f, 3.0f};
        const float grip_local[3] = {0.0f, -0.05f, 0.12f};
        const float level[3] = {0.0f, 0.0f, 1.0f};
        const math::Mat4 rifle = gameplay::makeAimedWeaponTransform(grip_world, level, grip_local);
        float at[3];
        apply(rifle, grip_local, at);
        expectNear(at[0], 1.0f, 1e-4f, "aimed grip x");
        expectNear(at[1], 2.0f, 1e-4f, "aimed grip y");
        expectNear(at[2], 3.0f, 1e-4f, "aimed grip z");
        const float muzzle[3] = {0.0f, 0.0f, -0.475f};
        // The rifle's axis runs from its centre to its muzzle.
        const float centre[3] = {0.0f, 0.0f, 0.0f};
        float tip[3], mid[3];
        apply(rifle, muzzle, tip);
        apply(rifle, centre, mid);
        expectNear(tip[0] - mid[0], 0.0f, 1e-4f, "level muzzle stays on the aim x");
        expectNear(tip[2] - mid[2], -0.475f * 1.0f * -1.0f, 1e-3f, "level muzzle runs along the aim");
        expectNear(tip[1] - mid[1], 0.0f, 1e-3f, "level muzzle stays level");

        // Aimed down and to the side: the muzzle follows the aim exactly.
        const float aim_down[3] = {0.6f, -0.3f, 0.7416198f};
        const math::Mat4 aimed = gameplay::makeAimedWeaponTransform(grip_world, aim_down, grip_local);
        apply(aimed, centre, mid);
        apply(aimed, muzzle, tip);
        const float along[3] = {tip[0] - mid[0], tip[1] - mid[1], tip[2] - mid[2]};
        const float length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        expectNear(along[0] / length, aim_down[0], 1e-3f, "aimed muzzle x follows the aim");
        expectNear(along[1] / length, aim_down[1], 1e-3f, "aimed muzzle y follows the aim");
        expectNear(along[2] / length, aim_down[2], 1e-3f, "aimed muzzle z follows the aim");

        // Proper rotation: the image of +X crossed with +Y is the image of +Z.
        float ix[3], iy[3], iz[3], origin[3], x_axis[3] = {1, 0, 0}, y_axis[3] = {0, 1, 0}, z_axis[3] = {0, 0, 1};
        const float zero[3] = {0, 0, 0};
        apply(aimed, zero, origin);
        apply(aimed, x_axis, ix);
        apply(aimed, y_axis, iy);
        apply(aimed, z_axis, iz);
        const float cross[3] = {
            (ix[1] - origin[1]) * (iy[2] - origin[2]) - (ix[2] - origin[2]) * (iy[1] - origin[1]),
            (ix[2] - origin[2]) * (iy[0] - origin[0]) - (ix[0] - origin[0]) * (iy[2] - origin[2]),
            (ix[0] - origin[0]) * (iy[1] - origin[1]) - (ix[1] - origin[1]) * (iy[0] - origin[0]),
        };
        expectNear(cross[0], iz[0] - origin[0], 1e-3f, "basis is right-handed (x)");
        expectNear(cross[1], iz[1] - origin[1], 1e-3f, "basis is right-handed (y)");
        expectNear(cross[2], iz[2] - origin[2], 1e-3f, "basis is right-handed (z)");
    }

    std::printf(failures == 0 ? "WEAPON ATTACHMENT OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

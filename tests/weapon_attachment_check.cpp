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

    std::printf(failures == 0 ? "WEAPON ATTACHMENT OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

// Checks the weapon wheel's sectors: rifle on the top, then pistol and fists
// clockwise, and no pick inside the dead zone.
// Build and run from the repository root:
//   cmake --build build --target wheel_check && ./build/wheel_check

#include "ui/WeaponWheel.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCx = 400.0f;
constexpr float kCy = 300.0f;
constexpr float kRadius = 150.0f;
constexpr float kDead = 40.0f;

// The screen point at a clockwise angle from the top of the wheel.
void pointAt(float degrees, float radius, float& x, float& y) {
    const float rad = degrees * kPi / 180.0f;
    x = kCx + radius * std::sin(rad);
    y = kCy - radius * std::cos(rad);
}

int failures = 0;

void expectSlot(float degrees, int want) {
    float x = 0.0f;
    float y = 0.0f;
    pointAt(degrees, kRadius, x, y);
    const int got = ui::weaponWheelSlot(x, y, kCx, kCy, kDead);
    if (got != want) {
        ++failures;
        std::printf("  FAIL: %.0f deg -> slot %d (want %d)\n", degrees, got, want);
    }
}

} // namespace

int main() {
    expectSlot(0.0f, 0);      // top: rifle
    expectSlot(30.0f, 0);     // upper right, still rifle
    expectSlot(100.0f, 1);    // right: pistol
    expectSlot(200.0f, 2);    // bottom: fists
    expectSlot(330.0f, 0);    // upper left, still rifle
    expectSlot(290.0f, 2);    // left: fists

    // The dead zone at the centre picks nothing.
    const int centre = ui::weaponWheelSlot(kCx + 5.0f, kCy - 5.0f, kCx, kCy, kDead);
    if (centre != -1) {
        ++failures;
        std::printf("  FAIL: the centre picked slot %d (want -1)\n", centre);
    }

    std::printf(failures == 0 ? "WHEEL CHECK OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

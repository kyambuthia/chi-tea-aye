// Checks the HUD's geometry: the minimap puts what is ahead of the camera at the top and
// the camera's right on the right, the player arrow points the right way, the touch
// buttons sit on screen without overlapping and hit-test where they are drawn, and the
// stick dead zone ignores drift but reaches full tilt.
// Build and run from the repository root:
//   cmake --build build --target hud_check && ./build/hud_check

#include "ui/HudLayout.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    }
}

constexpr float kPi = 3.14159265f;

} // namespace

int main() {
    // 1. Minimap orientation for a camera behind the start point (azimuth pi looks along +Z).
    {
        ui::RadarView view;
        view.screen_x = 100.0f;
        view.screen_y = 100.0f;
        view.azimuth = kPi;
        float x, y;
        view.toScreen(0.0f, 20.0f, x, y);  // ahead
        expect(y < 100.0f && std::fabs(x - 100.0f) < 1e-3f, "a point ahead of the camera is above the centre");
        view.toScreen(-20.0f, 0.0f, x, y);  // facing +Z, -X is the camera's right
        expect(x > 100.0f && std::fabs(y - 100.0f) < 1e-3f, "a point on the camera's right is right of the centre");
        expect(std::fabs(view.screenAngle(0.0f)) < 1e-5f, "facing the view direction draws the arrow straight up");
        expect(view.screenAngle(0.5f * kPi) < 0.0f, "turning left (toward +X) turns the arrow anticlockwise");

        // Turning the camera turns the map: after a quarter turn, the same point moves.
        view.azimuth = 0.5f * kPi;  // looking along -X
        view.toScreen(-20.0f, 0.0f, x, y);
        expect(y < 100.0f, "the map turns with the camera");
        expect(std::fabs(view.reach() - view.half_size * view.metres_per_pixel) < 1e-4f, "reach is the edge distance");
    }

    // 2. Touch buttons: on screen, apart, and hit where drawn, on foot and driving.
    for (const bool driving : {false, true}) {
        for (const auto& size : {std::pair<float, float>{1280.0f, 720.0f}, {844.0f, 390.0f}, {1024.0f, 1366.0f}}) {
            const ui::TouchLayout layout = ui::TouchLayout::make(size.first, size.second, driving);
            for (int i = 0; i < ui::TouchLayout::kCount; ++i) {
                const ui::TouchButton& a = layout.buttons[i];
                if (!a.visible) continue;
                expect(a.x - a.radius >= 0.0f && a.x + a.radius <= size.first && a.y - a.radius >= 0.0f && a.y + a.radius <= size.second,
                    "a touch button is fully on screen");
                expect(a.x > 0.5f * size.first, "touch buttons are on the right half, clear of the stick");
                expect(layout.hit(a.x, a.y) == i, "a touch at a button's centre hits it");
                for (int j = i + 1; j < ui::TouchLayout::kCount; ++j) {
                    const ui::TouchButton& b = layout.buttons[j];
                    if (!b.visible) continue;
                    const float d = std::hypot(a.x - b.x, a.y - b.y);
                    expect(d > (a.radius + b.radius) * 1.15f, "touch buttons do not overlap");
                }
            }
            expect(layout.hit(0.25f * size.first, 0.5f * size.second) == -1, "the stick area hits no button");
            expect(layout.buttons[ui::TouchLayout::kFire].visible == !driving, "fire shows on foot only");
            expect(layout.buttons[ui::TouchLayout::kHandbrake].visible == driving, "the handbrake shows only when driving");
        }
    }

    // 3. Stick dead zone.
    {
        float x = 0.1f, y = 0.1f;
        ui::stickDeadzone(x, y, 0.2f);
        expect(x == 0.0f && y == 0.0f, "drift inside the dead zone is ignored");
        x = 1.0f;
        y = 0.0f;
        ui::stickDeadzone(x, y, 0.2f);
        expect(std::fabs(x - 1.0f) < 1e-5f, "full tilt still reaches 1");
        x = 0.6f;
        y = 0.0f;
        ui::stickDeadzone(x, y, 0.2f);
        expect(std::fabs(x - 0.5f) < 1e-5f, "the range past the dead zone is rescaled");
    }

    std::printf(failures == 0 ? "HUD OK\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

#pragma once

#include <algorithm>
#include <cmath>

// Pure HUD geometry: the minimap transform and the touch button layout. No ImGui, so
// the app (for hit tests), the HUD (for drawing) and the checks all share one answer.
namespace ui {

// The minimap: a square on screen, centred on a world point, turned so the camera's
// view direction points up the screen.
struct RadarView {
    float screen_x = 0.0f;  // centre on screen, pixels
    float screen_y = 0.0f;
    float half_size = 90.0f;      // pixels from the centre to an edge
    float metres_per_pixel = 0.9f;
    float world_x = 0.0f;   // world point at the centre
    float world_z = 0.0f;
    float azimuth = 0.0f;   // the camera azimuth (the view looks along azimuth + pi)

    // World ground point to screen pixels.
    void toScreen(float x, float z, float& sx, float& sy) const {
        const float dx = x - world_x;
        const float dz = z - world_z;
        // Camera forward on the ground is (-sin az, -cos az); its right is (cos az, -sin az).
        const float forward = -dx * std::sin(azimuth) - dz * std::cos(azimuth);
        const float right = dx * std::cos(azimuth) - dz * std::sin(azimuth);
        sx = screen_x + right / metres_per_pixel;
        sy = screen_y - forward / metres_per_pixel;
    }

    // A world heading (radians about +Y, 0 faces +Z) as a screen angle for drawing an
    // arrow: 0 points up the screen, growing clockwise.
    float screenAngle(float heading) const {
        constexpr float kPi = 3.14159265f;
        const float view = azimuth + kPi;  // the camera's own heading
        float a = std::fmod(-(heading - view) + kPi, 2.0f * kPi);
        if (a < 0.0f) a += 2.0f * kPi;
        return a - kPi;
    }

    // Metres from the centre to the edge of the map.
    float reach() const { return half_size * metres_per_pixel; }
};

// On-screen touch buttons, on the right side of the screen, sized for thumbs.
struct TouchButton {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    const char* label = "";
    bool visible = false;
};

struct TouchLayout {
    enum Id { kFire, kAim, kUse, kCrouch, kHandbrake, kWeapon, kCount };
    TouchButton buttons[kCount];

    // driving: the fire, aim, crouch and weapon buttons give way to the handbrake.
    static TouchLayout make(float width, float height, bool driving) {
        TouchLayout layout;
        const float unit = std::min(width, height);
        const float big = 0.085f * unit;
        const float small = 0.06f * unit;
        const float right = width - 0.06f * unit;
        const float bottom = height - 0.08f * unit;
        auto place = [&](Id id, float x, float y, float r, const char* label, bool visible) {
            layout.buttons[id] = TouchButton{x, y, r, label, visible};
        };
        place(kFire, right - big, bottom - big, big, "FIRE", !driving);
        place(kAim, right - big * 3.3f, bottom - big * 0.7f, small, "AIM", !driving);
        place(kCrouch, right - big * 0.6f, bottom - big * 3.2f, small, "DUCK", !driving);
        place(kWeapon, right - big * 2.6f, bottom - big * 2.7f, small, "SWAP", !driving);
        place(kHandbrake, right - big, bottom - big, big, "BRAKE", driving);
        place(kUse, right - big * 0.6f, 0.08f * unit + small, small, driving ? "EXIT" : "CAR", true);
        return layout;
    }

    // The visible button under a point, or -1.
    int hit(float x, float y) const {
        for (int i = 0; i < kCount; ++i) {
            const TouchButton& b = buttons[i];
            const float dx = x - b.x;
            const float dy = y - b.y;
            // A little slack beyond the drawn circle: thumbs are not precise.
            if (b.visible && dx * dx + dy * dy <= (b.radius * 1.15f) * (b.radius * 1.15f)) {
                return i;
            }
        }
        return -1;
    }
};

// Applies a radial dead zone to a stick and rescales what is left to 0..1, so small
// drift is ignored and the full range is still reachable.
inline void stickDeadzone(float& x, float& y, float deadzone) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= deadzone) {
        x = 0.0f;
        y = 0.0f;
        return;
    }
    const float scaled = std::min(1.0f, (length - deadzone) / (1.0f - deadzone));
    x = x / length * scaled;
    y = y / length * scaled;
}

} // namespace ui

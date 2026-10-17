#include "ui/WeaponWheel.h"

#include <cmath>

namespace ui {

int weaponWheelSlot(float x, float y, float cx, float cy, float dead_radius) {
    const float dx = x - cx;
    const float dy = y - cy;
    if (dx * dx + dy * dy < dead_radius * dead_radius) {
        return -1;
    }
    // Clockwise from the top of the screen (screen y grows downward).
    const float kPi = 3.14159265358979323846f;
    float angle_deg = std::atan2(dx, -dy) * 180.0f / kPi;
    // Each sector is 120 degrees, centred on its direction: rifle on the top.
    float shifted = std::fmod(angle_deg + 60.0f + 720.0f, 360.0f);
    int slot = static_cast<int>(shifted / 120.0f);
    return slot >= kWeaponWheelSlots ? kWeaponWheelSlots - 1 : slot;
}

} // namespace ui

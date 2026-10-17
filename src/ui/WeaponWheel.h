#pragma once

namespace ui {

// The weapon wheel has three sectors, clockwise from the top: rifle, pistol, fists.
constexpr int kWeaponWheelSlots = 3;

// The sector under a point, for a wheel centred at (cx, cy). Returns -1 inside
// the dead zone at the centre, where no weapon is picked.
int weaponWheelSlot(float x, float y, float cx, float cy, float dead_radius);

} // namespace ui

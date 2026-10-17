#pragma once

namespace app {

struct KeyState {
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool shift = false;
};

struct MouseState {
    float x = 0.0f;
    float y = 0.0f;
};

struct InputState {
    KeyState keys;
    MouseState mouse;
    // Camera orbit and zoom accumulated since the last update.
    bool orbit_drag = false;
    // Weapons: one-frame requests, and aiming held with the right button.
    int weapon_request = -1;  // 0 rifle, 1 pistol, 2 fists; -1 none
    bool attack_pressed = false;
    bool aim_held = false;
    float look_dx = 0.0f;
    float look_dy = 0.0f;
    float zoom_steps = 0.0f;
};

} // namespace app

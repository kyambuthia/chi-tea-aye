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

// Everything the simulation reads from the player in one frame. Keyboard, mouse,
// gamepad and touch all write into this, so gameplay never sees a device.
struct InputState {
    KeyState keys;
    KeyState tapped;  // movement keys pressed since the last update
    MouseState mouse;
    // Camera orbit and zoom accumulated since the last update.
    bool orbit_drag = false;
    // Weapons: one-frame requests, and aiming held with the right button.
    int weapon_request = -1;  // 0 rifle, 1 pistol, 2 fists; -1 none
    bool attack_pressed = false;
    bool fire_pressed = false;
    bool crouch_pressed = false;
    bool inspect_pressed = false;  // F3: toggles the close inspection camera
    // F or gamepad Y: get into the nearest car, or out of the current one. On foot
    // with no car in reach, F punches instead.
    bool use_pressed = false;
    bool fire_held = false;  // the fire button (left mouse, Space, or a trigger) is down
    bool aim_held = false;
    float look_dx = 0.0f;
    float look_dy = 0.0f;
    float zoom_steps = 0.0f;

    // Analog movement from a stick: x to the right, y forward, each -1..1. Added to the keys.
    float move_x = 0.0f;
    float move_y = 0.0f;
    bool run_held = false;  // gamepad sprint; the keyboard uses Shift

    // Driving: analog pedals (gamepad triggers) are added to W and S.
    float throttle = 0.0f;
    float brake = 0.0f;
    bool handbrake_held = false;  // Space or a shoulder button
};

} // namespace app

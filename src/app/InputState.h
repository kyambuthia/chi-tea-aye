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
    float look_dx = 0.0f;
    float look_dy = 0.0f;
    float zoom_steps = 0.0f;
};

} // namespace app

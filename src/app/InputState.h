#pragma once

namespace app {

struct KeyState {
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
};

struct MouseState {
    float x = 0.0f;
    float y = 0.0f;
};

struct InputState {
    KeyState keys;
    MouseState mouse;
};

} // namespace app

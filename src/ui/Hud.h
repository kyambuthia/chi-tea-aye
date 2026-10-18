#pragma once

namespace gameplay {
class GameState;
}

namespace world {
class World;
}

namespace ui {

// What the HUD needs from the app beyond the game state.
struct HudContext {
    float width = 0.0f;   // window size in points
    float height = 0.0f;
    bool touch = false;            // a touch screen is in use: show the touch buttons
    unsigned touch_pressed = 0;    // bit per TouchLayout button held down
    bool pointer_captured = false; // the mouse is captured for looking around
    bool gamepad = false;          // the last input came from a gamepad: prompts name its buttons
    bool show_capture_hint = false;
};

// The player-facing overlay: minimap, speedometer, prompts and touch buttons.
// Drawn with ImGui's foreground list between DebugUi::beginFrame and render.
class Hud {
public:
    void draw(const gameplay::GameState& gameplay, const world::World& world, const HudContext& context);
};

} // namespace ui

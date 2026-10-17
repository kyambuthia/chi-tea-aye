#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace app {
struct InputState;
}

namespace gameplay {
class GameState;
}

namespace renderer {
struct StyleSettings;
}

namespace debug {

class DebugUi {
public:
    // Requires the GL context to be current; glsl_version is e.g. "#version 330".
    void init(SDL_Window* window, SDL_GLContext context, const char* glsl_version);
    void beginFrame();
    // clip_time and clip_duration describe the clip as the renderer last played it.
    void draw(const gameplay::GameState& gameplay, const app::InputState& input, renderer::StyleSettings& style, uint64_t frame_count, float delta_time,
        float clip_time, float clip_duration);
    void render();
    bool handleEvent(const SDL_Event* event);
    bool wantsMouse() const;
    bool wantsKeyboard() const;
    // The weapon wheel, drawn on top of the scene. hovered is the sector under the pointer, or -1.
    void drawWeaponWheel(float cx, float cy, float radius, int hovered);
    // Whether the camera recenters behind the warrior after orbiting (set from the panel).
    bool autoRecenter() const;
    void cleanup();

private:
    bool auto_recenter_ = false;
};

} // namespace debug

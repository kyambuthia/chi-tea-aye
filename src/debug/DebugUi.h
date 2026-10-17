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
    void cleanup();
};

} // namespace debug

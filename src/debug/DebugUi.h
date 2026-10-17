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
    void draw(const gameplay::GameState& gameplay, const app::InputState& input, renderer::StyleSettings& style, uint64_t frame_count, float delta_time);
    void render();
    bool handleEvent(const SDL_Event* event);
    void cleanup();
};

} // namespace debug

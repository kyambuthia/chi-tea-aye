#pragma once

#include <cstdint>

struct sapp_event;

namespace app {
struct InputState;
}

namespace gameplay {
class GameState;
}

namespace debug {

class DebugUi {
public:
    void init();
    void beginFrame(int width, int height, float delta_time, float dpi_scale);
    void draw(const gameplay::GameState& gameplay, const app::InputState& input, uint64_t frame_count, float delta_time);
    void render();
    bool handleEvent(const sapp_event* event);
    void cleanup();
};

} // namespace debug

#pragma once

namespace debug {
class DebugUi;
}

namespace gameplay {
class GameState;
}

namespace renderer {

struct StyleSettings {
    float light_x = -0.45f;
    float light_y = 0.80f;
    float light_z = 0.35f;
    float toon_bands = 4.0f;
    float outline_thickness = 0.045f;
    float outline_color[3] = {0.02f, 0.025f, 0.035f};
    bool halftone_enabled = true;
    float halftone_intensity = 0.22f;
    float halftone_scale = 9.0f;
};

class Renderer {
public:
    void init();
    void draw(const gameplay::GameState& gameplay, debug::DebugUi& debug_ui);
    StyleSettings& styleSettings();
    void cleanup();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace renderer

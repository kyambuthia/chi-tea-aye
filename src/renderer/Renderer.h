#pragma once

namespace assets {
class CharacterModel;
struct StaticMesh;
}

namespace gameplay {
class GameState;
}

namespace world {
class World;
}

namespace renderer {

struct StyleSettings {
    // Debug view: draws the warrior's joints and bones over the model.
    bool show_skeleton = true;
    float light_x = -0.45f;
    float light_y = 0.80f;
    float light_z = 0.35f;
    float toon_bands = 4.0f;
    bool halftone_enabled = true;
    float halftone_intensity = 0.22f;
    float halftone_scale = 9.0f;
};

class Renderer {
public:
    // Requires a current GL context. The character, world, and props must outlive the renderer.
    void init(const assets::CharacterModel& character, const world::World& world, const assets::StaticMesh& rifle,
        const assets::StaticMesh& pistol);
    void draw(const gameplay::GameState& gameplay, int framebuffer_width, int framebuffer_height);
    StyleSettings& styleSettings();
    void cleanup();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace renderer

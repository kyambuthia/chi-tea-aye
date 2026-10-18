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
    // Debug view: draws the warrior's joints and bones over the model (F1).
    bool show_skeleton = false;
    // Debug overlay: markers at the right hand, rifle grip, fore-end and muzzle (F2).
    bool show_rifle_points = false;
    // Time of day in hours (0..24): sun, sky, fog colour, lit windows and street lamps.
    float time_of_day = 17.0f;
    bool advance_time = false;   // let the clock run (one game hour per real minute)
    float fog_scale = 1.0f;
    // Performance: dynamic resolution, a fixed scale when it is off, and MSAA samples.
    bool dynamic_resolution = true;
    float fixed_scale = 1.0f;
    int msaa_samples = 4;
    // Character selection: 0 = African Warrior, 1 = Y-Bot (F4).
    int character_model = 1;
    bool show_companion = true;
    float toon_bands = 4.0f;
    bool halftone_enabled = true;
    float halftone_intensity = 0.12f;
    float halftone_scale = 9.0f;
};

class Renderer {
public:
    // Requires a current GL context. The characters, world, and props must outlive the renderer.
    void init(const assets::CharacterModel& character, const assets::CharacterModel* ybot, const world::World& world,
        const assets::StaticMesh& rifle, const assets::StaticMesh& pistol);
    // frame_seconds drives dynamic resolution and the clock.
    void draw(const gameplay::GameState& gameplay, int framebuffer_width, int framebuffer_height, float frame_seconds);
    StyleSettings& styleSettings();

    // Numbers for the dev panel.
    struct Stats {
        float render_scale = 1.0f;
        int scene_width = 0;
        int scene_height = 0;
        int samples = 0;
        bool offscreen = false;
        int visible_boxes = 0;
        int total_boxes = 0;
    };
    const Stats& stats() const;
    // The clip the last frame played: its time within the clip (wrapped for loops) and its length.
    float clipTime() const;
    float clipDuration() const;
    // The clip the last frame played, by name (the rifle's fire clip shows here while shooting).
    const char* playedClip() const;
    // Screen position of a world point in normalised device coordinates (-1 to 1, y up).
    // Returns false when the point is behind the camera.
    bool projectPoint(const float p[3], float& ndc_x, float& ndc_y) const;
    // The rifle's muzzle this frame. Returns false when no rifle is held.
    bool muzzlePoint(float out[3]) const;
    // This frame's rifle points, in world space. Returns false when no rifle is held.
    struct RiflePoints {
        float hand[3];
        float grip[3];
        float fore[3];
        float muzzle[3];
    };
    bool riflePoints(RiflePoints& out) const;
    void cleanup();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace renderer

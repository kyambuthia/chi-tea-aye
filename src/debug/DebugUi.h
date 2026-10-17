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

// Where a shot's effects go on the window, in window points.
struct ShotFeedback {
    float shot_age = 1e9f;   // seconds since the shot; large before the first one
    bool hit = false;        // the shot hit a target
    bool muzzle_visible = false;
    float muzzle_x = 0.0f, muzzle_y = 0.0f;
    bool impact_visible = false;
    float impact_x = 0.0f, impact_y = 0.0f;
};

// Window positions of the rifle's hand, grip, fore-end and muzzle (for the F2 overlay).
struct RiflePointsOverlay {
    bool visible = false;
    bool hand_visible = false, grip_visible = false, fore_visible = false, muzzle_visible = false;
    float hand_x = 0.0f, hand_y = 0.0f;
    float grip_x = 0.0f, grip_y = 0.0f;
    float fore_x = 0.0f, fore_y = 0.0f;
    float muzzle_x = 0.0f, muzzle_y = 0.0f;
};

class DebugUi {
public:
    // Requires the GL context to be current; glsl_version is e.g. "#version 330".
    void init(SDL_Window* window, SDL_GLContext context, const char* glsl_version);
    void beginFrame();
    // played_clip, clip_time and clip_duration describe the clip as the renderer last played it.
    void draw(const gameplay::GameState& gameplay, const app::InputState& input, renderer::StyleSettings& style, uint64_t frame_count, float delta_time,
        const char* played_clip, float clip_time, float clip_duration);
    // The muzzle flash, tracer, impact spark and hit marker of the latest shot.
    void drawShotFeedback(const ShotFeedback& shot);
    // Markers for the hand (white), grip (green), fore-end (blue) and muzzle (red), with the rifle's axis.
    void drawRiflePoints(const RiflePointsOverlay& points);
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

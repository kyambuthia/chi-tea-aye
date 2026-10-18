#pragma once

#include "renderer/GlApi.h"

namespace renderer {

// The 3D scene renders into an offscreen, multisampled target at a scaled resolution;
// this pass resolves it and draws it to the window with a colour grade and vignette.
// The UI is drawn afterwards straight to the window, so text stays sharp at any scale.
//
// If the offscreen target cannot be made (old driver, out of memory), the pass turns
// itself off and the scene draws straight to the window instead.
class PostPass {
public:
    struct Grade {
        float saturation = 0.86f;
        float contrast = 0.12f;
        float warmth = 0.05f;    // red up, blue down
        float vignette = 0.28f;
        float exposure = 1.0f;
    };

    bool init();
    // Binds the scene target sized for this frame (scale times the window size) and
    // sets the viewport. Returns false when the pass is off: draw to the window instead.
    bool begin(int window_width, int window_height, float scale, int samples);
    // Resolves the scene and draws it to the window.
    void end(int window_width, int window_height);
    void cleanup();

    Grade& grade() { return grade_; }
    bool active() const { return active_; }
    int sceneWidth() const { return width_; }
    int sceneHeight() const { return height_; }
    int samples() const { return samples_; }
    int maxSamples() const { return max_samples_; }

private:
    bool resize(int width, int height, int samples);
    void release();

    bool active_ = false;
    bool failed_ = false;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 0;
    int max_samples_ = 0;
    GLuint msaa_fbo_ = 0;
    GLuint msaa_color_ = 0;
    GLuint msaa_depth_ = 0;
    GLuint resolve_fbo_ = 0;
    GLuint resolve_texture_ = 0;
    GLuint program_ = 0;
    GLint scene_location_ = -1;
    GLint grade_location_ = -1;
    GLint aspect_location_ = -1;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    Grade grade_;
};

} // namespace renderer

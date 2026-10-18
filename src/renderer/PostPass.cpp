#include "renderer/PostPass.h"

#include "renderer/GlUtil.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace renderer {
namespace {

std::string vertexSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        in vec2 position;
        out vec2 v_uv;
        void main() {
            v_uv = position * 0.5 + 0.5;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";
}

std::string fragmentSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform sampler2D scene;
        uniform vec4 grade;    // saturation, contrast, warmth, vignette
        uniform vec2 aspect;   // x: width / height, y: exposure
        in vec2 v_uv;
        out vec4 frag_color;
        void main() {
            vec3 c = texture(scene, v_uv).rgb * aspect.y;
            float luma = dot(c, vec3(0.299, 0.587, 0.114));
            c = mix(vec3(luma), c, grade.x);
            c *= vec3(1.0 + grade.z, 1.0, 1.0 - grade.z);
            c = clamp(c, 0.0, 1.0);
            c = mix(c, c * c * (3.0 - 2.0 * c), grade.y);  // gentle S-curve
            vec2 p = (v_uv - 0.5) * vec2(aspect.x, 1.0);
            float v = smoothstep(1.05, 0.25, length(p));
            c *= mix(1.0 - grade.w, 1.0, v);
            frag_color = vec4(c, 1.0);
        }
    )";
}

} // namespace

bool PostPass::init() {
    program_ = buildProgram(vertexSource(), fragmentSource(), "post", {{kAttrPosition, "position"}});
    if (program_ == 0) {
        failed_ = true;
        return false;
    }
    scene_location_ = glGetUniformLocation(program_, "scene");
    grade_location_ = glGetUniformLocation(program_, "grade");
    aspect_location_ = glGetUniformLocation(program_, "aspect");
    const float tri[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(kAttrPosition);
    glVertexAttribPointer(kAttrPosition, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    GLint max_samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    max_samples_ = max_samples;
    return true;
}

void PostPass::release() {
    if (msaa_fbo_ != 0) glDeleteFramebuffers(1, &msaa_fbo_);
    if (resolve_fbo_ != 0) glDeleteFramebuffers(1, &resolve_fbo_);
    if (msaa_color_ != 0) glDeleteRenderbuffers(1, &msaa_color_);
    if (msaa_depth_ != 0) glDeleteRenderbuffers(1, &msaa_depth_);
    if (resolve_texture_ != 0) glDeleteTextures(1, &resolve_texture_);
    msaa_fbo_ = resolve_fbo_ = msaa_color_ = msaa_depth_ = resolve_texture_ = 0;
    width_ = height_ = samples_ = 0;
}

bool PostPass::resize(int width, int height, int samples) {
    release();
    // The multisampled target the scene draws into.
    glGenFramebuffers(1, &msaa_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
    glGenRenderbuffers(1, &msaa_color_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_color_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color_);
    glGenRenderbuffers(1, &msaa_depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth_);
    const bool msaa_ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    // The single-sampled texture it resolves into, sampled by the grade shader.
    glGenTextures(1, &resolve_texture_);
    glBindTexture(GL_TEXTURE_2D, resolve_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &resolve_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_texture_, 0);
    const bool resolve_ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (!msaa_ok || !resolve_ok) {
        std::fprintf(stderr, "[renderer] offscreen target %dx%d (%d samples) incomplete; drawing straight to the window\n",
            width, height, samples);
        release();
        return false;
    }
    width_ = width;
    height_ = height;
    samples_ = samples;
    return true;
}

bool PostPass::begin(int window_width, int window_height, float scale, int samples) {
    active_ = false;
    if (failed_ || program_ == 0 || window_width <= 0 || window_height <= 0) {
        return false;
    }
    const int w = std::max(1, static_cast<int>(window_width * scale + 0.5f));
    const int h = std::max(1, static_cast<int>(window_height * scale + 0.5f));
    const int s = std::max(0, std::min(samples, max_samples_));
    if (w != width_ || h != height_ || s != samples_) {
        if (!resize(w, h, s)) {
            failed_ = true;
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_);
    glViewport(0, 0, width_, height_);
    active_ = true;
    return true;
}

void PostPass::end(int window_width, int window_height) {
    if (!active_) {
        return;
    }
    // Resolve the samples, then upscale with the grade.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width, window_height);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolve_texture_);
    glUniform1i(scene_location_, 0);
    glUniform4f(grade_location_, grade_.saturation, grade_.contrast, grade_.warmth, grade_.vignette);
    glUniform2f(aspect_location_, window_height > 0 ? static_cast<float>(window_width) / static_cast<float>(window_height) : 1.0f,
        grade_.exposure);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    active_ = false;
}

void PostPass::cleanup() {
    release();
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (program_ != 0) glDeleteProgram(program_);
    vao_ = vbo_ = program_ = 0;
}

} // namespace renderer

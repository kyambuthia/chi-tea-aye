#pragma once

// Force-included into ImGui's OpenGL backend on the web build only.
//
// The backend saves and restores about 25 pieces of GL state around every UI draw
// with glGetIntegerv and glIsEnabled. In a browser each of those is a synchronous
// round trip to the GPU process, which stalls the main thread until every queued
// command has run: profiled at over 70% of the frame. The game does not need its
// state restored (the renderer sets what it uses at the start of each frame), so
// per-frame state queries get fixed, valid answers here instead. Queries ImGui makes
// once at start-up (versions, limits, extensions) still go to the real GL.

#include <GLES3/gl3.h>

static inline void chi_imgui_shadow_glGetIntegerv(GLenum pname, GLint* data) {
    switch (pname) {
        case GL_ACTIVE_TEXTURE:
            data[0] = GL_TEXTURE0;
            return;
        case GL_BLEND_EQUATION_RGB:
        case GL_BLEND_EQUATION_ALPHA:
            data[0] = GL_FUNC_ADD;
            return;
        case GL_BLEND_SRC_RGB:
        case GL_BLEND_SRC_ALPHA:
            data[0] = GL_ONE;
            return;
        case GL_BLEND_DST_RGB:
        case GL_BLEND_DST_ALPHA:
            data[0] = GL_ZERO;
            return;
        case GL_VIEWPORT:
        case GL_SCISSOR_BOX:
            data[0] = data[1] = data[2] = data[3] = 0;
            return;
        case GL_CURRENT_PROGRAM:
        case GL_TEXTURE_BINDING_2D:
        case GL_SAMPLER_BINDING:
        case GL_ARRAY_BUFFER_BINDING:
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
        case GL_VERTEX_ARRAY_BINDING:
        case GL_PIXEL_UNPACK_BUFFER_BINDING:
            data[0] = 0;
            return;
        default:
            glGetIntegerv(pname, data);  // start-up queries: versions, limits, extensions
            return;
    }
}

// Every capability is reported off, so the restore leaves blending, culling, depth,
// stencil and scissor disabled; the renderer re-enables what it needs each frame.
static inline GLboolean chi_imgui_shadow_glIsEnabled(GLenum) {
    return GL_FALSE;
}

#define glGetIntegerv chi_imgui_shadow_glGetIntegerv
#define glIsEnabled chi_imgui_shadow_glIsEnabled

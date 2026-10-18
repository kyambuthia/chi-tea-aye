#pragma once

// Shared GL helpers for the renderer's passes: program building, mesh and texture
// upload, and the fixed attribute layout every program agrees on.

#include "renderer/GlApi.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

namespace assets {
struct MeshVertex;
}

namespace renderer {

#if defined(CHI_GLES)
#define CHI_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define CHI_GLSL_HEADER "#version 330\n"
#endif

// Vertex layout shared by every program: attribute locations are fixed so a
// single attribute setup works for both static and skinned meshes.
enum AttributeLocation : GLuint {
    kAttrPosition = 0,
    kAttrNormal = 1,
    kAttrColor = 2,
    kAttrUv = 3,
    kAttrJoints = 4,
    kAttrWeights = 5,
};

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei index_count = 0;
};

// Compiles and links; returns 0 (and logs) on failure. bindings name the attribute
// locations, e.g. {{kAttrPosition, "position"}}.
GLuint buildProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label,
    std::initializer_list<std::pair<GLuint, const char*>> bindings);

// The MeshVertex attribute bindings (position, normal0, color0, uv0, joints0, weights0).
GLuint buildMeshProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label);

void setMeshVertexLayout();
GpuMesh uploadMesh(const assets::MeshVertex* vertices, size_t vertex_count, const uint16_t* indices, size_t index_count);
void deleteMesh(GpuMesh& mesh);
GLuint uploadTexture(const uint8_t* rgba, int width, int height);

} // namespace renderer

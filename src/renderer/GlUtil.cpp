#include "renderer/GlUtil.h"

#include "assets/MeshVertex.h"

#include <cstdio>

namespace renderer {

GLuint buildProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label,
    std::initializer_list<std::pair<GLuint, const char*>> bindings) {
    auto compile = [label](GLenum type, const std::string& source) {
        const GLuint shader = glCreateShader(type);
        const char* text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == 0) {
            char log[2048]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[renderer] %s %s shader compile failed: %s\n", label,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
            glDeleteShader(shader);
            return GLuint{0};
        }
        return shader;
    };

    const GLuint vs = compile(GL_VERTEX_SHADER, vertex_source);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    for (const auto& binding : bindings) {
        glBindAttribLocation(program, binding.first, binding.second);
    }
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[renderer] %s program link failed: %s\n", label, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLuint buildMeshProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label) {
    return buildProgram(vertex_source, fragment_source, label,
        {{kAttrPosition, "position"}, {kAttrNormal, "normal0"}, {kAttrColor, "color0"}, {kAttrUv, "uv0"},
            {kAttrJoints, "joints0"}, {kAttrWeights, "weights0"}});
}

void setMeshVertexLayout() {
    using assets::MeshVertex;
    const GLsizei stride = sizeof(MeshVertex);
    glEnableVertexAttribArray(kAttrPosition);
    glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(kAttrNormal);
    glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(kAttrColor);
    glVertexAttribPointer(kAttrColor, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, color)));
    glEnableVertexAttribArray(kAttrUv);
    glVertexAttribPointer(kAttrUv, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, uv)));
    glEnableVertexAttribArray(kAttrJoints);
    glVertexAttribPointer(kAttrJoints, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, joints)));
    glEnableVertexAttribArray(kAttrWeights);
    glVertexAttribPointer(kAttrWeights, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(MeshVertex, weights)));
}

GpuMesh uploadMesh(const assets::MeshVertex* vertices, size_t vertex_count, const uint16_t* indices, size_t index_count) {
    GpuMesh mesh;
    mesh.index_count = static_cast<GLsizei>(index_count);

    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);

    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_count * sizeof(assets::MeshVertex)), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_count * sizeof(uint16_t)), indices, GL_STATIC_DRAW);

    setMeshVertexLayout();
    glBindVertexArray(0);
    return mesh;
}

void deleteMesh(GpuMesh& mesh) {
    if (mesh.vao != 0) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ibo);
    }
    mesh = GpuMesh{};
}

GLuint uploadTexture(const uint8_t* rgba, int width, int height) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace renderer

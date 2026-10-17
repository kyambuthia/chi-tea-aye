#include "renderer/Renderer.h"

#include "gameplay/GameState.h"
#include "renderer/GlApi.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

namespace renderer {
namespace {

struct Vertex {
    float position[3];
    float normal[3];
    float color[4];
};

struct Mat4 {
    float m[16];
};

// Uniform blocks are split per stage: sokol requires each stage to declare
// the blocks it reads (Metal in particular), so fragment inputs get their own
// block at slot 1.
struct ToonVsUniforms {
    Mat4 mvp;
    Mat4 model;
};

struct ToonFsUniforms {
    float light_dir_bands[4];
    float style[4];
    float surface[4];
};

struct OutlineVsUniforms {
    Mat4 mvp;
    float outline_thickness[4];
};

struct OutlineFsUniforms {
    float outline_color[4];
};

constexpr Vertex kVertices[] = {
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.93f, 0.18f, 0.20f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.93f, 0.18f, 0.20f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.93f, 0.18f, 0.20f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.93f, 0.18f, 0.20f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.10f, 0.64f, 0.92f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.10f, 0.64f, 0.92f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.10f, 0.64f, 0.92f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.10f, 0.64f, 0.92f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.98f, 0.80f, 0.18f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.98f, 0.80f, 0.18f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.98f, 0.80f, 0.18f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.98f, 0.80f, 0.18f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.48f, 0.80f, 0.28f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.48f, 0.80f, 0.28f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.48f, 0.80f, 0.28f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.48f, 0.80f, 0.28f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.95f, 0.38f, 0.16f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.95f, 0.38f, 0.16f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.95f, 0.38f, 0.16f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.95f, 0.38f, 0.16f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.64f, 0.82f, 0.38f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.64f, 0.82f, 0.38f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.64f, 0.82f, 0.38f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.64f, 0.82f, 0.38f, 1.0f}},
    {{-6.0f, -0.52f, -6.0f}, {0.0f, 1.0f, 0.0f}, {0.11f, 0.12f, 0.14f, 1.0f}},
    {{6.0f, -0.52f, -6.0f}, {0.0f, 1.0f, 0.0f}, {0.11f, 0.12f, 0.14f, 1.0f}},
    {{6.0f, -0.52f, 6.0f}, {0.0f, 1.0f, 0.0f}, {0.10f, 0.11f, 0.13f, 1.0f}},
    {{-6.0f, -0.52f, 6.0f}, {0.0f, 1.0f, 0.0f}, {0.10f, 0.11f, 0.13f, 1.0f}},
};

constexpr uint16_t kCubeIndices[] = {
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    8, 9, 10, 8, 10, 11,
    12, 15, 14, 12, 14, 13,
    16, 17, 18, 16, 18, 19,
    20, 23, 22, 20, 22, 21,
};

constexpr uint16_t kGroundIndices[] = {24, 25, 26, 24, 26, 27};
constexpr float kPi = 3.14159265358979323846f;

Mat4 identity() {
    return Mat4{{1.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 1.0f}};
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

Mat4 translation(float x, float y, float z) {
    Mat4 out = identity();
    out.m[12] = x;
    out.m[13] = y;
    out.m[14] = z;
    return out;
}

Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
    const float f = 1.0f / std::tan(fovy_radians * 0.5f);
    Mat4 out{};
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (far_z + near_z) / (near_z - far_z);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
    return out;
}

Mat4 lookAt(float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z) {
    float fx = target_x - eye_x;
    float fy = target_y - eye_y;
    float fz = target_z - eye_z;
    const float f_len = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
    fx /= f_len;
    fy /= f_len;
    fz /= f_len;

    float sx = -fz;
    float sy = 0.0f;
    float sz = fx;
    const float s_len = std::sqrt((sx * sx) + (sz * sz));
    sx /= s_len;
    sz /= s_len;

    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    Mat4 out = identity();
    out.m[0] = sx;
    out.m[4] = sy;
    out.m[8] = sz;
    out.m[1] = ux;
    out.m[5] = uy;
    out.m[9] = uz;
    out.m[2] = -fx;
    out.m[6] = -fy;
    out.m[10] = -fz;
    out.m[12] = -((sx * eye_x) + (sy * eye_y) + (sz * eye_z));
    out.m[13] = -((ux * eye_x) + (uy * eye_y) + (uz * eye_z));
    out.m[14] = (fx * eye_x) + (fy * eye_y) + (fz * eye_z);
    return out;
}

#if defined(CHI_GLES)
#define CHI_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define CHI_GLSL_HEADER "#version 330\n"
#endif

const char* toonVertexShaderSource() {
    return CHI_GLSL_HEADER R"(
        uniform mat4 mvp;
        uniform mat4 model;

        in vec3 position;
        in vec3 normal0;
        in vec4 color0;
        out vec3 normal;
        out vec3 local_pos;
        out vec3 world_pos;
        out vec4 color;

        void main() {
            vec4 world_position = model * vec4(position, 1.0);
            gl_Position = mvp * vec4(position, 1.0);
            local_pos = position;
            world_pos = world_position.xyz;
            normal = normalize((model * vec4(normal0, 0.0)).xyz);
            color = color0;
        }
    )";
}

const char* toonFragmentShaderSource() {
    return CHI_GLSL_HEADER R"(
        uniform vec4 light_dir_bands;
        uniform vec4 style;
        uniform vec4 surface;

        in vec3 normal;
        in vec3 local_pos;
        in vec3 world_pos;
        in vec4 color;
        out vec4 frag_color;

        void main() {
            vec3 n = normalize(normal);
            vec3 light_dir = normalize(light_dir_bands.xyz);
            float ndotl = max(dot(n, light_dir), 0.0);
            float bands = max(light_dir_bands.w, 2.0);
            float shade = floor(ndotl * bands) / (bands - 1.0);
            float ink_shadow = mix(0.42, 1.08, shade);
            vec3 final_color = color.rgb * ink_shadow;

            if ((surface.x < 0.5) && (style.x > 0.5)) {
                vec3 object_coord = (local_pos + vec3(0.5)) * max(style.z, 1.0);
                vec2 pattern_uv = abs(n.y) > 0.5 ? object_coord.xz : (abs(n.x) > 0.5 ? object_coord.zy : object_coord.xy);
                vec2 cell = floor(pattern_uv);
                float checker = mod(cell.x + cell.y, 2.0);
                float dots = mix(1.0, 1.0 - style.y, checker);
                final_color *= dots;
            }

            if (surface.x > 0.5) {
                vec2 grid_coord = world_pos.xz;
                vec2 grid_deriv = fwidth(grid_coord);
                vec2 minor_dist = abs(fract(grid_coord - 0.5) - 0.5);
                vec2 minor_line = 1.0 - smoothstep(vec2(0.0), grid_deriv * 1.25, minor_dist);
                float minor = max(minor_line.x, minor_line.y);

                vec2 major_coord = grid_coord / 4.0;
                vec2 major_deriv = fwidth(major_coord);
                vec2 major_dist = abs(fract(major_coord - 0.5) - 0.5);
                vec2 major_line = 1.0 - smoothstep(vec2(0.0), major_deriv * 1.6, major_dist);
                float major = max(major_line.x, major_line.y);

                vec3 base = vec3(0.08, 0.09, 0.11);
                vec3 minor_color = vec3(0.22, 0.24, 0.28);
                vec3 major_color = vec3(0.42, 0.44, 0.50);
                final_color = mix(base, minor_color, minor * 0.75);
                final_color = mix(final_color, major_color, major * 0.95);
            }

            frag_color = vec4(final_color, 1.0);
        }
    )";
}

const char* outlineVertexShaderSource() {
    return CHI_GLSL_HEADER R"(
        uniform mat4 mvp;
        uniform vec4 outline_thickness;

        in vec3 position;
        in vec3 normal0;

        void main() {
            vec3 expanded = position + normal0 * outline_thickness.w;
            gl_Position = mvp * vec4(expanded, 1.0);
        }
    )";
}

const char* outlineFragmentShaderSource() {
    return CHI_GLSL_HEADER R"(
        uniform vec4 outline_color;
        out vec4 frag_color;

        void main() {
            frag_color = vec4(outline_color.rgb, 1.0);
        }
    )";
}

ToonVsUniforms makeToonVsUniforms(const Mat4& view_projection, const Mat4& model) {
    ToonVsUniforms uniforms{};
    uniforms.mvp = multiply(view_projection, model);
    uniforms.model = model;
    return uniforms;
}

ToonFsUniforms makeToonFsUniforms(const StyleSettings& style, bool ground) {
    ToonFsUniforms uniforms{};
    uniforms.light_dir_bands[0] = style.light_x;
    uniforms.light_dir_bands[1] = style.light_y;
    uniforms.light_dir_bands[2] = style.light_z;
    uniforms.light_dir_bands[3] = std::floor(style.toon_bands + 0.5f);
    uniforms.style[0] = style.halftone_enabled ? 1.0f : 0.0f;
    uniforms.style[1] = style.halftone_intensity;
    uniforms.style[2] = style.halftone_scale;
    uniforms.style[3] = 0.0f;
    uniforms.surface[0] = ground ? 1.0f : 0.0f;
    uniforms.surface[1] = 0.0f;
    uniforms.surface[2] = 0.0f;
    uniforms.surface[3] = 0.0f;
    return uniforms;
}

OutlineVsUniforms makeOutlineVsUniforms(const Mat4& view_projection, const Mat4& model, const StyleSettings& style) {
    OutlineVsUniforms uniforms{};
    uniforms.mvp = multiply(view_projection, model);
    uniforms.outline_thickness[3] = style.outline_thickness;
    return uniforms;
}

OutlineFsUniforms makeOutlineFsUniforms(const StyleSettings& style) {
    OutlineFsUniforms uniforms{};
    uniforms.outline_color[0] = style.outline_color[0];
    uniforms.outline_color[1] = style.outline_color[1];
    uniforms.outline_color[2] = style.outline_color[2];
    uniforms.outline_color[3] = 1.0f;
    return uniforms;
}

// Compiles and links a program. Attribute locations are fixed so one VAO
// layout serves both the toon and outline programs.
GLuint buildProgram(const char* vertex_source, const char* fragment_source, const char* label) {
    auto compile = [label](GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == 0) {
            char log[1024]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::fprintf(stderr, "[renderer] %s shader compile failed: %s\n", label, log);
            glDeleteShader(shader);
            return GLuint{0};
        }
        return shader;
    };

    const GLuint vs = compile(GL_VERTEX_SHADER, vertex_source);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (vs == 0 || fs == 0) {
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal0");
    glBindAttribLocation(program, 2, "color0");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[1024]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[renderer] %s program link failed: %s\n", label, log);
        return 0;
    }
    return program;
}

} // namespace

struct Renderer::Impl {
    GLuint vao = 0;
    GLuint vertex_buffer = 0;
    GLuint index_buffer = 0;

    GLuint toon_program = 0;
    GLint toon_mvp = -1;
    GLint toon_model = -1;
    GLint toon_light_dir_bands = -1;
    GLint toon_style = -1;
    GLint toon_surface = -1;

    GLuint outline_program = 0;
    GLint outline_mvp = -1;
    GLint outline_thickness = -1;
    GLint outline_color = -1;

    StyleSettings style{};
};

void Renderer::init() {
    impl_ = new Impl{};

    glGenVertexArrays(1, &impl_->vao);
    glBindVertexArray(impl_->vao);

    glGenBuffers(1, &impl_->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

    // Cube indices first (36), then the ground quad (6).
    uint16_t indices[42]{};
    for (size_t i = 0; i < 36; ++i) {
        indices[i] = kCubeIndices[i];
    }
    for (size_t i = 0; i < 6; ++i) {
        indices[36 + i] = kGroundIndices[i];
    }
    glGenBuffers(1, &impl_->index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, color)));

    impl_->toon_program = buildProgram(toonVertexShaderSource(), toonFragmentShaderSource(), "toon");
    if (impl_->toon_program != 0) {
        impl_->toon_mvp = glGetUniformLocation(impl_->toon_program, "mvp");
        impl_->toon_model = glGetUniformLocation(impl_->toon_program, "model");
        impl_->toon_light_dir_bands = glGetUniformLocation(impl_->toon_program, "light_dir_bands");
        impl_->toon_style = glGetUniformLocation(impl_->toon_program, "style");
        impl_->toon_surface = glGetUniformLocation(impl_->toon_program, "surface");
    }

    impl_->outline_program = buildProgram(outlineVertexShaderSource(), outlineFragmentShaderSource(), "outline");
    if (impl_->outline_program != 0) {
        impl_->outline_mvp = glGetUniformLocation(impl_->outline_program, "mvp");
        impl_->outline_thickness = glGetUniformLocation(impl_->outline_program, "outline_thickness");
        impl_->outline_color = glGetUniformLocation(impl_->outline_program, "outline_color");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    // Vertex winding is clockwise, matching the sokol default this scene was authored with.
    glFrontFace(GL_CW);
}

void Renderer::draw(const gameplay::GameState& gameplay, int framebuffer_width, int framebuffer_height) {
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
#if defined(CHI_GLES)
    glClearDepthf(1.0f);
#else
    glClearDepth(1.0);
#endif
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexArray(impl_->vao);

    const float aspect = framebuffer_width > 0 && framebuffer_height > 0
        ? static_cast<float>(framebuffer_width) / static_cast<float>(framebuffer_height)
        : 16.0f / 9.0f;
    const Mat4 projection = perspective(60.0f * kPi / 180.0f, aspect, 0.01f, 100.0f);
    const Mat4 view = lookAt(4.5f, 3.5f, 6.0f, 0.0f, 0.0f, 0.0f);
    const Mat4 view_projection = multiply(projection, view);

    glUseProgram(impl_->toon_program);
    const auto drawToon = [this](const ToonVsUniforms& vs, const ToonFsUniforms& fs, GLsizei count, const void* offset) {
        glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, vs.mvp.m);
        glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, vs.model.m);
        glUniform4fv(impl_->toon_light_dir_bands, 1, fs.light_dir_bands);
        glUniform4fv(impl_->toon_style, 1, fs.style);
        glUniform4fv(impl_->toon_surface, 1, fs.surface);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, offset);
    };

    const ToonVsUniforms ground_vs = makeToonVsUniforms(view_projection, identity());
    const ToonFsUniforms ground_fs = makeToonFsUniforms(impl_->style, true);
    drawToon(ground_vs, ground_fs, 6, reinterpret_cast<const void*>(36 * sizeof(uint16_t)));

    const Mat4 model = translation(gameplay.playerX(), 0.0f, gameplay.playerZ());
    const ToonVsUniforms cube_vs = makeToonVsUniforms(view_projection, model);
    const ToonFsUniforms cube_fs = makeToonFsUniforms(impl_->style, false);
    drawToon(cube_vs, cube_fs, 36, nullptr);

    if (impl_->style.outline_thickness > 0.0f) {
        const OutlineVsUniforms outline_vs = makeOutlineVsUniforms(view_projection, model, impl_->style);
        const OutlineFsUniforms outline_fs = makeOutlineFsUniforms(impl_->style);
        glUseProgram(impl_->outline_program);
        glUniformMatrix4fv(impl_->outline_mvp, 1, GL_FALSE, outline_vs.mvp.m);
        glUniform4fv(impl_->outline_thickness, 1, outline_vs.outline_thickness);
        glUniform4fv(impl_->outline_color, 1, outline_fs.outline_color);

        // Inflated back faces act as the ink outline.
        glCullFace(GL_FRONT);
        glDepthMask(GL_FALSE);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr);
        glDepthMask(GL_TRUE);
        glCullFace(GL_BACK);
    }

    glBindVertexArray(0);
}

StyleSettings& Renderer::styleSettings() {
    return impl_->style;
}

void Renderer::cleanup() {
    if (impl_ == nullptr) {
        return;
    }

    glDeleteProgram(impl_->toon_program);
    glDeleteProgram(impl_->outline_program);
    delete impl_;
    impl_ = nullptr;
}

} // namespace renderer

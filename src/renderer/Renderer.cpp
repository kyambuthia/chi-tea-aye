#include "renderer/Renderer.h"

#include "assets/CharacterModel.h"
#include "assets/MeshVertex.h"
#include "gameplay/GameState.h"
#include "math/Mat4.h"
#include "renderer/GlApi.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace renderer {
namespace {

using assets::MeshVertex;
using math::Mat4;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGroundY = -0.52f;
constexpr int kJoints = assets::kMaxJoints;

static_assert(kJoints == 32, "the skinning shader declares joints[32]");

#if defined(CHI_GLES)
#define CHI_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define CHI_GLSL_HEADER "#version 330\n"
#endif

// Skinning for the toon shader. Vertices with zero total weight (static
// scenery) keep their model-space position.
const char* kSkinningGlsl = R"(
uniform mat4 joints[32];

mat4 skinMatrix(vec4 j, vec4 w) {
    return w.x * joints[int(j.x)] + w.y * joints[int(j.y)] + w.z * joints[int(j.z)] + w.w * joints[int(j.w)];
}
)";

std::string toonVertexShaderSource() {
    return std::string(CHI_GLSL_HEADER) + kSkinningGlsl + R"(
        uniform mat4 mvp;
        uniform mat4 model;

        in vec3 position;
        in vec3 normal0;
        in vec4 color0;
        in vec2 uv0;
        in vec4 joints0;
        in vec4 weights0;
        out vec3 normal;
        out vec3 local_pos;
        out vec3 world_pos;
        out vec4 color;
        out vec2 v_uv;

        void main() {
            vec4 p = vec4(position, 1.0);
            vec3 n = normal0;
            if (weights0.x + weights0.y + weights0.z + weights0.w > 0.0) {
                mat4 skin = skinMatrix(joints0, weights0);
                p = skin * p;
                n = (skin * vec4(normal0, 0.0)).xyz;
            }
            vec4 world_position = model * p;
            gl_Position = mvp * p;
            local_pos = p.xyz;
            world_pos = world_position.xyz;
            normal = normalize((model * vec4(n, 0.0)).xyz);
            color = color0;
            v_uv = uv0;
        }
    )";
}

std::string toonFragmentShaderSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform vec4 light_dir_bands;
        uniform vec4 style;
        uniform vec4 surface;
        uniform sampler2D base_color;

        in vec3 normal;
        in vec3 local_pos;
        in vec3 world_pos;
        in vec4 color;
        in vec2 v_uv;
        out vec4 frag_color;

        void main() {
            vec3 n = normalize(normal);
            vec3 light_dir = normalize(light_dir_bands.xyz);
            float ndotl = max(dot(n, light_dir), 0.0);
            float bands = max(light_dir_bands.w, 2.0);
            float shade = floor(ndotl * bands) / (bands - 1.0);
            float ink_shadow = mix(0.42, 1.08, shade);
            vec3 albedo = color.rgb * texture(base_color, v_uv).rgb;
            vec3 final_color = albedo * ink_shadow;

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

GLuint buildProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label) {
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
    glBindAttribLocation(program, kAttrPosition, "position");
    glBindAttribLocation(program, kAttrNormal, "normal0");
    glBindAttribLocation(program, kAttrColor, "color0");
    glBindAttribLocation(program, kAttrUv, "uv0");
    glBindAttribLocation(program, kAttrJoints, "joints0");
    glBindAttribLocation(program, kAttrWeights, "weights0");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[renderer] %s program link failed: %s\n", label, log);
        return 0;
    }
    return program;
}

void setVertexLayout() {
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

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei index_count = 0;
};

GpuMesh uploadMesh(const MeshVertex* vertices, size_t vertex_count, const uint16_t* indices, size_t index_count) {
    GpuMesh mesh;
    mesh.index_count = static_cast<GLsizei>(index_count);

    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);

    glGenBuffers(1, &mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_count * sizeof(MeshVertex)), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_count * sizeof(uint16_t)), indices, GL_STATIC_DRAW);

    setVertexLayout();
    glBindVertexArray(0);
    return mesh;
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

MeshVertex groundVertex(float x, float z, float r, float g, float b) {
    MeshVertex v{};
    v.position[0] = x;
    v.position[1] = kGroundY;
    v.position[2] = z;
    v.normal[1] = 1.0f;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = 1.0f;
    return v;
}

Mat4 perspectiveFor(int width, int height) {
    const float aspect = width > 0 && height > 0
        ? static_cast<float>(width) / static_cast<float>(height)
        : 16.0f / 9.0f;
    return math::perspective(60.0f * kPi / 180.0f, aspect, 0.01f, 100.0f);
}

} // namespace

struct Renderer::Impl {
    GpuMesh ground;
    GpuMesh warrior;
    GLuint ground_texture = 0;
    GLuint warrior_texture = 0;
    GLuint white_texture = 0;

    GLuint toon_program = 0;
    GLint toon_mvp = -1;
    GLint toon_model = -1;
    GLint toon_joints = -1;
    GLint toon_light_dir_bands = -1;
    GLint toon_style = -1;
    GLint toon_surface = -1;

    const assets::CharacterModel* character = nullptr;
    Mat4 joint_matrices[kJoints]{};
    StyleSettings style{};
};

void Renderer::init(const assets::CharacterModel& character) {
    impl_ = new Impl{};
    impl_->character = &character;

    const MeshVertex ground_vertices[] = {
        groundVertex(-6.0f, -6.0f, 0.11f, 0.12f, 0.14f),
        groundVertex(6.0f, -6.0f, 0.11f, 0.12f, 0.14f),
        groundVertex(6.0f, 6.0f, 0.10f, 0.11f, 0.13f),
        groundVertex(-6.0f, 6.0f, 0.10f, 0.11f, 0.13f),
    };
    // Counter-clockwise when seen from above, matching the glTF meshes.
    const uint16_t ground_indices[] = {0, 2, 1, 0, 3, 2};
    impl_->ground = uploadMesh(ground_vertices, 4, ground_indices, 6);

    const assets::CharacterMesh& mesh = character.mesh();
    impl_->warrior = uploadMesh(mesh.vertices.data(), mesh.vertices.size(), mesh.indices.data(), mesh.indices.size());

    const uint8_t white[4] = {255, 255, 255, 255};
    impl_->white_texture = uploadTexture(white, 1, 1);
    impl_->ground_texture = impl_->white_texture;
    impl_->warrior_texture = mesh.base_color_rgba.empty()
        ? impl_->white_texture
        : uploadTexture(mesh.base_color_rgba.data(), mesh.texture_width, mesh.texture_height);

    impl_->toon_program = buildProgram(toonVertexShaderSource(), toonFragmentShaderSource(), "toon");
    if (impl_->toon_program != 0) {
        impl_->toon_mvp = glGetUniformLocation(impl_->toon_program, "mvp");
        impl_->toon_model = glGetUniformLocation(impl_->toon_program, "model");
        impl_->toon_joints = glGetUniformLocation(impl_->toon_program, "joints");
        impl_->toon_light_dir_bands = glGetUniformLocation(impl_->toon_program, "light_dir_bands");
        impl_->toon_style = glGetUniformLocation(impl_->toon_program, "style");
        impl_->toon_surface = glGetUniformLocation(impl_->toon_program, "surface");
        glUseProgram(impl_->toon_program);
        glUniform1i(glGetUniformLocation(impl_->toon_program, "base_color"), 0);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    // glTF meshes wind counter-clockwise; front faces are the outward ones.
    glFrontFace(GL_CCW);
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

    // Third-person camera: looks at the upper body from behind and above.
    const float px = gameplay.playerX();
    const float pz = gameplay.playerZ();
    const float target_y = 1.1f;
    const float azimuth = gameplay.cameraAzimuth();
    const float pitch = gameplay.cameraPitch();
    const float distance = gameplay.cameraDistance();
    const float horizontal = distance * std::cos(pitch);
    const Mat4 view = math::lookAt(
        px + horizontal * std::sin(azimuth), target_y + distance * std::sin(pitch), pz + horizontal * std::cos(azimuth),
        px, target_y, pz);
    const Mat4 view_projection = math::multiply(perspectiveFor(framebuffer_width, framebuffer_height), view);

    auto setToonSurface = [this](bool ground) {
        const float light[4] = {
            impl_->style.light_x,
            impl_->style.light_y,
            impl_->style.light_z,
            std::floor(impl_->style.toon_bands + 0.5f),
        };
        const float style[4] = {
            impl_->style.halftone_enabled ? 1.0f : 0.0f,
            impl_->style.halftone_intensity,
            impl_->style.halftone_scale,
            0.0f,
        };
        const float surface[4] = {ground ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        glUniform4fv(impl_->toon_light_dir_bands, 1, light);
        glUniform4fv(impl_->toon_style, 1, style);
        glUniform4fv(impl_->toon_surface, 1, surface);
    };

    glUseProgram(impl_->toon_program);
    glActiveTexture(GL_TEXTURE0);

    // Ground: static geometry, so the skinning path is skipped.
    const Mat4 ground_model = math::identity();
    glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, ground_model).m);
    glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, ground_model.m);
    setToonSurface(true);
    glBindTexture(GL_TEXTURE_2D, impl_->ground_texture);
    glBindVertexArray(impl_->ground.vao);
    glDrawElements(GL_TRIANGLES, impl_->ground.index_count, GL_UNSIGNED_SHORT, nullptr);

    // Warrior: pose the skeleton for the current clip, then draw skinned.
    // Turn the rig so its measured front faces the heading.
    const Mat4 model = math::multiply(math::translation(px, kGroundY, pz),
        math::rotationY(gameplay.playerYaw() - impl_->character->frontYaw()));
    impl_->character->evaluatePose(gameplay.clipName(), gameplay.clipTime(), impl_->joint_matrices);
    glUniformMatrix4fv(impl_->toon_joints, kJoints, GL_FALSE, impl_->joint_matrices[0].m);
    glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, model).m);
    glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, model.m);
    setToonSurface(false);
    glBindTexture(GL_TEXTURE_2D, impl_->warrior_texture);
    glBindVertexArray(impl_->warrior.vao);
    glDrawElements(GL_TRIANGLES, impl_->warrior.index_count, GL_UNSIGNED_SHORT, nullptr);

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
    delete impl_;
    impl_ = nullptr;
}

} // namespace renderer

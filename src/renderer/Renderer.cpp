#include "renderer/Renderer.h"

#include "assets/CharacterModel.h"
#include "assets/MeshVertex.h"
#include "gameplay/GameState.h"
#include "gameplay/WeaponAttachment.h"
#include "gameplay/WeaponState.h"
#include "assets/StaticModel.h"
#include "math/Mat4.h"
#include "renderer/GlApi.h"
#include "world/World.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace renderer {
namespace {

using assets::MeshVertex;
using math::Mat4;

constexpr float kPi = 3.14159265358979323846f;
constexpr int kGroundCells = 80;
constexpr float kGroundHalfSize = world::World::kHalfSize;
constexpr int kJoints = assets::kMaxJoints;

// Grip offsets from the posed right hand to the prop's origin, tuned by eye.
const math::Mat4 kRifleGrip = math::identity();
// The rifle's grip and fore-end points in prop space, in metres from the prop's centre.
// The grip is under the receiver; the fore-end is the front of the handguard. Tuned by eye.
// The rifle mesh points its muzzle along -Z; the held rifle turns it half a turn
// about the grip so the muzzle faces forward. The grip sits just behind the
// receiver (toward the stock, +Z) and the fore-end just ahead of it (toward the
// muzzle, -Z). Tuned by eye from a side view.
const float kRifleGripLocal[3] = {0.0f, -0.05f, 0.12f};
const float kRifleForegripLocal[3] = {0.0f, -0.03f, -0.2f};
// The spine leans toward the aim: this many radians per radian of camera pitch, up to a limit.
constexpr float kSpineBendPerPitch = 0.8f;
constexpr float kSpineBendLimit = 0.5f;
const math::Mat4 kPistolGrip = math::identity();

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
        uniform vec3 eye_pos;
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

            // Fill light from above and from the camera, so the back of the
            // character is not in shadow, plus a rim light on the silhouette.
            vec3 view_dir = normalize(eye_pos - world_pos);
            float facing = max(dot(n, view_dir), 0.0);
            float rim = pow(1.0 - facing, 3.0);
            float fill = 0.10 + 0.08 * max(n.y, 0.0) + 0.18 * facing;
            vec3 final_color = albedo * (ink_shadow + fill) + albedo * 0.25 * rim;

            if ((surface.x < 0.5) && (style.x > 0.5)) {
                vec3 object_coord = (local_pos + vec3(0.5)) * max(style.z, 1.0);
                vec2 pattern_uv = abs(n.y) > 0.5 ? object_coord.xz : (abs(n.x) > 0.5 ? object_coord.zy : object_coord.xy);
                // One round dot per cell. Shadowed areas get bigger dots.
                float dist = length(fract(pattern_uv) - 0.5);
                float radius = 0.45 * sqrt(1.0 - clamp(ndotl, 0.0, 1.0));
                float aa = max(fwidth(dist), 0.001);
                float dot_mask = 1.0 - smoothstep(radius - aa, radius + aa, dist);
                final_color *= mix(1.0, 1.0 - style.y, dot_mask);
            }

            // Props are brightened by surface.y so dark metal and wood read against the body.
            if (surface.x < 0.5) {
                final_color *= 1.0 + surface.y;
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

// Debug skeleton overlay: flat colour lines and points, no lighting or skinning.
std::string skeletonVertexShaderSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform mat4 mvp;
        in vec3 position;

        void main() {
            gl_Position = mvp * vec4(position, 1.0);
            gl_PointSize = 7.0;
        }
    )";
}

std::string skeletonFragmentShaderSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform vec4 color;
        out vec4 frag_color;

        void main() {
            frag_color = color;
        }
    )";
}

// Scope overlay: a full-screen pass, dark outside a round lens, with a centred reticle.
std::string scopeVertexShaderSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        in vec2 position;
        out vec2 v_ndc;

        void main() {
            v_ndc = position;
            gl_Position = vec4(position, 0.0, 1.0);
        }
    )";
}

std::string scopeFragmentShaderSource() {
    return std::string(CHI_GLSL_HEADER) + R"(
        uniform float aspect;
        uniform float alpha;
        in vec2 v_ndc;
        out vec4 frag_color;

        void main() {
            // Units of half the framebuffer height, so the lens is round on any aspect.
            vec2 p = vec2(v_ndc.x * aspect, v_ndc.y);
            float r = length(p);
            float outside = smoothstep(0.40, 0.42, r);
            float hair_x = step(abs(p.x), 0.0025) * step(0.02, abs(p.y)) * step(abs(p.y), 0.30);
            float hair_y = step(abs(p.y), 0.0025) * step(0.02, abs(p.x)) * step(abs(p.x), 0.30);
            float reticle = max(hair_x, hair_y) * (1.0 - outside);
            float shade = max(outside * 0.92, reticle);
            frag_color = vec4(0.0, 0.0, 0.0, shade * alpha);
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

// A unit cube centred on the origin, in one colour. Faces are counter-clockwise seen from outside.
void buildCube(std::vector<MeshVertex>& vertices, std::vector<uint16_t>& indices, const float color[3]) {
    vertices.clear();
    indices.clear();
    for (int axis = 0; axis < 3; ++axis) {
        for (const float sign : {-1.0f, 1.0f}) {
            // Axes (u, v) with cross(u, v) = the face normal; swapped on the negative faces.
            const int next = (axis + 1) % 3;
            const int after = (axis + 2) % 3;
            const int u_axis = sign > 0.0f ? next : after;
            const int v_axis = sign > 0.0f ? after : next;
            const uint16_t base = static_cast<uint16_t>(vertices.size());
            for (int k = 0; k < 4; ++k) {
                const float su = (k == 1 || k == 2) ? 0.5f : -0.5f;
                const float sv = k >= 2 ? 0.5f : -0.5f;
                MeshVertex v{};
                v.position[axis] = 0.5f * sign;
                v.position[u_axis] = su;
                v.position[v_axis] = sv;
                v.normal[axis] = sign;
                v.color[0] = color[0];
                v.color[1] = color[1];
                v.color[2] = color[2];
                v.color[3] = 1.0f;
                vertices.push_back(v);
            }
            indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2),
                base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)});
        }
    }
}

// Terrain grid: heights come from the world, normals from its slopes, and
// triangles wind counter-clockwise seen from above.
void buildGround(const world::World& world, std::vector<MeshVertex>& vertices, std::vector<uint16_t>& indices) {
    const int n = kGroundCells + 1;
    const float step = 2.0f * kGroundHalfSize / kGroundCells;
    const float eps = 0.01f;
    vertices.clear();
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = -kGroundHalfSize + i * step;
            const float z = -kGroundHalfSize + j * step;
            const float slope_x = (world.groundHeight(x + eps, z) - world.groundHeight(x - eps, z)) / (2.0f * eps);
            const float slope_z = (world.groundHeight(x, z + eps) - world.groundHeight(x, z - eps)) / (2.0f * eps);
            const float len = std::sqrt(slope_x * slope_x + 1.0f + slope_z * slope_z);
            MeshVertex v{};
            v.position[0] = x;
            v.position[1] = world.groundHeight(x, z);
            v.position[2] = z;
            v.normal[0] = -slope_x / len;
            v.normal[1] = 1.0f / len;
            v.normal[2] = -slope_z / len;
            const bool light = ((i + j) & 1) == 0;
            const float shade = light ? 0.11f : 0.10f;
            v.color[0] = shade;
            v.color[1] = shade + 0.01f;
            v.color[2] = shade + 0.03f;
            v.color[3] = 1.0f;
            vertices.push_back(v);
        }
    }

    indices.clear();
    for (int j = 0; j < kGroundCells; ++j) {
        for (int i = 0; i < kGroundCells; ++i) {
            const uint16_t a = static_cast<uint16_t>(j * n + i);
            const uint16_t b = static_cast<uint16_t>(a + 1);
            const uint16_t c = static_cast<uint16_t>(a + n + 1);
            const uint16_t d = static_cast<uint16_t>(a + n);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(c);
        }
    }
}

Mat4 perspectiveFor(int width, int height, float fov_radians) {
    const float aspect = width > 0 && height > 0
        ? static_cast<float>(width) / static_cast<float>(height)
        : 16.0f / 9.0f;
    return math::perspective(fov_radians, aspect, 0.01f, 100.0f);
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
    GLint toon_eye = -1;
    float clip_time = 0.0f;      // last frame: time within the clip, wrapped
    const char* played_clip = "Idle";
    Mat4 last_view_projection = math::identity();
    bool muzzle_valid = false;
    float muzzle[3] = {0.0f, 0.0f, 0.0f};
    float clip_duration = 0.0f;  // last frame: the clip length
    GLint toon_surface = -1;

    GLuint skeleton_program = 0;
    GLuint skeleton_vao = 0;
    GLuint skeleton_vbo = 0;
    GLuint scope_program = 0;
    GLint scope_aspect = -1;
    GLint scope_alpha = -1;
    GLuint scope_vao = 0;
    GLuint scope_vbo = 0;
    GLint skeleton_mvp = -1;
    GLint skeleton_color = -1;

    GpuMesh rifle_mesh;
    GpuMesh pistol_mesh;
    GpuMesh cube_mesh;  // shooting targets
    GpuMesh wall_mesh;  // walls
    GLuint rifle_texture = 0;
    GLuint pistol_texture = 0;
    int hand_joint = -1;
    int left_hand_joint = -1;

    const assets::CharacterModel* character = nullptr;
    const world::World* world = nullptr;
    assets::GroundHeightFn ground_height;
    Mat4 joint_matrices[kJoints]{};
    StyleSettings style{};
};

// Builds a program from sources with position-only input at attribute 0.
GLuint buildFlatProgram(const std::string& vertex_source, const std::string& fragment_source, const char* label) {
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

void Renderer::init(const assets::CharacterModel& character, const world::World& world, const assets::StaticMesh& rifle,
    const assets::StaticMesh& pistol) {
    impl_ = new Impl{};
    impl_->character = &character;
    impl_->hand_joint = character.jointIndex("RightHand");
    impl_->left_hand_joint = character.jointIndex("LeftHand");

    const uint8_t white_px[4] = {255, 255, 255, 255};
    if (!rifle.vertices.empty()) {
        impl_->rifle_mesh = uploadMesh(rifle.vertices.data(), rifle.vertices.size(), rifle.indices.data(), rifle.indices.size());
        impl_->rifle_texture = rifle.base_color_rgba.empty()
            ? uploadTexture(white_px, 1, 1)
            : uploadTexture(rifle.base_color_rgba.data(), rifle.texture_width, rifle.texture_height);
    }
    if (!pistol.vertices.empty()) {
        impl_->pistol_mesh = uploadMesh(pistol.vertices.data(), pistol.vertices.size(), pistol.indices.data(), pistol.indices.size());
        impl_->pistol_texture = pistol.base_color_rgba.empty()
            ? uploadTexture(white_px, 1, 1)
            : uploadTexture(pistol.base_color_rgba.data(), pistol.texture_width, pistol.texture_height);
    }
    impl_->world = &world;
    impl_->ground_height = [&world](float x, float z) { return world.groundHeight(x, z); };

    std::vector<MeshVertex> ground_vertices;
    std::vector<uint16_t> ground_indices;
    buildGround(world, ground_vertices, ground_indices);
    impl_->ground = uploadMesh(ground_vertices.data(), ground_vertices.size(), ground_indices.data(), ground_indices.size());

    const assets::CharacterMesh& mesh = character.mesh();
    impl_->warrior = uploadMesh(mesh.vertices.data(), mesh.vertices.size(), mesh.indices.data(), mesh.indices.size());

    std::vector<MeshVertex> cube_vertices;
    std::vector<uint16_t> cube_indices;
    const float target_color[3] = {0.95f, 0.50f, 0.15f};
    buildCube(cube_vertices, cube_indices, target_color);
    impl_->cube_mesh = uploadMesh(cube_vertices.data(), cube_vertices.size(), cube_indices.data(), cube_indices.size());

    const float wall_color[3] = {0.46f, 0.43f, 0.39f};
    buildCube(cube_vertices, cube_indices, wall_color);
    impl_->wall_mesh = uploadMesh(cube_vertices.data(), cube_vertices.size(), cube_indices.data(), cube_indices.size());

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
        impl_->toon_eye = glGetUniformLocation(impl_->toon_program, "eye_pos");
        impl_->toon_surface = glGetUniformLocation(impl_->toon_program, "surface");
        glUseProgram(impl_->toon_program);
        glUniform1i(glGetUniformLocation(impl_->toon_program, "base_color"), 0);
    }

    impl_->skeleton_program = buildFlatProgram(skeletonVertexShaderSource(), skeletonFragmentShaderSource(), "skeleton");
    if (impl_->skeleton_program != 0) {
        impl_->skeleton_mvp = glGetUniformLocation(impl_->skeleton_program, "mvp");
        impl_->skeleton_color = glGetUniformLocation(impl_->skeleton_program, "color");
    }
    glGenVertexArrays(1, &impl_->skeleton_vao);
    glGenBuffers(1, &impl_->skeleton_vbo);

    impl_->scope_program = buildFlatProgram(scopeVertexShaderSource(), scopeFragmentShaderSource(), "scope");
    if (impl_->scope_program != 0) {
        impl_->scope_aspect = glGetUniformLocation(impl_->scope_program, "aspect");
        impl_->scope_alpha = glGetUniformLocation(impl_->scope_program, "alpha");
    }
    const float quad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &impl_->scope_vao);
    glGenBuffers(1, &impl_->scope_vbo);
    glBindVertexArray(impl_->scope_vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->scope_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(kAttrPosition);
    glVertexAttribPointer(kAttrPosition, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

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

    // The player model stands at the player position.
    const float px = gameplay.playerX();
    const float pz = gameplay.playerZ();

    // Third-person camera: looks at the upper body from behind and above.
    // Orbit view, or the sight view from the right shoulder while aimed (see cameraView).
    float eye[3];
    float look[3];
    gameplay.cameraView(eye, look);
    const float eye_x = eye[0];
    const float eye_y = eye[1];
    const float eye_z = eye[2];
    const Mat4 view = math::lookAt(eye_x, eye_y, eye_z, look[0], look[1], look[2]);
    // Aiming narrows the view from 60 to 30 degrees.
    const float aim = gameplay.weapon().aim();
    const float fov = (60.0f - 30.0f * aim) * kPi / 180.0f;
    const Mat4 view_projection = math::multiply(perspectiveFor(framebuffer_width, framebuffer_height, fov), view);
    impl_->last_view_projection = view_projection;

    // brighten lifts a prop's dark metal and wood out of the body's shadows.
    auto setToonSurface = [this](bool ground, float brighten = 0.0f) {
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
        const float surface[4] = {ground ? 1.0f : 0.0f, brighten, 0.0f, 0.0f};
        glUniform4fv(impl_->toon_light_dir_bands, 1, light);
        glUniform4fv(impl_->toon_style, 1, style);
        glUniform4fv(impl_->toon_surface, 1, surface);
    };

    glUseProgram(impl_->toon_program);
    glUniform3f(impl_->toon_eye, eye_x, eye_y, eye_z);
    glActiveTexture(GL_TEXTURE0);

    // Ground: static geometry, so the skinning path is skipped.
    const Mat4 ground_model = math::identity();
    glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, ground_model).m);
    glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, ground_model.m);
    setToonSurface(true);
    glBindTexture(GL_TEXTURE_2D, impl_->ground_texture);
    glBindVertexArray(impl_->ground.vao);
    glDrawElements(GL_TRIANGLES, impl_->ground.index_count, GL_UNSIGNED_SHORT, nullptr);

    // Warrior: place the root on the terrain and turn it so its measured front
    // faces the heading. The pose is solved in world space (with foot IK), so
    // the model matrix is the identity.
    const Mat4 root = math::multiply(impl_->world->groundFrame(px, pz),
        math::rotationY(gameplay.playerYaw() - impl_->character->frontYaw()));
    // A fist attack plays its punch over the locomotion, if the asset has the clip.
    const char* clip_name = gameplay.clipName();
    float clip_time = gameplay.clipTime();
    // Crouching: the rifle has its own crouch clips; other weapons use the standing ones.
    if (std::strcmp(clip_name, "CrouchIdle") == 0 || std::strcmp(clip_name, "CrouchWalk") == 0) {
        const bool idle = std::strcmp(clip_name, "CrouchIdle") == 0;
        const bool rifle_ready = gameplay.weapon().active() == gameplay::WeaponSlot::Rifle;
        clip_name = rifle_ready ? (idle ? "RifleCrouchIdle" : "RifleCrouchWalk") : (idle ? "Idle" : "Walk");
    }
    // With a weapon in hand, locomotion uses the weapon-held clips when they exist.
    const gameplay::WeaponSlot slot = gameplay.weapon().active();
    if (slot == gameplay::WeaponSlot::Rifle || slot == gameplay::WeaponSlot::Pistol) {
        const bool rifle = slot == gameplay::WeaponSlot::Rifle;
        const char* held = nullptr;
        // Standing still with the rifle up at the shoulder plays the aim pose.
        const bool aiming = rifle && gameplay.weapon().aim() > 0.5f;
        if (std::strcmp(clip_name, "Idle") == 0) held = rifle ? (aiming ? "RifleAim" : "RifleIdleAim") : "PistolIdle";
        else if (std::strcmp(clip_name, "Walk") == 0) held = rifle ? "RifleWalk" : "PistolWalk";
        else if (std::strcmp(clip_name, "Run") == 0) held = rifle ? "RifleRun" : "PistolRun";
        if (held != nullptr && impl_->character->hasClip(held)) {
            clip_name = held;
        }
    }
    if (gameplay.weapon().attacking() && impl_->character->hasClip("PunchCombo")) {
        clip_name = "PunchCombo";
        clip_time = gameplay.weapon().attackProgress() * impl_->character->clipDuration("PunchCombo");
    }
    impl_->played_clip = clip_name;
    impl_->clip_duration = impl_->character->clipDuration(clip_name);
    impl_->clip_time = impl_->clip_duration > 0.0f ? std::fmod(clip_time, impl_->clip_duration) : 0.0f;

    // The pose: the locomotion clip below the spine, with the rifle's fire clip as an
    // upper-body layer while shooting, and the spine leaning toward the aim.
    const gameplay::WeaponState& weapon = gameplay.weapon();
    const bool rifle_ready = weapon.active() == gameplay::WeaponSlot::Rifle;
    assets::CharacterModel::PoseRequest request;
    request.clip = clip_name;
    request.time = clip_time;
    if (rifle_ready && weapon.firing() && impl_->character->hasClip("RifleFire")) {
        request.upper_clip = "RifleFire";
        request.upper_time = weapon.fireProgress() * impl_->character->clipDuration("RifleFire");
    }
    // The aim weight: 1 while firing, and the aim amount while aimed down the scope.
    const float aim_weight = rifle_ready ? (weapon.firing() ? 1.0f : weapon.aim()) : 0.0f;
    const float lean = std::fmax(-kSpineBendLimit, std::fmin(kSpineBendLimit, kSpineBendPerPitch * gameplay.cameraPitch()));
    request.spine_bend = aim_weight * lean;

    // A held rifle is placed by the hands, then the left arm reaches the fore-end.
    // Two passes: the left hand's goal depends on where the rifle is.
    const bool rifle_held = impl_->hand_joint >= 0 && impl_->left_hand_joint >= 0 && rifle_ready &&
        impl_->rifle_mesh.index_count > 0;
    impl_->muzzle_valid = false;
    Mat4 rifle_model = math::identity();
    impl_->character->evaluatePose(request, root, impl_->ground_height, impl_->joint_matrices);
    if (rifle_held) {
        const Mat4 hand = math::multiply(impl_->joint_matrices[impl_->hand_joint],
            impl_->character->jointBindWorld(impl_->hand_joint));
        const Mat4 left = math::multiply(impl_->joint_matrices[impl_->left_hand_joint],
            impl_->character->jointBindWorld(impl_->left_hand_joint));
        const float grip_world[3] = {hand.m[12], hand.m[13] - 0.3f * (1.0f - weapon.raise()), hand.m[14]};

        // Direction: from the grip toward the fore-end the hands hold, blended into the
        // camera's aim line as the rifle is raised and fired. Shots go where the rifle points.
        float hand_dir[3] = {left.m[12] - hand.m[12], left.m[13] - hand.m[13], left.m[14] - hand.m[14]};
        const float hand_len = std::sqrt(hand_dir[0] * hand_dir[0] + hand_dir[1] * hand_dir[1] + hand_dir[2] * hand_dir[2]);
        if (hand_len > 1e-4f) {
            for (float& c : hand_dir) c /= hand_len;
        } else {
            hand_dir[0] = 0.0f; hand_dir[1] = 0.0f; hand_dir[2] = 1.0f;
        }
        const gameplay::Ray aim = gameplay.cameraRay();
        float dir[3];
        float dir_len = 0.0f;
        for (int c = 0; c < 3; ++c) {
            dir[c] = (1.0f - aim_weight) * hand_dir[c] + aim_weight * aim.direction[c];
            dir_len += dir[c] * dir[c];
        }
        dir_len = std::sqrt(dir_len);
        for (float& c : dir) c /= dir_len;
        rifle_model = gameplay::makeAimedWeaponTransform(grip_world, dir, kRifleGripLocal);

        // Second pass: the left hand reaches the fore-end, on the rifle.
        const Mat4& r = rifle_model;
        const float p[3] = {kRifleForegripLocal[0], kRifleForegripLocal[1], kRifleForegripLocal[2]};
        const float fore[3] = {r.m[0] * p[0] + r.m[4] * p[1] + r.m[8] * p[2] + r.m[12],
            r.m[1] * p[0] + r.m[5] * p[1] + r.m[9] * p[2] + r.m[13],
            r.m[2] * p[0] + r.m[6] * p[1] + r.m[10] * p[2] + r.m[14]};
        request.left_hand_goal = fore;
        impl_->character->evaluatePose(request, root, impl_->ground_height, impl_->joint_matrices);

        // The muzzle is the rifle's -Z end, half its 0.95 m length from the centre.
        const float tip[3] = {0.0f, 0.0f, -0.475f};
        const float* m = rifle_model.m;
        impl_->muzzle[0] = m[0] * tip[0] + m[4] * tip[1] + m[8] * tip[2] + m[12];
        impl_->muzzle[1] = m[1] * tip[0] + m[5] * tip[1] + m[9] * tip[2] + m[13];
        impl_->muzzle[2] = m[2] * tip[0] + m[6] * tip[1] + m[10] * tip[2] + m[14];
        impl_->muzzle_valid = true;
    }
    const Mat4 model = math::identity();
    glUniformMatrix4fv(impl_->toon_joints, kJoints, GL_FALSE, impl_->joint_matrices[0].m);
    glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, view_projection.m);
    glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, model.m);
    setToonSurface(false);
    glBindTexture(GL_TEXTURE_2D, impl_->warrior_texture);
    glBindVertexArray(impl_->warrior.vao);
    glDrawElements(GL_TRIANGLES, impl_->warrior.index_count, GL_UNSIGNED_SHORT, nullptr);

    // The equipped prop follows the posed right hand. Fists show no prop; a switch
    // lowers the old prop and raises the new one.
    if (impl_->hand_joint >= 0 && weapon.active() != gameplay::WeaponSlot::Fists) {
        const bool rifle = weapon.active() == gameplay::WeaponSlot::Rifle;
        const GpuMesh& prop = rifle ? impl_->rifle_mesh : impl_->pistol_mesh;
        if (prop.index_count > 0) {
            const Mat4 hand = math::multiply(impl_->joint_matrices[impl_->hand_joint],
                impl_->character->jointBindWorld(impl_->hand_joint));
            // The rifle is held at the grip (see above). The pistol keeps the carry
            // pose: at the hand, following the warrior's facing.
            Mat4 prop_model = rifle_model;
            if (!rifle) {
                Mat4 facing = root;
                facing.m[12] = facing.m[13] = facing.m[14] = 0.0f;
                const Mat4 at_hand = math::translation(hand.m[12], hand.m[13] - 0.3f * (1.0f - weapon.raise()), hand.m[14]);
                prop_model = math::multiply(at_hand, math::multiply(facing, kPistolGrip));
            }
            glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, prop_model).m);
            glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, prop_model.m);
            setToonSurface(false, 0.45f);
            glBindTexture(GL_TEXTURE_2D, rifle ? impl_->rifle_texture : impl_->pistol_texture);
            glBindVertexArray(prop.vao);
            glDrawElements(GL_TRIANGLES, prop.index_count, GL_UNSIGNED_SHORT, nullptr);
        }
    }

    // Scope: only with the rifle in hand, fading in with the aim.
    // Walls: grey boxes on the terrain.
    if (impl_->wall_mesh.index_count > 0) {
        glBindTexture(GL_TEXTURE_2D, impl_->white_texture);
        setToonSurface(false);
        glBindVertexArray(impl_->wall_mesh.vao);
        for (const world::Box& wall : impl_->world->walls()) {
            const float centre[3] = {0.5f * (wall.min[0] + wall.max[0]), 0.5f * (wall.min[1] + wall.max[1]), 0.5f * (wall.min[2] + wall.max[2])};
            const float size[3] = {wall.max[0] - wall.min[0], wall.max[1] - wall.min[1], wall.max[2] - wall.min[2]};
            const float no_rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const Mat4 model = math::fromTrs(centre, no_rotation, size);
            glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, model).m);
            glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, model.m);
            glDrawElements(GL_TRIANGLES, impl_->wall_mesh.index_count, GL_UNSIGNED_SHORT, nullptr);
        }
    }

    // Shooting targets: each standing target is a cube on the terrain.
    if (impl_->cube_mesh.index_count > 0) {
        glBindTexture(GL_TEXTURE_2D, impl_->white_texture);
        setToonSurface(false);
        glBindVertexArray(impl_->cube_mesh.vao);
        for (const gameplay::Target& target : gameplay.targets().targets()) {
            if (!target.standing && target.time_down >= gameplay::Targets::kFallTime) {
                continue;  // knocked down and gone until it respawns
            }
            // A hit target tips over about its base, faster as it goes, before it disappears.
            float tilt = 0.0f;
            if (!target.standing) {
                const float t = target.time_down / gameplay::Targets::kFallTime;
                tilt = 0.5f * kPi * t * t;
            }
            const float half_height = 0.5f * target.height;
            const float base[3] = {target.center[0], target.center[1] - half_height, target.center[2]};
            const float centre[3] = {base[0], base[1] + half_height * std::cos(tilt), base[2] + half_height * std::sin(tilt)};
            const float axis_half[4] = {std::sin(0.5f * tilt), 0.0f, 0.0f, std::cos(0.5f * tilt)};
            const float size[3] = {target.size, target.height, target.size};
            const Mat4 model = math::fromTrs(centre, axis_half, size);
            glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, math::multiply(view_projection, model).m);
            glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, model.m);
            glDrawElements(GL_TRIANGLES, impl_->cube_mesh.index_count, GL_UNSIGNED_SHORT, nullptr);
        }
    }

    const float scope = weapon.active() == gameplay::WeaponSlot::Rifle ? weapon.aim() : 0.0f;
    if (scope > 0.001f && impl_->scope_program != 0) {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(impl_->scope_program);
        glUniform1f(impl_->scope_aspect, framebuffer_width > 0 && framebuffer_height > 0
            ? static_cast<float>(framebuffer_width) / static_cast<float>(framebuffer_height)
            : 16.0f / 9.0f);
        glUniform1f(impl_->scope_alpha, scope);
        glBindVertexArray(impl_->scope_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    if (impl_->style.show_skeleton && impl_->skeleton_program != 0) {
        // Joint world positions: skin matrix times bind pose (the inverse of the bind's inverse).
        const int joint_count = impl_->character->jointCount();
        std::vector<float> bones;
        std::vector<float> points;
        std::vector<Mat4> world(joint_count);
        for (int j = 0; j < joint_count; ++j) {
            world[j] = math::multiply(impl_->joint_matrices[j], impl_->character->jointBindWorld(j));
        }
        for (int j = 0; j < joint_count; ++j) {
            const Mat4& w = world[j];
            points.insert(points.end(), {w.m[12], w.m[13], w.m[14]});
            const int parent = impl_->character->jointParent(j);
            if (parent >= 0) {
                const Mat4& pw = world[parent];
                bones.insert(bones.end(), {pw.m[12], pw.m[13], pw.m[14], w.m[12], w.m[13], w.m[14]});
            }
        }

        glDisable(GL_DEPTH_TEST);
#if !defined(CHI_GLES)
        glEnable(0x8642);  // GL_PROGRAM_POINT_SIZE, so the shader's point size applies
#endif
        glUseProgram(impl_->skeleton_program);
        glUniformMatrix4fv(impl_->skeleton_mvp, 1, GL_FALSE, view_projection.m);
        glBindVertexArray(impl_->skeleton_vao);
        glBindBuffer(GL_ARRAY_BUFFER, impl_->skeleton_vbo);

        glEnableVertexAttribArray(kAttrPosition);
        glVertexAttribPointer(kAttrPosition, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

        // Bones: yellow lines.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bones.size() * sizeof(float)), bones.data(), GL_DYNAMIC_DRAW);
        const float bone_color[4] = {1.0f, 0.9f, 0.2f, 1.0f};
        glUniform4fv(impl_->skeleton_color, 1, bone_color);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(bones.size() / 3));

        // Joints: red points.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(points.size() * sizeof(float)), points.data(), GL_DYNAMIC_DRAW);
        const float joint_color[4] = {1.0f, 0.25f, 0.25f, 1.0f};
        glUniform4fv(impl_->skeleton_color, 1, joint_color);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(points.size() / 3));

        glEnable(GL_DEPTH_TEST);
    }

    glBindVertexArray(0);
}

float Renderer::clipTime() const {
    return impl_->clip_time;
}

float Renderer::clipDuration() const {
    return impl_->clip_duration;
}

const char* Renderer::playedClip() const {
    return impl_->played_clip;
}

bool Renderer::projectPoint(const float p[3], float& ndc_x, float& ndc_y) const {
    const float* m = impl_->last_view_projection.m;
    const float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (w <= 1e-4f) {
        return false;
    }
    ndc_x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / w;
    ndc_y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / w;
    return true;
}

bool Renderer::muzzlePoint(float out[3]) const {
    if (impl_->muzzle_valid) {
        out[0] = impl_->muzzle[0];
        out[1] = impl_->muzzle[1];
        out[2] = impl_->muzzle[2];
    }
    return impl_->muzzle_valid;
}

StyleSettings& Renderer::styleSettings() {
    return impl_->style;
}

void Renderer::cleanup() {
    if (impl_ == nullptr) {
        return;
    }

    for (GpuMesh* mesh : {&impl_->rifle_mesh, &impl_->pistol_mesh, &impl_->cube_mesh, &impl_->wall_mesh}) {
        if (mesh->vao != 0) {
            glDeleteVertexArrays(1, &mesh->vao);
            glDeleteBuffers(1, &mesh->vbo);
            glDeleteBuffers(1, &mesh->ibo);
        }
    }
    for (GLuint texture : {impl_->rifle_texture, impl_->pistol_texture}) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    glDeleteVertexArrays(1, &impl_->skeleton_vao);
    glDeleteBuffers(1, &impl_->skeleton_vbo);
    glDeleteVertexArrays(1, &impl_->scope_vao);
    glDeleteBuffers(1, &impl_->scope_vbo);
    glDeleteProgram(impl_->scope_program);

    glDeleteProgram(impl_->toon_program);
    delete impl_;
    impl_ = nullptr;
}

} // namespace renderer

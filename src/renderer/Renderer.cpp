#include "renderer/Renderer.h"

#include "assets/CharacterModel.h"
#include "assets/MeshVertex.h"
#include "gameplay/GameState.h"
#include "gameplay/WeaponAttachment.h"
#include "gameplay/WeaponState.h"
#include "assets/StaticModel.h"
#include "math/Mat4.h"
#include "renderer/CityPass.h"
#include "renderer/GlUtil.h"
#include "renderer/PostPass.h"
#include "renderer/ResolutionScale.h"
#include "renderer/ShaderChunks.h"
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
// The near plane stays close for the scope and the F3 camera; the far plane sits
// past the fog, so the 24-bit depth buffer still separates distant facades.
constexpr float kNearPlane = 0.05f;
constexpr float kFarPlane = 650.0f;
constexpr float kCullDistance = 520.0f;  // boxes beyond this are lost in the haze
constexpr float kGroundHalfSize = world::World::kHalfSize;
constexpr int kJoints = assets::kMaxJoints;

// The rifle's grip and fore-end points in prop space, in metres from the prop's centre.
// The grip is under the receiver; the fore-end is the front of the handguard. Tuned by eye.
// The rifle mesh points its muzzle along -Z; the held rifle turns it half a turn
// about the grip so the muzzle faces forward. The grip sits just behind the
// receiver (toward the stock, +Z) and the fore-end just ahead of it (toward the
// muzzle, -Z). Tuned by eye from a side view.
const float kRifleGripLocal[3] = {0.0f, -0.075f, 0.075f};  // the pistol grip, measured from the mesh
const float kRifleForegripLocal[3] = {0.0f, -0.03f, -0.05f};  // fore-end marker near the receiver
// The spine leans toward the aim: this many radians per radian of camera pitch, up to a limit.
constexpr float kSpineBendPerPitch = 0.8f;
constexpr float kSpineBendLimit = 0.5f;
const math::Mat4 kPistolGrip = math::identity();

static_assert(kJoints == 32, "the skinning shader declares joints[32]");

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
    return std::string(CHI_GLSL_HEADER) + kAtmosphereGlsl + R"(
        uniform vec4 light_dir_bands;  // w: toon bands (the direction comes from the sun)
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

            if (surface.x > 0.5) {
                // The park: mown grass in stripes, with the sidewalk ring at its edge.
                vec2 p = world_pos.xz;
                float grain = groundGrain(p);
                vec3 c;
                if (max(abs(p.x), abs(p.y)) > 19.0) {
                    c = vec3(0.40, 0.39, 0.37) + grain;
                } else {
                    float stripe = step(0.5, fract((p.x + p.y * 0.15) / 3.0));
                    c = mix(vec3(0.15, 0.23, 0.09), vec3(0.18, 0.27, 0.11), stripe) + grain * vec3(0.6, 1.0, 0.4);
                }
                vec3 lit = c * (sun_color * max(dot(n, sun_dir), 0.0) * 0.9 + hemiAmbient(n));
                lit += c * vec3(1.0, 0.62, 0.30) * lampPools(p) * fog_params.y * 2.2;
                frag_color = vec4(applyFog(lit, world_pos), 1.0);
                return;
            }

            vec3 light_dir = normalize(sun_dir);
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

            if (style.x > 0.5) {
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
            final_color *= 1.0 + surface.y;

            // The time of day tints the toon shading, so the warrior sits in the scene.
            vec3 tint = clamp(sun_color * 0.55 + amb_sky * 0.9, vec3(0.28), vec3(1.1));
            final_color *= tint;
            final_color += albedo * vec3(1.0, 0.62, 0.30) * lampPools(world_pos.xz) * fog_params.y * 0.8;
            frag_color = vec4(applyFog(final_color, world_pos), 1.0);
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
            // Only the crosshair: no darkening outside the view, so the scene stays visible.
            float hair_x = step(abs(p.x), 0.0025) * step(0.02, abs(p.y)) * step(abs(p.y), 0.30);
            float hair_y = step(abs(p.y), 0.0025) * step(0.02, abs(p.x)) * step(abs(p.x), 0.30);
            float reticle = max(hair_x, hair_y);
            frag_color = vec4(0.0, 0.0, 0.0, reticle * alpha);
        }
    )";
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
    return math::perspective(fov_radians, aspect, kNearPlane, kFarPlane);
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
    float clip_time = 0.0f;      // last frame: time within the clip, wrapped
    const char* played_clip = "Idle";
    Mat4 last_view_projection = math::identity();
    bool muzzle_valid = false;
    float muzzle[3] = {0.0f, 0.0f, 0.0f};
    RiflePoints rifle_points{};
    bool rifle_points_valid = false;
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
    GLuint rifle_texture = 0;
    GLuint pistol_texture = 0;
    int hand_joint = -1;

    const assets::CharacterModel* character = nullptr;
    const world::World* world = nullptr;
    assets::GroundHeightFn ground_height;
    Mat4 joint_matrices[kJoints]{};
    StyleSettings style{};

    CityPass city;
    PostPass post;
    ResolutionScale resolution;
    AtmosphereUniforms toon_atmosphere;
    Stats stats;
};

void Renderer::init(const assets::CharacterModel& character, const world::World& world, const assets::StaticMesh& rifle,
    const assets::StaticMesh& pistol) {
    impl_ = new Impl{};
    impl_->character = &character;
    impl_->hand_joint = character.jointIndex("RightHand");

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

    const uint8_t white[4] = {255, 255, 255, 255};
    impl_->white_texture = uploadTexture(white, 1, 1);
    impl_->ground_texture = impl_->white_texture;
    impl_->warrior_texture = mesh.base_color_rgba.empty()
        ? impl_->white_texture
        : uploadTexture(mesh.base_color_rgba.data(), mesh.texture_width, mesh.texture_height);

    impl_->toon_program = buildMeshProgram(toonVertexShaderSource(), toonFragmentShaderSource(), "toon");
    if (impl_->toon_program != 0) {
        impl_->toon_mvp = glGetUniformLocation(impl_->toon_program, "mvp");
        impl_->toon_model = glGetUniformLocation(impl_->toon_program, "model");
        impl_->toon_joints = glGetUniformLocation(impl_->toon_program, "joints");
        impl_->toon_light_dir_bands = glGetUniformLocation(impl_->toon_program, "light_dir_bands");
        impl_->toon_style = glGetUniformLocation(impl_->toon_program, "style");
        impl_->toon_surface = glGetUniformLocation(impl_->toon_program, "surface");
        impl_->toon_atmosphere.locate(impl_->toon_program);
        glUseProgram(impl_->toon_program);
        glUniform1i(glGetUniformLocation(impl_->toon_program, "base_color"), 0);
    }

    impl_->skeleton_program = buildProgram(skeletonVertexShaderSource(), skeletonFragmentShaderSource(), "skeleton", {{kAttrPosition, "position"}});
    if (impl_->skeleton_program != 0) {
        impl_->skeleton_mvp = glGetUniformLocation(impl_->skeleton_program, "mvp");
        impl_->skeleton_color = glGetUniformLocation(impl_->skeleton_program, "color");
    }
    glGenVertexArrays(1, &impl_->skeleton_vao);
    glGenBuffers(1, &impl_->skeleton_vbo);

    impl_->scope_program = buildProgram(scopeVertexShaderSource(), scopeFragmentShaderSource(), "scope", {{kAttrPosition, "position"}});
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

    if (!impl_->city.init(world)) {
        std::fprintf(stderr, "[renderer] city pass failed to build; the city will not draw\n");
    }
    impl_->post.init();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    // glTF meshes wind counter-clockwise; front faces are the outward ones.
    glFrontFace(GL_CCW);
}

void Renderer::draw(const gameplay::GameState& gameplay, int framebuffer_width, int framebuffer_height, float frame_seconds) {
    StyleSettings& style_settings = impl_->style;
    if (style_settings.advance_time) {
        style_settings.time_of_day = std::fmod(style_settings.time_of_day + frame_seconds / 60.0f, 24.0f);
    }
    const Atmosphere atmosphere = atmosphereAt(style_settings.time_of_day, style_settings.fog_scale);

    // The scene goes to the offscreen target at the dynamic scale, or straight to the
    // window if that target is not available.
    impl_->resolution.setEnabled(style_settings.dynamic_resolution);
    impl_->resolution.setFixedScale(style_settings.fixed_scale);
    const ResolutionScale::Quality quality = impl_->resolution.update(frame_seconds, framebuffer_width, framebuffer_height);
    const float scale = quality.scale;
    const bool offscreen = impl_->post.begin(framebuffer_width, framebuffer_height, scale, quality.msaa ? style_settings.msaa_samples : 0);
    if (!offscreen) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebuffer_width, framebuffer_height);
    }
    impl_->stats.render_scale = offscreen ? scale : 1.0f;
    impl_->stats.offscreen = offscreen;
    impl_->stats.scene_width = offscreen ? impl_->post.sceneWidth() : framebuffer_width;
    impl_->stats.scene_height = offscreen ? impl_->post.sceneHeight() : framebuffer_height;
    impl_->stats.samples = offscreen ? impl_->post.samples() : 0;

    glClearColor(atmosphere.sky_horizon[0], atmosphere.sky_horizon[1], atmosphere.sky_horizon[2], 1.0f);
#if defined(CHI_GLES)
    glClearDepthf(1.0f);
#else
    glClearDepth(1.0);
#endif
    // Set every piece of state the passes rely on: the UI drawn last frame leaves
    // blending and scissoring on and culling off, and is not asked to restore them.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The player model stands at the player position.
    const float px = gameplay.playerX();
    const float pz = gameplay.playerZ();

    // Third-person camera: looks at the upper body from behind and above.
    // Orbit view, or the sight view from the right shoulder while aimed (see cameraView).
    // The drawn view includes camera shake; shots use the steady one.
    float eye[3];
    float look[3];
    gameplay.renderCameraView(eye, look);
    const float eye_x = eye[0];
    const float eye_y = eye[1];
    const float eye_z = eye[2];
    const Mat4 view = math::lookAt(eye_x, eye_y, eye_z, look[0], look[1], look[2]);
    // Aiming narrows the view; speed in a car widens it.
    const float fov = gameplay.cameraFov();
    const Mat4 view_projection = math::multiply(perspectiveFor(framebuffer_width, framebuffer_height, fov), view);
    impl_->last_view_projection = view_projection;

    // The city: floor and every box, frustum culled, in one instanced draw.
    CityFrame city_frame{};
    city_frame.view_projection = view_projection;
    city_frame.frustum = math::Frustum::fromViewProjection(view_projection);
    for (int c = 0; c < 3; ++c) {
        city_frame.eye[c] = eye[c];
        // lookAt's rows are the camera's right, up and back axes.
        city_frame.right[c] = view.m[c * 4 + 0];
        city_frame.up[c] = view.m[c * 4 + 1];
        city_frame.forward[c] = -view.m[c * 4 + 2];
    }
    city_frame.tan_half_fov = std::tan(0.5f * fov);
    city_frame.aspect = framebuffer_height > 0 ? static_cast<float>(framebuffer_width) / static_cast<float>(framebuffer_height) : 1.0f;
    city_frame.atmosphere = atmosphere;
    city_frame.max_distance = kCullDistance;
    impl_->city.drawOpaque(city_frame, gameplay);
    impl_->stats.visible_boxes = impl_->city.visibleInstances();
    impl_->stats.total_boxes = impl_->city.totalInstances();

    // brighten lifts a prop's dark metal and wood out of the body's shadows.
    auto setToonSurface = [this](bool ground, float brighten = 0.0f) {
        const float light[4] = {0.0f, 0.0f, 0.0f, std::floor(impl_->style.toon_bands + 0.5f)};
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
    impl_->toon_atmosphere.set(atmosphere, eye);
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
    // Pose the character from its animation clips. The rifle follows the posed
    // hand while carried and the sight line while aimed; neither arm is solved to it.
    impl_->character->evaluatePose(request, root, impl_->ground_height, impl_->joint_matrices);
    const bool rifle_held = impl_->hand_joint >= 0 && rifle_ready &&
        impl_->rifle_mesh.index_count > 0;
    impl_->muzzle_valid = false;
    impl_->rifle_points_valid = false;
    Mat4 rifle_model = math::identity();
    if (rifle_held) {
        const Mat4 hand = math::multiply(impl_->joint_matrices[impl_->hand_joint],
            impl_->character->jointBindWorld(impl_->hand_joint));
        float grip_world[3] = {hand.m[12], hand.m[13] - 0.3f * (1.0f - weapon.raise()), hand.m[14]};
        if (weapon.aim() > 0.5f) gameplay.sightGrip(grip_world);

        // Direction: from the grip toward the fore-end, blended into the
        // camera's aim line as the rifle is raised and fired. Shots go where the rifle points.
        // Unaimed, the rifle lies level along the warrior's facing, so it never points
        // up or down with the clip's hands. Aimed, it follows the aim line.
        float hand_dir[3] = {root.m[8], 0.0f, root.m[10]};
        const float hand_len = std::sqrt(hand_dir[0] * hand_dir[0] + hand_dir[2] * hand_dir[2]);
        if (hand_len > 1e-4f) {
            hand_dir[0] /= hand_len;
            hand_dir[2] /= hand_len;
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

        // Contact points for the debug overlay only.
        const Mat4& r = rifle_model;
        const float p[3] = {kRifleForegripLocal[0], kRifleForegripLocal[1], kRifleForegripLocal[2]};
        const float fore[3] = {r.m[0] * p[0] + r.m[4] * p[1] + r.m[8] * p[2] + r.m[12],
            r.m[1] * p[0] + r.m[5] * p[1] + r.m[9] * p[2] + r.m[13],
            r.m[2] * p[0] + r.m[6] * p[1] + r.m[10] * p[2] + r.m[14]};
        // The muzzle is the rifle's -Z end, half its 0.95 m length from the centre.
        const float tip[3] = {0.0f, 0.0f, -0.475f};
        const float* m = rifle_model.m;
        impl_->muzzle[0] = m[0] * tip[0] + m[4] * tip[1] + m[8] * tip[2] + m[12];
        impl_->muzzle[1] = m[1] * tip[0] + m[5] * tip[1] + m[9] * tip[2] + m[13];
        impl_->muzzle[2] = m[2] * tip[0] + m[6] * tip[1] + m[10] * tip[2] + m[14];
        impl_->muzzle_valid = true;
        for (int c = 0; c < 3; ++c) {
            impl_->rifle_points.hand[c] = hand.m[12 + c];
            impl_->rifle_points.grip[c] = grip_world[c];
            impl_->rifle_points.fore[c] = fore[c];
            impl_->rifle_points.muzzle[c] = impl_->muzzle[c];
        }
        impl_->rifle_points_valid = true;
    }
    const Mat4 model = math::identity();
    glUniformMatrix4fv(impl_->toon_joints, kJoints, GL_FALSE, impl_->joint_matrices[0].m);
    glUniformMatrix4fv(impl_->toon_mvp, 1, GL_FALSE, view_projection.m);
    glUniformMatrix4fv(impl_->toon_model, 1, GL_FALSE, model.m);
    setToonSurface(false);
    glBindTexture(GL_TEXTURE_2D, impl_->warrior_texture);
    glBindVertexArray(impl_->warrior.vao);
    if (!gameplay.driving()) {  // in a car the warrior is out of sight
        glDrawElements(GL_TRIANGLES, impl_->warrior.index_count, GL_UNSIGNED_SHORT, nullptr);
    }

    // The pistol follows the posed right hand; the rifle uses the placement above.
    // Fists show no prop; a switch lowers the old prop and raises the new one.
    if (impl_->hand_joint >= 0 && weapon.active() != gameplay::WeaponSlot::Fists && !gameplay.driving()) {
        const bool rifle = weapon.active() == gameplay::WeaponSlot::Rifle;
        const GpuMesh& prop = rifle ? impl_->rifle_mesh : impl_->pistol_mesh;
        if (prop.index_count > 0) {
            const Mat4 hand = math::multiply(impl_->joint_matrices[impl_->hand_joint],
                impl_->character->jointBindWorld(impl_->hand_joint));
            // The rifle is placed at the grip (see above). The pistol keeps the carry
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

    // Contact shadows over the opaque scene, then the sky in whatever is left.
    const float warrior_at[3] = {px, 0.0f, pz};
    impl_->city.drawShadows(city_frame, gameplay, warrior_at);
    impl_->city.drawSky(city_frame);

    // Scope: only with the rifle in hand, fading in with the aim.
    const float scope = weapon.active() == gameplay::WeaponSlot::Rifle && !gameplay.driving() ? weapon.aim() : 0.0f;
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
    impl_->post.end(framebuffer_width, framebuffer_height);
}

const Renderer::Stats& Renderer::stats() const {
    return impl_->stats;
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

bool Renderer::riflePoints(RiflePoints& out) const {
    out = impl_->rifle_points;
    return impl_->rifle_points_valid;
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

    impl_->city.cleanup();
    impl_->post.cleanup();
    for (GpuMesh* mesh : {&impl_->rifle_mesh, &impl_->pistol_mesh, &impl_->cube_mesh}) {
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

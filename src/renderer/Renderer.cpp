#include "renderer/Renderer.h"

#include "app/SokolLog.h"
#include "debug/DebugUi.h"
#include "gameplay/GameState.h"

#include "sokol_gfx.h"
#include "sokol_glue.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
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

struct ToonUniforms {
    Mat4 mvp;
    Mat4 model;
    float light_dir_bands[4];
    float style[4];
    float surface[4];
};

struct OutlineUniforms {
    Mat4 mvp;
    float outline_color_thickness[4];
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

const char* vertexShaderSource() {
    return R"(
        #version 330
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

const char* fragmentShaderSource() {
    return R"(
        #version 330
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
    return R"(
        #version 330
        uniform mat4 mvp;
        uniform vec4 outline_color_thickness;

        in vec3 position;
        in vec3 normal0;

        void main() {
            vec3 expanded = position + normal0 * outline_color_thickness.w;
            gl_Position = mvp * vec4(expanded, 1.0);
        }
    )";
}

const char* outlineFragmentShaderSource() {
    return R"(
        #version 330
        uniform vec4 outline_color_thickness;
        out vec4 frag_color;

        void main() {
            frag_color = vec4(outline_color_thickness.rgb, 1.0);
        }
    )";
}

ToonUniforms makeToonUniforms(const Mat4& view_projection, const Mat4& model, const StyleSettings& style, bool ground) {
    ToonUniforms uniforms{};
    uniforms.mvp = multiply(view_projection, model);
    uniforms.model = model;
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

OutlineUniforms makeOutlineUniforms(const Mat4& view_projection, const Mat4& model, const StyleSettings& style) {
    OutlineUniforms uniforms{};
    uniforms.mvp = multiply(view_projection, model);
    uniforms.outline_color_thickness[0] = style.outline_color[0];
    uniforms.outline_color_thickness[1] = style.outline_color[1];
    uniforms.outline_color_thickness[2] = style.outline_color[2];
    uniforms.outline_color_thickness[3] = style.outline_thickness;
    return uniforms;
}

} // namespace

struct Renderer::Impl {
    sg_buffer vertex_buffer{};
    sg_buffer index_buffer{};
    sg_pipeline toon_pipeline{};
    sg_pipeline outline_pipeline{};
    sg_bindings bindings{};
    sg_pass_action pass_action{};
    StyleSettings style{};
};

void Renderer::init() {
    impl_ = new Impl{};

    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = app::sokolLog;
    sg_setup(&desc);

    sg_buffer_desc buffer_desc{};
    buffer_desc.data.ptr = kVertices;
    buffer_desc.data.size = sizeof(kVertices);
    buffer_desc.label = "scene-vertices";
    impl_->vertex_buffer = sg_make_buffer(&buffer_desc);

    uint16_t indices[42]{};
    for (size_t i = 0; i < 36; ++i) {
        indices[i] = kCubeIndices[i];
    }
    for (size_t i = 0; i < 6; ++i) {
        indices[36 + i] = kGroundIndices[i];
    }

    sg_buffer_desc index_desc{};
    index_desc.usage.vertex_buffer = false;
    index_desc.usage.index_buffer = true;
    index_desc.usage.immutable = true;
    index_desc.data.ptr = indices;
    index_desc.data.size = sizeof(indices);
    index_desc.label = "scene-indices";
    impl_->index_buffer = sg_make_buffer(&index_desc);

    sg_shader_desc shader_desc{};
    shader_desc.vertex_func.source = vertexShaderSource();
    shader_desc.fragment_func.source = fragmentShaderSource();
    shader_desc.attrs[0].glsl_name = "position";
    shader_desc.attrs[1].glsl_name = "normal0";
    shader_desc.attrs[2].glsl_name = "color0";
    shader_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shader_desc.uniform_blocks[0].size = sizeof(ToonUniforms);
    shader_desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    shader_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shader_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    shader_desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "model";
    shader_desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_MAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[1].array_count = 1;
    shader_desc.uniform_blocks[0].glsl_uniforms[2].glsl_name = "light_dir_bands";
    shader_desc.uniform_blocks[0].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[2].array_count = 1;
    shader_desc.uniform_blocks[0].glsl_uniforms[3].glsl_name = "style";
    shader_desc.uniform_blocks[0].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[3].array_count = 1;
    shader_desc.uniform_blocks[0].glsl_uniforms[4].glsl_name = "surface";
    shader_desc.uniform_blocks[0].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[4].array_count = 1;
    shader_desc.label = "toon-shader";
    const sg_shader shader = sg_make_shader(&shader_desc);

    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader;
    pipeline_desc.layout.buffers[0].stride = sizeof(Vertex);
    pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc.index_type = SG_INDEXTYPE_UINT16;
    pipeline_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.cull_mode = SG_CULLMODE_BACK;
    pipeline_desc.label = "toon-pipeline";
    impl_->toon_pipeline = sg_make_pipeline(&pipeline_desc);

    sg_shader_desc outline_shader_desc{};
    outline_shader_desc.vertex_func.source = outlineVertexShaderSource();
    outline_shader_desc.fragment_func.source = outlineFragmentShaderSource();
    outline_shader_desc.attrs[0].glsl_name = "position";
    outline_shader_desc.attrs[1].glsl_name = "normal0";
    outline_shader_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    outline_shader_desc.uniform_blocks[0].size = sizeof(OutlineUniforms);
    outline_shader_desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "outline_color_thickness";
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    outline_shader_desc.uniform_blocks[0].glsl_uniforms[1].array_count = 1;
    outline_shader_desc.label = "outline-shader";
    const sg_shader outline_shader = sg_make_shader(&outline_shader_desc);

    sg_pipeline_desc outline_pipeline_desc{};
    outline_pipeline_desc.shader = outline_shader;
    outline_pipeline_desc.layout.buffers[0].stride = sizeof(Vertex);
    outline_pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    outline_pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    outline_pipeline_desc.index_type = SG_INDEXTYPE_UINT16;
    outline_pipeline_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    outline_pipeline_desc.depth.write_enabled = false;
    outline_pipeline_desc.cull_mode = SG_CULLMODE_FRONT;
    outline_pipeline_desc.label = "outline-pipeline";
    impl_->outline_pipeline = sg_make_pipeline(&outline_pipeline_desc);

    impl_->bindings.vertex_buffers[0] = impl_->vertex_buffer;
    impl_->bindings.index_buffer = impl_->index_buffer;
    impl_->pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    impl_->pass_action.colors[0].clear_value = {0.08f, 0.09f, 0.11f, 1.0f};
    impl_->pass_action.depth.load_action = SG_LOADACTION_CLEAR;
    impl_->pass_action.depth.clear_value = 1.0f;
}

void Renderer::draw(const gameplay::GameState& gameplay, debug::DebugUi& debug_ui) {
    sg_pass pass{};
    pass.action = impl_->pass_action;
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    sg_apply_pipeline(impl_->toon_pipeline);
    sg_apply_bindings(&impl_->bindings);

    const float aspect = pass.swapchain.width > 0 && pass.swapchain.height > 0
        ? static_cast<float>(pass.swapchain.width) / static_cast<float>(pass.swapchain.height)
        : 16.0f / 9.0f;
    const Mat4 projection = perspective(60.0f * kPi / 180.0f, aspect, 0.01f, 100.0f);
    const Mat4 view = lookAt(4.5f, 3.5f, 6.0f, 0.0f, 0.0f, 0.0f);
    const Mat4 view_projection = multiply(projection, view);

    const Mat4 ground_model = identity();
    ToonUniforms ground_uniforms = makeToonUniforms(view_projection, ground_model, impl_->style, true);
    sg_range ground_range{&ground_uniforms, sizeof(ground_uniforms)};
    sg_apply_uniforms(0, &ground_range);
    sg_draw(36, 6, 1);

    const Mat4 model = translation(gameplay.playerX(), 0.0f, gameplay.playerZ());
    ToonUniforms cube_uniforms = makeToonUniforms(view_projection, model, impl_->style, false);
    sg_range cube_range{&cube_uniforms, sizeof(cube_uniforms)};
    sg_apply_uniforms(0, &cube_range);
    sg_draw(0, 36, 1);

    if (impl_->style.outline_thickness > 0.0f) {
        OutlineUniforms outline_uniforms = makeOutlineUniforms(view_projection, model, impl_->style);
        sg_range outline_range{&outline_uniforms, sizeof(outline_uniforms)};
        sg_apply_pipeline(impl_->outline_pipeline);
        sg_apply_bindings(&impl_->bindings);
        sg_apply_uniforms(0, &outline_range);
        sg_draw(0, 36, 1);
    }

    debug_ui.render();
    sg_end_pass();
    sg_commit();
}

StyleSettings& Renderer::styleSettings() {
    return impl_->style;
}

void Renderer::cleanup() {
    if (impl_ == nullptr) {
        return;
    }

    sg_shutdown();
    delete impl_;
    impl_ = nullptr;
}

} // namespace renderer

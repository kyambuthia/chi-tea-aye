#include "renderer/Renderer.h"

#include "app/SokolLog.h"
#include "debug/DebugUi.h"

#include "sokol_gfx.h"
#include "sokol_glue.h"

#include <new>

namespace renderer {
namespace {

struct Vertex {
    float position[3];
    float color[4];
};

constexpr Vertex kTriangle[] = {
    {{0.0f, 0.6f, 0.0f}, {0.95f, 0.25f, 0.18f, 1.0f}},
    {{0.55f, -0.45f, 0.0f}, {0.10f, 0.70f, 0.95f, 1.0f}},
    {{-0.55f, -0.45f, 0.0f}, {0.95f, 0.85f, 0.20f, 1.0f}},
};

const char* vertexShaderSource() {
    return R"(
        #version 330
        in vec3 position;
        in vec4 color0;
        out vec4 color;

        void main() {
            gl_Position = vec4(position, 1.0);
            color = color0;
        }
    )";
}

const char* fragmentShaderSource() {
    return R"(
        #version 330
        in vec4 color;
        out vec4 frag_color;

        void main() {
            frag_color = color;
        }
    )";
}

} // namespace

struct Renderer::Impl {
    sg_buffer vertex_buffer{};
    sg_pipeline pipeline{};
    sg_bindings bindings{};
    sg_pass_action pass_action{};
};

void Renderer::init() {
    impl_ = new Impl{};

    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = app::sokolLog;
    sg_setup(&desc);

    sg_buffer_desc buffer_desc{};
    buffer_desc.data.ptr = kTriangle;
    buffer_desc.data.size = sizeof(kTriangle);
    buffer_desc.label = "triangle-vertices";
    impl_->vertex_buffer = sg_make_buffer(&buffer_desc);

    sg_shader_desc shader_desc{};
    shader_desc.vertex_func.source = vertexShaderSource();
    shader_desc.fragment_func.source = fragmentShaderSource();
    shader_desc.attrs[0].glsl_name = "position";
    shader_desc.attrs[1].glsl_name = "color0";
    shader_desc.label = "triangle-shader";
    const sg_shader shader = sg_make_shader(&shader_desc);

    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader;
    pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc.label = "triangle-pipeline";
    impl_->pipeline = sg_make_pipeline(&pipeline_desc);

    impl_->bindings.vertex_buffers[0] = impl_->vertex_buffer;
    impl_->pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    impl_->pass_action.colors[0].clear_value = {0.08f, 0.09f, 0.11f, 1.0f};
}

void Renderer::draw(debug::DebugUi& debug_ui) {
    sg_pass pass{};
    pass.action = impl_->pass_action;
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    sg_apply_pipeline(impl_->pipeline);
    sg_apply_bindings(&impl_->bindings);
    sg_draw(0, 3, 1);
    debug_ui.render();
    sg_end_pass();
    sg_commit();
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

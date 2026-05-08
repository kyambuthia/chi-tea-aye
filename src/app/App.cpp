#include "app/App.h"

#include "app/InputState.h"
#include "app/SokolLog.h"
#include "assets/AssetSystem.h"
#include "debug/DebugUi.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "renderer/Renderer.h"
#include "world/World.h"

#include "sokol_app.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace app {
namespace {

class App {
public:
    void init() {
        assets_.init();
        world_.init();
        physics_.init();
        gameplay_.init();
        renderer_.init();
        debug_ui_.init();
    }

    void frame() {
        const auto dt = static_cast<float>(sapp_frame_duration());

        ++frame_count_;
        gameplay_.update(input_, physics_, world_, dt);

        debug_ui_.beginFrame(sapp_width(), sapp_height(), dt, sapp_dpi_scale());
        debug_ui_.draw(gameplay_, input_, renderer_.styleSettings(), frame_count_, dt);

        renderer_.draw(gameplay_, debug_ui_);
    }

    void cleanup() {
        debug_ui_.cleanup();
        renderer_.cleanup();
        gameplay_.cleanup();
        physics_.cleanup();
        world_.cleanup();
        assets_.cleanup();
    }

    void event(const sapp_event* event) {
        (void)debug_ui_.handleEvent(event);

        switch (event->type) {
            case SAPP_EVENTTYPE_MOUSE_MOVE:
                input_.mouse.x = event->mouse_x;
                input_.mouse.y = event->mouse_y;
                break;
            case SAPP_EVENTTYPE_KEY_DOWN:
            case SAPP_EVENTTYPE_KEY_UP:
                setKey(event->key_code, event->type == SAPP_EVENTTYPE_KEY_DOWN);
                break;
            default:
                break;
        }
    }

private:
    void setKey(sapp_keycode key, bool down) {
        switch (key) {
            case SAPP_KEYCODE_W:
                input_.keys.w = down;
                break;
            case SAPP_KEYCODE_A:
                input_.keys.a = down;
                break;
            case SAPP_KEYCODE_S:
                input_.keys.s = down;
                break;
            case SAPP_KEYCODE_D:
                input_.keys.d = down;
                break;
            default:
                break;
        }
    }

    InputState input_;
    assets::AssetSystem assets_;
    world::World world_;
    physics::Physics physics_;
    gameplay::GameState gameplay_;
    renderer::Renderer renderer_;
    debug::DebugUi debug_ui_;
    uint64_t frame_count_ = 0;
};

std::unique_ptr<App> g_app;

} // namespace

void sokolLog(const char* tag, uint32_t log_level, uint32_t log_item_id, const char* message, uint32_t line, const char* filename, void* user_data) {
    (void)user_data;

    std::fprintf(stderr, "[%s] level=%u item=%u %s:%u: %s\n",
        tag ? tag : "sokol",
        log_level,
        log_item_id,
        filename ? filename : "<unknown>",
        line,
        message ? message : "<no message>");

    if (log_level == 0) {
        std::abort();
    }
}

namespace {

void initCallback() {
    g_app = std::make_unique<App>();
    g_app->init();
}

void frameCallback() {
    g_app->frame();
}

void cleanupCallback() {
    g_app->cleanup();
    g_app.reset();
}

void eventCallback(const sapp_event* event) {
    g_app->event(event);
}

} // namespace

sapp_desc makeAppDesc(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    sapp_desc desc{};
    desc.init_cb = initCallback;
    desc.frame_cb = frameCallback;
    desc.cleanup_cb = cleanupCallback;
    desc.event_cb = eventCallback;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title = "chi-tea-aye prototype";
    desc.icon.sokol_default = true;
    desc.gl.major_version = 3;
    desc.gl.minor_version = 3;
    desc.logger.func = sokolLog;
    return desc;
}

} // namespace app

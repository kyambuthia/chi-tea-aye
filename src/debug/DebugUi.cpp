#include "debug/DebugUi.h"

#include "app/InputState.h"
#include "app/SokolLog.h"
#include "gameplay/GameState.h"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_imgui.h"

namespace debug {

void DebugUi::init() {
    simgui_desc_t desc{};
    desc.logger.func = app::sokolLog;
    simgui_setup(&desc);
}

void DebugUi::beginFrame(int width, int height, float delta_time, float dpi_scale) {
    simgui_frame_desc_t desc{};
    desc.width = width;
    desc.height = height;
    desc.delta_time = delta_time;
    desc.dpi_scale = dpi_scale;
    simgui_new_frame(&desc);
}

void DebugUi::draw(const gameplay::GameState& gameplay, const app::InputState& input, uint64_t frame_count, float delta_time) {
    const float fps = delta_time > 0.0f ? 1.0f / delta_time : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 210.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Prototype Debug");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(frame_count));
    ImGui::Text("Mouse: %.1f, %.1f", input.mouse.x, input.mouse.y);
    ImGui::Separator();
    ImGui::Text("W: %s", input.keys.w ? "down" : "up");
    ImGui::Text("A: %s", input.keys.a ? "down" : "up");
    ImGui::Text("S: %s", input.keys.s ? "down" : "up");
    ImGui::Text("D: %s", input.keys.d ? "down" : "up");
    ImGui::Separator();
    ImGui::Text("Player x: %.2f", gameplay.playerX());
    ImGui::Text("Player y: %.2f", gameplay.playerY());
    ImGui::End();
}

void DebugUi::render() {
    simgui_render();
}

bool DebugUi::handleEvent(const sapp_event* event) {
    return simgui_handle_event(event);
}

void DebugUi::cleanup() {
    simgui_shutdown();
}

} // namespace debug

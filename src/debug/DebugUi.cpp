#include "debug/DebugUi.h"

#include "app/InputState.h"
#include "app/SokolLog.h"
#include "gameplay/GameState.h"
#include "renderer/Renderer.h"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_imgui.h"

#include <cstdio>

namespace debug {
namespace {

enum class PanelIcon {
    Activity,
    Keyboard,
    Box,
    Sliders,
};

void applyShadcnLikeTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 10.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 7.0f;
    style.ChildRounding = 7.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 7.0f;
    style.ScrollbarRounding = 7.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.56f, 0.58f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.05f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.05f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.06f, 0.92f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.06f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.38f, 0.42f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.82f, 0.82f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.72f, 0.76f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.20f, 0.23f, 0.35f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.34f, 0.34f, 0.38f, 0.70f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.50f, 0.50f, 0.55f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.70f, 0.70f, 0.76f, 1.00f);
}

void line(ImDrawList* draw_list, ImVec2 origin, float scale, ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
    draw_list->AddLine(
        ImVec2(origin.x + a.x * scale, origin.y + a.y * scale),
        ImVec2(origin.x + b.x * scale, origin.y + b.y * scale),
        color,
        thickness);
}

void rect(ImDrawList* draw_list, ImVec2 origin, float scale, ImVec2 min, ImVec2 max, ImU32 color, float thickness, float rounding = 0.0f) {
    draw_list->AddRect(
        ImVec2(origin.x + min.x * scale, origin.y + min.y * scale),
        ImVec2(origin.x + max.x * scale, origin.y + max.y * scale),
        color,
        rounding,
        0,
        thickness);
}

void circle(ImDrawList* draw_list, ImVec2 origin, float scale, ImVec2 center, float radius, ImU32 color, float thickness) {
    draw_list->AddCircle(
        ImVec2(origin.x + center.x * scale, origin.y + center.y * scale),
        radius * scale,
        color,
        24,
        thickness);
}

void drawPanelIcon(PanelIcon icon, ImVec2 origin, float size, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float scale = size / 24.0f;
    const float thickness = 1.7f;

    switch (icon) {
        case PanelIcon::Activity:
            line(draw_list, origin, scale, ImVec2(3, 12), ImVec2(7, 12), color, thickness);
            line(draw_list, origin, scale, ImVec2(7, 12), ImVec2(10, 5), color, thickness);
            line(draw_list, origin, scale, ImVec2(10, 5), ImVec2(14, 19), color, thickness);
            line(draw_list, origin, scale, ImVec2(14, 19), ImVec2(17, 12), color, thickness);
            line(draw_list, origin, scale, ImVec2(17, 12), ImVec2(21, 12), color, thickness);
            break;
        case PanelIcon::Keyboard:
            rect(draw_list, origin, scale, ImVec2(3, 5), ImVec2(21, 19), color, thickness, 3.0f);
            for (int y = 9; y <= 13; y += 4) {
                for (int x = 7; x <= 17; x += 5) {
                    circle(draw_list, origin, scale, ImVec2(static_cast<float>(x), static_cast<float>(y)), 0.7f, color, thickness);
                }
            }
            line(draw_list, origin, scale, ImVec2(8, 16), ImVec2(16, 16), color, thickness);
            break;
        case PanelIcon::Box:
            line(draw_list, origin, scale, ImVec2(12, 3), ImVec2(20, 7.5f), color, thickness);
            line(draw_list, origin, scale, ImVec2(20, 7.5f), ImVec2(20, 16.5f), color, thickness);
            line(draw_list, origin, scale, ImVec2(20, 16.5f), ImVec2(12, 21), color, thickness);
            line(draw_list, origin, scale, ImVec2(12, 21), ImVec2(4, 16.5f), color, thickness);
            line(draw_list, origin, scale, ImVec2(4, 16.5f), ImVec2(4, 7.5f), color, thickness);
            line(draw_list, origin, scale, ImVec2(4, 7.5f), ImVec2(12, 3), color, thickness);
            line(draw_list, origin, scale, ImVec2(4.5f, 8), ImVec2(12, 12), color, thickness);
            line(draw_list, origin, scale, ImVec2(19.5f, 8), ImVec2(12, 12), color, thickness);
            line(draw_list, origin, scale, ImVec2(12, 12), ImVec2(12, 21), color, thickness);
            break;
        case PanelIcon::Sliders:
            line(draw_list, origin, scale, ImVec2(4, 7), ImVec2(20, 7), color, thickness);
            line(draw_list, origin, scale, ImVec2(4, 12), ImVec2(20, 12), color, thickness);
            line(draw_list, origin, scale, ImVec2(4, 17), ImVec2(20, 17), color, thickness);
            circle(draw_list, origin, scale, ImVec2(9, 7), 2.0f, color, thickness);
            circle(draw_list, origin, scale, ImVec2(15, 12), 2.0f, color, thickness);
            circle(draw_list, origin, scale, ImVec2(11, 17), 2.0f, color, thickness);
            break;
    }
}

void sectionHeader(PanelIcon icon, const char* label) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float icon_size = 16.0f;
    const ImU32 icon_color = ImGui::GetColorU32(ImGuiCol_Text);
    drawPanelIcon(icon, ImVec2(pos.x, pos.y + 1.0f), icon_size, icon_color);
    ImGui::Dummy(ImVec2(icon_size + 6.0f, icon_size));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0f);
    ImGui::TextUnformatted(label);
}

void drawKeyPill(const char* label, bool down) {
    const ImVec4 active_bg(0.92f, 0.92f, 0.94f, 1.0f);
    const ImVec4 active_text(0.04f, 0.04f, 0.05f, 1.0f);
    const ImVec4 idle_bg(0.10f, 0.10f, 0.12f, 1.0f);
    const ImVec4 idle_text(0.64f, 0.65f, 0.68f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, down ? active_bg : idle_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, down ? active_bg : idle_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, down ? active_bg : idle_bg);
    ImGui::PushStyleColor(ImGuiCol_Text, down ? active_text : idle_text);
    ImGui::Button(label, ImVec2(32.0f, 24.0f));
    ImGui::PopStyleColor(4);
}

void drawStatRow(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(104.0f);
    ImGui::TextUnformatted(value);
}

} // namespace

void DebugUi::init() {
    simgui_desc_t desc{};
    desc.logger.func = app::sokolLog;
    simgui_setup(&desc);
    applyShadcnLikeTheme();
}

void DebugUi::beginFrame(int width, int height, float delta_time, float dpi_scale) {
    simgui_frame_desc_t desc{};
    desc.width = width;
    desc.height = height;
    desc.delta_time = delta_time;
    desc.dpi_scale = dpi_scale;
    simgui_new_frame(&desc);
}

void DebugUi::draw(const gameplay::GameState& gameplay, const app::InputState& input, renderer::StyleSettings& style, uint64_t frame_count, float delta_time) {
    const float fps = delta_time > 0.0f ? 1.0f / delta_time : 0.0f;

    char frame_text[32]{};
    char fps_text[32]{};
    char mouse_text[48]{};
    char player_text[48]{};
    std::snprintf(frame_text, sizeof(frame_text), "%llu", static_cast<unsigned long long>(frame_count));
    std::snprintf(fps_text, sizeof(fps_text), "%.1f", fps);
    std::snprintf(mouse_text, sizeof(mouse_text), "%.0f, %.0f", input.mouse.x, input.mouse.y);
    std::snprintf(player_text, sizeof(player_text), "%.2f, %.2f", gameplay.playerX(), gameplay.playerZ());

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dev Panel");

    sectionHeader(PanelIcon::Activity, "Runtime");
    drawStatRow("FPS", fps_text);
    drawStatRow("Frame", frame_text);
    drawStatRow("Mouse", mouse_text);

    ImGui::Separator();

    sectionHeader(PanelIcon::Keyboard, "Input");
    drawKeyPill("W", input.keys.w);
    ImGui::SameLine();
    drawKeyPill("A", input.keys.a);
    ImGui::SameLine();
    drawKeyPill("S", input.keys.s);
    ImGui::SameLine();
    drawKeyPill("D", input.keys.d);

    ImGui::Separator();

    sectionHeader(PanelIcon::Box, "Player");
    drawStatRow("Position x,z", player_text);

    ImGui::Separator();

    sectionHeader(PanelIcon::Sliders, "Comic Shader");
    ImGui::SliderFloat("Bands", &style.toon_bands, 2.0f, 8.0f, "%.0f");
    ImGui::SliderFloat("Outline", &style.outline_thickness, 0.0f, 0.12f, "%.3f");
    ImGui::ColorEdit3("Ink", style.outline_color, ImGuiColorEditFlags_NoInputs);
    ImGui::Checkbox("Halftone", &style.halftone_enabled);
    ImGui::SliderFloat("Dot strength", &style.halftone_intensity, 0.0f, 0.6f);
    ImGui::SliderFloat("Dot scale", &style.halftone_scale, 3.0f, 24.0f);

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

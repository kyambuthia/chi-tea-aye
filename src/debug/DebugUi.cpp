#include "debug/DebugUi.h"

#include "app/InputState.h"
#include "gameplay/GameState.h"
#include "renderer/Renderer.h"
#include "ui/WeaponWheel.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <cmath>
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

void DebugUi::init(SDL_Window* window, SDL_GLContext context, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init(glsl_version);
    applyShadcnLikeTheme();
}

void DebugUi::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

namespace {

// A crosshair of four short arms around a centre point.
void drawCrosshair(ImVec2 centre) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    constexpr float kGap = 5.0f;
    constexpr float kLength = 9.0f;
    const ImU32 outline = IM_COL32(0, 0, 0, 170);
    const ImU32 fill = IM_COL32(240, 240, 240, 230);
    const ImVec2 arms[4][2] = {
        {ImVec2(centre.x - kGap - kLength, centre.y), ImVec2(centre.x - kGap, centre.y)},
        {ImVec2(centre.x + kGap, centre.y), ImVec2(centre.x + kGap + kLength, centre.y)},
        {ImVec2(centre.x, centre.y - kGap - kLength), ImVec2(centre.x, centre.y - kGap)},
        {ImVec2(centre.x, centre.y + kGap), ImVec2(centre.x, centre.y + kGap + kLength)},
    };
    for (const auto& arm : arms) {
        draw->AddLine(arm[0], arm[1], outline, 3.0f);
        draw->AddLine(arm[0], arm[1], fill, 1.5f);
    }
}

} // namespace

void DebugUi::draw(const gameplay::GameState& gameplay, const app::InputState& input, renderer::StyleSettings& style,
    const renderer::Renderer::Stats& stats, uint64_t frame_count, float delta_time, const char* played_clip, float clip_time,
    float clip_duration) {
    const float fps = delta_time > 0.0f ? 1.0f / delta_time : 0.0f;

    char frame_text[32]{};
    char fps_text[32]{};
    char mouse_text[48]{};
    char player_text[48]{};
    std::snprintf(frame_text, sizeof(frame_text), "%llu", static_cast<unsigned long long>(frame_count));
    std::snprintf(fps_text, sizeof(fps_text), "%.1f", fps);
    // Polled every frame, so the readout follows the pointer even without mouse events.
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    std::snprintf(mouse_text, sizeof(mouse_text), "%.0f, %.0f", mouse_x, mouse_y);
    std::snprintf(player_text, sizeof(player_text), "%.2f, %.2f", gameplay.playerX(), gameplay.playerZ());

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
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
    char clip_text[48]{};
    // Looping clips wrap, so show the time within the clip, not the time since it started.
    std::snprintf(clip_text, sizeof(clip_text), "%s  %.2f / %.2fs", played_clip, clip_time, clip_duration);
    drawStatRow("Clip", clip_text);
    char target_text[48]{};
    std::snprintf(target_text, sizeof(target_text), "%d hit / %d shots", gameplay.targets().hits(), gameplay.targets().shots());
    drawStatRow("Targets", target_text);
    if (gameplay.driving()) {
        const auto& car = gameplay.vehicles()[gameplay.drivenVehicle()];
        char car_text[64]{};
        std::snprintf(car_text, sizeof(car_text), "%.0f km/h, %d wheels down", std::fabs(car.forwardSpeed()) * 3.6f, car.groundedWheels());
        drawStatRow("Car", car_text);
    }
    ImGui::Checkbox("Show skeleton (F1)", &style.show_skeleton);
    ImGui::Checkbox("Auto-recenter camera", &auto_recenter_);

    ImGui::Separator();

    sectionHeader(PanelIcon::Sliders, "World");
    ImGui::SliderFloat("Time of day", &style.time_of_day, 0.0f, 24.0f, "%.1f h");
    ImGui::Checkbox("Clock runs", &style.advance_time);
    ImGui::SliderFloat("Fog", &style.fog_scale, 0.0f, 3.0f, "%.2fx");

    ImGui::Separator();

    sectionHeader(PanelIcon::Activity, "Performance");
    char scale_text[64]{};
    std::snprintf(scale_text, sizeof(scale_text), "%.0f%%  (%d x %d, %dx MSAA)", stats.render_scale * 100.0f, stats.scene_width,
        stats.scene_height, stats.samples);
    drawStatRow("3D resolution", stats.offscreen ? scale_text : "window (no offscreen target)");
    char boxes_text[48]{};
    std::snprintf(boxes_text, sizeof(boxes_text), "%d of %d", stats.visible_boxes, stats.total_boxes);
    drawStatRow("Boxes drawn", boxes_text);
    ImGui::Checkbox("Dynamic resolution", &style.dynamic_resolution);
    if (!style.dynamic_resolution) {
        ImGui::SliderFloat("Scale", &style.fixed_scale, 0.5f, 1.0f, "%.2f");
    }
    const char* msaa_names[] = {"Off", "2x", "4x"};
    int msaa_index = style.msaa_samples >= 4 ? 2 : (style.msaa_samples >= 2 ? 1 : 0);
    if (ImGui::Combo("MSAA", &msaa_index, msaa_names, 3)) {
        style.msaa_samples = msaa_index == 0 ? 0 : (msaa_index == 1 ? 2 : 4);
    }

    ImGui::Separator();

    sectionHeader(PanelIcon::Sliders, "Comic Shader");
    ImGui::SliderFloat("Bands", &style.toon_bands, 2.0f, 8.0f, "%.0f");
    ImGui::Checkbox("Halftone", &style.halftone_enabled);
    ImGui::SliderFloat("Dot strength", &style.halftone_intensity, 0.0f, 0.6f);
    ImGui::SliderFloat("Dot scale", &style.halftone_scale, 3.0f, 24.0f);

    ImGui::End();

    // A crosshair at the centre of the view while the rifle is ready and not aimed
    // down the scope (the scope has its own reticle).
    const gameplay::WeaponState& weapon = gameplay.weapon();
    if (weapon.active() == gameplay::WeaponSlot::Rifle && weapon.aim() < 0.5f && !gameplay.driving()) {
        drawCrosshair(ImGui::GetMainViewport()->GetCenter());
    }
}

void DebugUi::drawShotFeedback(const ShotFeedback& shot) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    constexpr float kFlashTime = 0.06f;   // muzzle flash
    constexpr float kTracerTime = 0.10f;  // tracer line
    constexpr float kSparkTime = 0.18f;   // impact spark
    constexpr float kMarkerTime = 0.22f;  // hit marker

    if (shot.muzzle_visible && shot.shot_age < kFlashTime) {
        const float fade = 1.0f - shot.shot_age / kFlashTime;
        const float radius = 4.0f + 12.0f * fade;
        draw->AddCircleFilled(ImVec2(shot.muzzle_x, shot.muzzle_y), radius, IM_COL32(255, 235, 160, static_cast<int>(240 * fade)), 16);
    }
    if (shot.muzzle_visible && shot.impact_visible && shot.shot_age < kTracerTime) {
        const float fade = 1.0f - shot.shot_age / kTracerTime;
        draw->AddLine(ImVec2(shot.muzzle_x, shot.muzzle_y), ImVec2(shot.impact_x, shot.impact_y),
            IM_COL32(255, 230, 150, static_cast<int>(200 * fade)), 2.0f);
    }
    if (shot.impact_visible && shot.shot_age < kSparkTime) {
        const float t = shot.shot_age / kSparkTime;
        const float reach = 4.0f + 14.0f * t;
        const ImU32 spark = IM_COL32(255, 210, 120, static_cast<int>(255 * (1.0f - t)));
        for (int k = 0; k < 6; ++k) {
            const float angle = 3.14159265f * k / 3.0f + 0.5f;
            draw->AddLine(ImVec2(shot.impact_x, shot.impact_y),
                ImVec2(shot.impact_x + reach * std::cos(angle), shot.impact_y + reach * std::sin(angle)), spark, 1.5f);
        }
    }
    if (shot.hit && shot.shot_age < kMarkerTime) {
        // Hit marker: a small X at the centre of the view.
        const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
        const float fade = 1.0f - shot.shot_age / kMarkerTime;
        const float size = 8.0f;
        const ImU32 marker = IM_COL32(255, 70, 60, static_cast<int>(255 * fade));
        draw->AddLine(ImVec2(centre.x - size, centre.y - size), ImVec2(centre.x + size, centre.y + size), marker, 2.5f);
        draw->AddLine(ImVec2(centre.x + size, centre.y - size), ImVec2(centre.x - size, centre.y + size), marker, 2.5f);
    }
}

void DebugUi::drawRiflePoints(const RiflePointsOverlay& p) {
    if (!p.visible) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    auto mark = [draw](bool ok, float x, float y, ImU32 colour, const char* label) {
        if (!ok) {
            return;
        }
        draw->AddCircleFilled(ImVec2(x, y), 6.0f, colour, 12);
        draw->AddCircle(ImVec2(x, y), 6.0f, IM_COL32(0, 0, 0, 220), 12, 1.5f);
        draw->AddText(ImVec2(x + 8.0f, y - 8.0f), colour, label);
    };
    if (p.grip_visible && p.fore_visible) {
        draw->AddLine(ImVec2(p.grip_x, p.grip_y), ImVec2(p.fore_x, p.fore_y), IM_COL32(255, 255, 255, 180), 1.5f);
    }
    if (p.grip_visible && p.muzzle_visible) {
        draw->AddLine(ImVec2(p.grip_x, p.grip_y), ImVec2(p.muzzle_x, p.muzzle_y), IM_COL32(255, 80, 80, 140), 1.5f);
    }
    mark(p.hand_visible, p.hand_x, p.hand_y, IM_COL32(255, 255, 255, 255), "hand");
    mark(p.grip_visible, p.grip_x, p.grip_y, IM_COL32(90, 230, 90, 255), "grip");
    mark(p.fore_visible, p.fore_x, p.fore_y, IM_COL32(90, 160, 255, 255), "fore");
    mark(p.muzzle_visible, p.muzzle_x, p.muzzle_y, IM_COL32(255, 90, 90, 255), "muzzle");
}

void DebugUi::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool DebugUi::handleEvent(const SDL_Event* event) {
    return ImGui_ImplSDL3_ProcessEvent(event);
}

bool DebugUi::wantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

void DebugUi::setMouseEnabled(bool enabled) {
    ImGuiIO& io = ImGui::GetIO();
    if (enabled) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
}

bool DebugUi::wantsKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void DebugUi::drawWeaponWheel(float cx, float cy, float radius, int hovered) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 centre(cx, cy);
    const char* labels[ui::kWeaponWheelSlots] = {"Rifle", "Pistol", "Fists"};
    const float kPi = 3.14159265358979323846f;
    constexpr int kArcSegments = 24;

    draw->AddCircleFilled(centre, radius, IM_COL32(18, 20, 26, 215), 96);
    for (int slot = 0; slot < ui::kWeaponWheelSlots; ++slot) {
        // Sector edges, in degrees clockwise from the top.
        const float start = slot * 120.0f - 60.0f;
        draw->PathClear();
        draw->PathLineTo(centre);
        for (int k = 0; k <= kArcSegments; ++k) {
            const float deg = start + 120.0f * k / kArcSegments;
            const float rad = deg * kPi / 180.0f;
            draw->PathLineTo(ImVec2(cx + radius * std::sin(rad), cy - radius * std::cos(rad)));
        }
        const ImU32 fill = slot == hovered ? IM_COL32(230, 150, 60, 235) : IM_COL32(60, 64, 76, 215);
        draw->PathFillConvex(fill);

        const float mid = (slot * 120.0f) * kPi / 180.0f;
        const ImVec2 at(cx + radius * 0.62f * std::sin(mid), cy - radius * 0.62f * std::cos(mid));
        const ImVec2 size = ImGui::CalcTextSize(labels[slot]);
        draw->AddText(ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), IM_COL32(245, 245, 245, 255), labels[slot]);
    }
    draw->AddCircle(centre, radius, IM_COL32(200, 205, 215, 180), 96, 2.0f);
}

bool DebugUi::autoRecenter() const {
    return auto_recenter_;
}

void DebugUi::cleanup() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

} // namespace debug

#include "ui/Hud.h"

#include "gameplay/GameState.h"
#include "physics/Vehicle.h"
#include "ui/HudLayout.h"
#include "world/City.h"
#include "world/World.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace ui {
namespace {

ImU32 rgba(int r, int g, int b, int a) {
    return IM_COL32(r, g, b, a);
}

// Text with a soft shadow, so it reads over bright and dark scenes alike.
void shadowText(ImDrawList* draw, ImFont* font, float size, ImVec2 at, ImU32 colour, const char* text) {
    draw->AddText(font, size, ImVec2(at.x + 1.5f, at.y + 1.5f), rgba(0, 0, 0, 170), text);
    draw->AddText(font, size, at, colour, text);
}

void centredText(ImDrawList* draw, float size, ImVec2 centre, ImU32 colour, const char* text) {
    ImFont* font = ImGui::GetFont();
    const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
    shadowText(draw, font, size, ImVec2(centre.x - 0.5f * extent.x, centre.y - 0.5f * extent.y), colour, text);
}

void quad(ImDrawList* draw, const RadarView& view, float x0, float z0, float x1, float z1, ImU32 colour) {
    ImVec2 p[4];
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {z0, z0, z1, z1};
    for (int i = 0; i < 4; ++i) view.toScreen(xs[i], zs[i], p[i].x, p[i].y);
    draw->AddQuadFilled(p[0], p[1], p[2], p[3], colour);
}

void arrow(ImDrawList* draw, ImVec2 c, float angle, float size, ImU32 fill) {
    // Points up at angle 0; angle grows clockwise on screen.
    auto rot = [&](float x, float y) {
        return ImVec2(c.x + x * std::cos(angle) - y * std::sin(angle), c.y + x * std::sin(angle) + y * std::cos(angle));
    };
    const ImVec2 tip = rot(0.0f, -size);
    const ImVec2 left = rot(-size * 0.7f, size * 0.75f);
    const ImVec2 notch = rot(0.0f, size * 0.35f);
    const ImVec2 right = rot(size * 0.7f, size * 0.75f);
    draw->AddTriangleFilled(tip, left, notch, fill);
    draw->AddTriangleFilled(tip, notch, right, fill);
    draw->AddLine(tip, left, rgba(0, 0, 0, 200), 1.2f);
    draw->AddLine(tip, right, rgba(0, 0, 0, 200), 1.2f);
}

void drawRadar(ImDrawList* draw, const gameplay::GameState& gameplay, const world::World& world, const HudContext& ctx) {
    const float unit = std::fmin(ctx.width, ctx.height);
    RadarView view;
    view.half_size = std::fmax(60.0f, std::fmin(110.0f, unit * 0.13f));
    view.screen_x = 18.0f + view.half_size;
    view.screen_y = ctx.height - 18.0f - view.half_size;
    view.world_x = gameplay.playerX();
    view.world_z = gameplay.playerZ();
    view.azimuth = gameplay.cameraAzimuth();
    // Zoom out with speed, as a driver needs to see further ahead.
    float speed = 0.0f;
    if (gameplay.driving()) speed = std::fabs(gameplay.vehicles()[gameplay.drivenVehicle()].forwardSpeed());
    view.metres_per_pixel = 0.7f + 0.03f * std::fmin(speed, 30.0f);

    const ImVec2 lo(view.screen_x - view.half_size, view.screen_y - view.half_size);
    const ImVec2 hi(view.screen_x + view.half_size, view.screen_y + view.half_size);
    draw->AddRectFilled(ImVec2(lo.x - 3, lo.y - 3), ImVec2(hi.x + 3, hi.y + 3), rgba(0, 0, 0, 150), 10.0f);
    draw->PushClipRect(lo, hi, true);
    draw->AddRectFilled(lo, hi, rgba(52, 56, 52, 215));

    using L = world::CityLayout;
    const float reach = view.reach() * 1.5f;  // the corners reach farther than the edge
    // Blocks: lot ground, then the roads as the gaps between them. The park is green.
    for (int bz = -L::kBlockRadius; bz <= L::kBlockRadius; ++bz) {
        for (int bx = -L::kBlockRadius; bx <= L::kBlockRadius; ++bx) {
            const float cx = bx * L::kBlockPitch;
            const float cz = bz * L::kBlockPitch;
            if (std::fabs(cx - view.world_x) > reach + L::kBlockHalf || std::fabs(cz - view.world_z) > reach + L::kBlockHalf) continue;
            const ImU32 ground = (bx == 0 && bz == 0) ? rgba(64, 96, 58, 235) : rgba(92, 94, 90, 235);
            quad(draw, view, cx - L::kBlockHalf, cz - L::kBlockHalf, cx + L::kBlockHalf, cz + L::kBlockHalf, ground);
        }
    }
    // Buildings, by footprint.
    std::vector<int> near;
    world.boxesNear(view.world_x - reach, view.world_z - reach, view.world_x + reach, view.world_z + reach, near);
    for (int i : near) {
        const world::Box& b = world.walls()[i];
        if (i < world.practiceWallCount()) {
            quad(draw, view, b.min[0], b.min[2], b.max[0], b.max[2], rgba(150, 140, 120, 255));
            continue;
        }
        quad(draw, view, b.min[0], b.min[2], b.max[0], b.max[2], rgba(128, 128, 124, 255));
    }
    // Other cars.
    for (int i = 0; i < static_cast<int>(gameplay.vehicles().size()); ++i) {
        if (i == gameplay.drivenVehicle()) continue;
        const physics::Vehicle& car = gameplay.vehicles()[i];
        float sx = 0.0f;
        float sy = 0.0f;
        view.toScreen(car.position().x, car.position().z, sx, sy);
        draw->AddCircleFilled(ImVec2(sx, sy), 3.2f, rgba(90, 170, 255, 255), 10);
    }
    // North marker at the edge, in the direction of +Z.
    {
        float nx = 0.0f;
        float ny = 0.0f;
        view.toScreen(view.world_x, view.world_z + 10000.0f, nx, ny);
        const float dx = nx - view.screen_x;
        const float dy = ny - view.screen_y;
        const float scale = (view.half_size - 10.0f) / std::fmax(std::fabs(dx), std::fabs(dy));
        centredText(draw, 15.0f, ImVec2(view.screen_x + dx * scale, view.screen_y + dy * scale), rgba(255, 255, 255, 230), "N");
    }
    // The player, always at the centre.
    arrow(draw, ImVec2(view.screen_x, view.screen_y), view.screenAngle(gameplay.playerYaw()), 8.0f, rgba(255, 255, 255, 255));
    draw->PopClipRect();
    draw->AddRect(lo, hi, rgba(10, 10, 10, 220), 6.0f, 0, 2.0f);
}

void drawSpeedometer(ImDrawList* draw, const gameplay::GameState& gameplay, const HudContext& ctx) {
    const physics::Vehicle& car = gameplay.vehicles()[gameplay.drivenVehicle()];
    const float kmh = std::fabs(car.forwardSpeed()) * 3.6f;
    char text[32];
    std::snprintf(text, sizeof(text), "%3.0f", kmh);
    ImFont* font = ImGui::GetFont();
    const float size = 46.0f;
    const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
    const ImVec2 at(ctx.width - 28.0f - extent.x - 50.0f, ctx.height - 28.0f - extent.y);
    shadowText(draw, font, size, at, rgba(255, 255, 255, 240), text);
    shadowText(draw, font, 18.0f, ImVec2(at.x + extent.x + 6.0f, at.y + extent.y - 22.0f), rgba(220, 220, 220, 220), "km/h");
    if (car.forwardSpeed() < -0.5f) {
        shadowText(draw, font, 18.0f, ImVec2(at.x, at.y - 20.0f), rgba(255, 200, 120, 230), "R");
    }
}

void drawPrompt(ImDrawList* draw, const char* text) {
    ImFont* font = ImGui::GetFont();
    const float size = 19.0f;
    const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
    const ImVec2 at(16.0f + 4.0f, 16.0f + 40.0f);
    draw->AddRectFilled(ImVec2(at.x - 10.0f, at.y - 8.0f), ImVec2(at.x + extent.x + 10.0f, at.y + extent.y + 8.0f), rgba(0, 0, 0, 165), 6.0f);
    draw->AddText(font, size, at, rgba(255, 255, 255, 245), text);
}

void drawTouchButtons(ImDrawList* draw, const gameplay::GameState& gameplay, const HudContext& ctx) {
    const TouchLayout layout = TouchLayout::make(ctx.width, ctx.height, gameplay.driving());
    for (int i = 0; i < TouchLayout::kCount; ++i) {
        const TouchButton& b = layout.buttons[i];
        if (!b.visible) continue;
        const bool down = (ctx.touch_pressed >> i) & 1u;
        draw->AddCircleFilled(ImVec2(b.x, b.y), b.radius, down ? rgba(255, 255, 255, 120) : rgba(20, 20, 24, 110), 32);
        draw->AddCircle(ImVec2(b.x, b.y), b.radius, rgba(255, 255, 255, 170), 32, 2.0f);
        centredText(draw, b.radius * 0.42f, ImVec2(b.x, b.y), rgba(255, 255, 255, 235), b.label);
    }
}

} // namespace

void Hud::draw(const gameplay::GameState& gameplay, const world::World& world, const HudContext& context) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    drawRadar(draw, gameplay, world, context);
    if (gameplay.driving()) {
        drawSpeedometer(draw, gameplay, context);
    }

    // Prompts: one line at the top left, under the dev panel's title bar.
    const char* use_key = context.touch ? "CAR" : (context.gamepad ? "Y" : "F");
    char text[96];
    if (!gameplay.driving() && gameplay.vehicleInReach() >= 0) {
        std::snprintf(text, sizeof(text), "Press %s to get in", use_key);
        drawPrompt(draw, text);
    } else if (context.show_capture_hint) {
        drawPrompt(draw, "Click to look around  -  Esc frees the mouse");
    }

    if (context.touch) {
        drawTouchButtons(draw, gameplay, context);
    }
}

} // namespace ui

#include "app/App.h"

#include "app/InputState.h"
#include "assets/AssetSystem.h"
#include "debug/DebugUi.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "renderer/GlFunctions.h"
#include "renderer/Renderer.h"
#include "ui/WeaponWheel.h"
#include "world/World.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace app {
namespace {

#if defined(CHI_GLES)
constexpr const char* kGlslVersion = "#version 300 es";
#else
constexpr const char* kGlslVersion = "#version 330";
#endif

// Touch stick: the left half of the screen acts as a virtual joystick.
constexpr float kStickDeadzone = 0.25f;
constexpr float kStickRadiusFraction = 0.12f;

class App {
public:
    SDL_AppResult init() {
        SDL_SetAppMetadata("chi-tea-aye", "0.1", "dev.chi-tea-aye.prototype");

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

#if defined(CHI_GLES)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window_ = SDL_CreateWindow("chi-tea-aye prototype", 1280, 720,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr) {
            SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }

        gl_context_ = SDL_GL_CreateContext(window_);
        if (gl_context_ == nullptr) {
            SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        SDL_GL_MakeCurrent(window_, gl_context_);
        SDL_GL_SetSwapInterval(1);

        if (!loadGlFunctions()) {
            SDL_Log("failed to load OpenGL functions");
            return SDL_APP_FAILURE;
        }

        if (!assets_.init()) {
            return SDL_APP_FAILURE;
        }
        world_.init();
        physics_.init();
        gameplay_.init();
        gameplay_.placeTargets(world_);
        renderer_.init(assets_.character(), world_, assets_.rifle(), assets_.pistol());
        debug_ui_.init(window_, gl_context_, kGlslVersion);

        last_ticks_ns_ = SDL_GetTicksNS();
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult iterate() {
        const uint64_t now_ns = SDL_GetTicksNS();
        // Clamp so a stall (window drag, backgrounding) does not teleport the player.
        const float dt = std::min(static_cast<float>(now_ns - last_ticks_ns_) / 1e9f, 0.1f);
        last_ticks_ns_ = now_ns;

        ++frame_count_;
        gameplay_.setAutoRecenter(debug_ui_.autoRecenter());
        gameplay_.update(input_, physics_, world_, dt);
        // Orbit and zoom are per-frame deltas; the drag state persists.
        input_.look_dx = 0.0f;
        input_.look_dy = 0.0f;
        input_.zoom_steps = 0.0f;
        input_.weapon_request = -1;
        input_.attack_pressed = false;
        input_.fire_pressed = false;
        input_.crouch_pressed = false;
        input_.inspect_pressed = false;
        input_.tapped = app::KeyState{};

        debug_ui_.beginFrame();
        debug_ui_.draw(gameplay_, input_, renderer_.styleSettings(), frame_count_, dt,
            renderer_.playedClip(), renderer_.clipTime(), renderer_.clipDuration());
        if (wheel_open_) {
            debug_ui_.drawWeaponWheel(windowCentreX(), windowCentreY(), kWheelRadius, wheelSelection());
        }
        debug_ui_.drawShotFeedback(shotFeedback());
        if (renderer_.styleSettings().show_rifle_points) {
            debug_ui_.drawRiflePoints(riflePointsOnWindow());
        }

#if defined(__EMSCRIPTEN__)
        // The page owns the canvas size on web; keep the window matched to it.
        double css_w = 0.0;
        double css_h = 0.0;
        if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS) {
            int win_w = 0;
            int win_h = 0;
            SDL_GetWindowSize(window_, &win_w, &win_h);
            if (win_w != static_cast<int>(css_w) || win_h != static_cast<int>(css_h)) {
                SDL_SetWindowSize(window_, static_cast<int>(css_w), static_cast<int>(css_h));
            }
        }
#endif

        int fb_width = 0;
        int fb_height = 0;
        SDL_GetWindowSizeInPixels(window_, &fb_width, &fb_height);
        renderer_.draw(gameplay_, fb_width, fb_height);
        debug_ui_.render();

        SDL_GL_SwapWindow(window_);
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult event(const SDL_Event* e) {
        (void)debug_ui_.handleEvent(e);

        switch (e->type) {
            case SDL_EVENT_QUIT:
                return SDL_APP_SUCCESS;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                // While the dev panel has the keyboard, keys go to it, not the game.
                // Key releases always pass through, so no key is left stuck down.
                if (e->type == SDL_EVENT_KEY_UP && e->key.scancode == SDL_SCANCODE_SPACE) {
                    input_.fire_held = false;
                }
                if (e->key.scancode == SDL_SCANCODE_Q) {
                    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && !debug_ui_.wantsKeyboard()) {
                        wheel_open_ = true;
                    }
                    if (e->type == SDL_EVENT_KEY_UP && wheel_open_) {
                        const int slot = wheelSelection();
                        if (slot >= 0) {
                            input_.weapon_request = slot;
                        }
                        wheel_open_ = false;
                    }
                }
                if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && !debug_ui_.wantsKeyboard()) {
                    if (e->key.scancode == SDL_SCANCODE_1) input_.weapon_request = 0;
                    if (e->key.scancode == SDL_SCANCODE_2) input_.weapon_request = 1;
                    if (e->key.scancode == SDL_SCANCODE_3) input_.weapon_request = 2;
                    if (e->key.scancode == SDL_SCANCODE_F) input_.attack_pressed = true;
                    if (e->key.scancode == SDL_SCANCODE_SPACE) {
                        input_.fire_pressed = true;
                        input_.fire_held = true;
                    }
                    if (e->key.scancode == SDL_SCANCODE_C) input_.crouch_pressed = true;
                    if (e->key.scancode == SDL_SCANCODE_W) input_.tapped.w = true;
                    if (e->key.scancode == SDL_SCANCODE_A) input_.tapped.a = true;
                    if (e->key.scancode == SDL_SCANCODE_S) input_.tapped.s = true;
                    if (e->key.scancode == SDL_SCANCODE_D) input_.tapped.d = true;
                }
                if (e->key.scancode == SDL_SCANCODE_F3 && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
                    input_.inspect_pressed = true;
                }
                if (e->key.scancode == SDL_SCANCODE_F2 && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
                    renderer_.styleSettings().show_rifle_points = !renderer_.styleSettings().show_rifle_points;
                }
                if (e->key.scancode == SDL_SCANCODE_F1 && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
                    renderer_.styleSettings().show_skeleton = !renderer_.styleSettings().show_skeleton;
                }
                if (e->key.scancode == SDL_SCANCODE_ESCAPE && e->type == SDL_EVENT_KEY_DOWN) {
                    return SDL_APP_SUCCESS;
                }
                if (e->type == SDL_EVENT_KEY_UP || !debug_ui_.wantsKeyboard()) {
                    setKey(e->key.scancode, e->type == SDL_EVENT_KEY_DOWN);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                input_.mouse.x = e->motion.x;
                input_.mouse.y = e->motion.y;
                // Touches also produce mouse events; the touch path handles those.
                // Drags over the dev panel, such as its sliders, must not move the camera.
                if (input_.orbit_drag && e->motion.which != SDL_TOUCH_MOUSEID && !debug_ui_.wantsMouse()) {
                    input_.look_dx += e->motion.xrel;
                    input_.look_dy += e->motion.yrel;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // Left fires (hold for automatic fire), middle drags the camera, right aims.
                if (e->button.button == SDL_BUTTON_LEFT && e->button.which != SDL_TOUCH_MOUSEID &&
                    !debug_ui_.wantsMouse()) {
                    input_.fire_pressed = true;
                    input_.fire_held = true;
                }
                if (e->button.button == SDL_BUTTON_MIDDLE && e->button.which != SDL_TOUCH_MOUSEID &&
                    !debug_ui_.wantsMouse()) {
                    input_.orbit_drag = true;
                }
                if (e->button.button == SDL_BUTTON_RIGHT && e->button.which != SDL_TOUCH_MOUSEID &&
                    !debug_ui_.wantsMouse()) {
                    input_.aim_held = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e->button.button == SDL_BUTTON_LEFT) {
                    input_.fire_held = false;
                }
                if (e->button.button == SDL_BUTTON_MIDDLE) {
                    input_.orbit_drag = false;
                }
                if (e->button.button == SDL_BUTTON_RIGHT) {
                    input_.aim_held = false;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (!debug_ui_.wantsMouse()) {
                    input_.zoom_steps += e->wheel.y;
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
                onFingerDown(e->tfinger);
                break;
            case SDL_EVENT_FINGER_MOTION:
                onFingerMotion(e->tfinger);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                onFingerUp(e->tfinger);
                break;
            default:
                break;
        }
        return SDL_APP_CONTINUE;
    }

    void quit() {
        if (window_ == nullptr) {
            return;
        }
        SDL_GL_MakeCurrent(window_, gl_context_);
        debug_ui_.cleanup();
        renderer_.cleanup();
        gameplay_.cleanup();
        physics_.cleanup();
        world_.cleanup();
        assets_.cleanup();

        SDL_GL_DestroyContext(gl_context_);
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        gl_context_ = nullptr;
        SDL_Quit();
    }

private:
    // The window centre is where the weapon wheel is drawn.
    float windowCentreX() const {
        int w = 0;
        SDL_GetWindowSize(window_, &w, nullptr);
        return 0.5f * static_cast<float>(w);
    }
    float windowCentreY() const {
        int h = 0;
        SDL_GetWindowSize(window_, nullptr, &h);
        return 0.5f * static_cast<float>(h);
    }

    // Screen position of a world point, in window points. False when it is behind the camera.
    bool windowPoint(const float p[3], float& x, float& y) const {
        float ndc_x = 0.0f;
        float ndc_y = 0.0f;
        if (!renderer_.projectPoint(p, ndc_x, ndc_y)) {
            return false;
        }
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(w);
        y = (0.5f - ndc_y * 0.5f) * static_cast<float>(h);
        return true;
    }

    // The rifle's hand, grip, fore-end and muzzle, positioned on the window (the overlay).
    debug::RiflePointsOverlay riflePointsOnWindow() const {
        debug::RiflePointsOverlay overlay;
        renderer::Renderer::RiflePoints points{};
        overlay.visible = renderer_.riflePoints(points);
        if (!overlay.visible) {
            return overlay;
        }
        overlay.hand_visible = windowPoint(points.hand, overlay.hand_x, overlay.hand_y);
        overlay.grip_visible = windowPoint(points.grip, overlay.grip_x, overlay.grip_y);
        overlay.fore_visible = windowPoint(points.fore, overlay.fore_x, overlay.fore_y);
        overlay.muzzle_visible = windowPoint(points.muzzle, overlay.muzzle_x, overlay.muzzle_y);
        return overlay;
    }

    // The shot's muzzle flash, tracer and impact, positioned on the window.
    debug::ShotFeedback shotFeedback() const {
        debug::ShotFeedback feedback;
        const gameplay::GameState::ShotEvent& shot = gameplay_.lastShot();
        feedback.shot_age = shot.age;
        feedback.hit = shot.hit;
        float muzzle[3];
        feedback.muzzle_visible = renderer_.muzzlePoint(muzzle) && windowPoint(muzzle, feedback.muzzle_x, feedback.muzzle_y);
        feedback.impact_visible = windowPoint(shot.impact, feedback.impact_x, feedback.impact_y);
        return feedback;
    }

    // The weapon under the pointer, or -1 in the wheel's dead zone.
    int wheelSelection() const {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        return ui::weaponWheelSlot(mouse_x, mouse_y, windowCentreX(), windowCentreY(), kWheelDeadZone);
    }

    void setKey(SDL_Scancode key, bool down) {
        switch (key) {
            case SDL_SCANCODE_W:
                input_.keys.w = down;
                break;
            case SDL_SCANCODE_A:
                input_.keys.a = down;
                break;
            case SDL_SCANCODE_S:
                input_.keys.s = down;
                break;
            case SDL_SCANCODE_D:
                input_.keys.d = down;
                break;
            case SDL_SCANCODE_LSHIFT:
            case SDL_SCANCODE_RSHIFT:
                input_.keys.shift = down;
                break;
            default:
                break;
        }
    }

    // Finger coordinates are normalized to the window, so convert them to points.
    void fingerPoint(const SDL_TouchFingerEvent& f, float& x, float& y) const {
        int w = 1;
        int h = 1;
        SDL_GetWindowSize(window_, &w, &h);
        x = f.x * static_cast<float>(w);
        y = f.y * static_cast<float>(h);
    }

    void onFingerDown(const SDL_TouchFingerEvent& f) {
        // Right half drags the camera; left half is the movement stick.
        if (f.x >= 0.5f) {
            if (!camera_active_) {
                camera_active_ = true;
                camera_finger_ = f.fingerID;
            }
            return;
        }
        if (stick_active_) {
            return;
        }
        stick_active_ = true;
        stick_finger_ = f.fingerID;
        fingerPoint(f, stick_origin_x_, stick_origin_y_);
    }

    void onFingerMotion(const SDL_TouchFingerEvent& f) {
        if (camera_active_ && f.fingerID == camera_finger_) {
            int w = 1;
            int h = 1;
            SDL_GetWindowSize(window_, &w, &h);
            input_.look_dx += f.dx * static_cast<float>(w);
            input_.look_dy += f.dy * static_cast<float>(h);
            return;
        }
        if (!stick_active_ || f.fingerID != stick_finger_) {
            return;
        }
        int w = 1;
        int h = 1;
        SDL_GetWindowSize(window_, &w, &h);
        const float radius = kStickRadiusFraction * static_cast<float>(std::min(w, h));
        float x = 0.0f;
        float y = 0.0f;
        fingerPoint(f, x, y);
        const float dx = (x - stick_origin_x_) / radius;
        const float dy = (y - stick_origin_y_) / radius;

        // Screen y grows downward, so pushing the stick up is negative dy.
        input_.keys.a = dx < -kStickDeadzone;
        input_.keys.d = dx > kStickDeadzone;
        input_.keys.w = dy < -kStickDeadzone;
        input_.keys.s = dy > kStickDeadzone;
    }

    void onFingerUp(const SDL_TouchFingerEvent& f) {
        if (camera_active_ && f.fingerID == camera_finger_) {
            camera_active_ = false;
            return;
        }
        if (!stick_active_ || f.fingerID != stick_finger_) {
            return;
        }
        stick_active_ = false;
        input_.keys.a = false;
        input_.keys.d = false;
        input_.keys.w = false;
        input_.keys.s = false;
    }

    static constexpr float kWheelRadius = 150.0f;   // pixels
    static constexpr float kWheelDeadZone = 40.0f;  // pixels

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    InputState input_;
    assets::AssetSystem assets_;
    world::World world_;
    physics::Physics physics_;
    gameplay::GameState gameplay_;
    renderer::Renderer renderer_;
    debug::DebugUi debug_ui_;
    uint64_t frame_count_ = 0;
    uint64_t last_ticks_ns_ = 0;
    // Holding Q opens the weapon wheel; releasing it over a sector picks that weapon.
    bool wheel_open_ = false;
    bool stick_active_ = false;
    bool camera_active_ = false;
    SDL_FingerID camera_finger_ = 0;
    SDL_FingerID stick_finger_ = 0;
    float stick_origin_x_ = 0.0f;
    float stick_origin_y_ = 0.0f;
};

std::unique_ptr<App> g_app;

} // namespace

SDL_AppResult init() {
    g_app = std::make_unique<App>();
    return g_app->init();
}

SDL_AppResult iterate() {
    return g_app->iterate();
}

SDL_AppResult event(const SDL_Event* event) {
    return g_app->event(event);
}

void quit() {
    if (g_app) {
        g_app->quit();
        g_app.reset();
    }
}

} // namespace app

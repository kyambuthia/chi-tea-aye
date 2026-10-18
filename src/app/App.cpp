#include "app/App.h"

#include "app/InputState.h"
#include "assets/AssetSystem.h"
#include "debug/DebugUi.h"
#include "gameplay/GameState.h"
#include "physics/Physics.h"
#include "renderer/GlFunctions.h"
#include "renderer/Renderer.h"
#include "ui/Hud.h"
#include "ui/HudLayout.h"
#include "ui/WeaponWheel.h"
#include "world/World.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#if defined(__EMSCRIPTEN__)
// A page URL option as a number, e.g. ?msaa=0, or -1 when it is missing.
EM_JS(double, chi_page_option, (const char* name), {
    var v = new URLSearchParams(window.location.search).get(UTF8ToString(name));
    return v === null || isNaN(parseFloat(v)) ? -1 : parseFloat(v);
});
// SDL 3.2's web backend marks relative mode enabled before pointer lock succeeds,
// and does not catch promise rejections. Let the browser own capture on web;
// SDL still delivers movementX/Y as relative motion while the canvas is locked.
EM_JS(void, chi_set_pointer_captured, (int captured), {
    var canvas = Module['canvas'];
    var state = Module['chiPointerCapture'] || (Module['chiPointerCapture'] = {pending: false, wanted: false});
    state.wanted = !!captured;
    if (!captured) {
        if (document.pointerLockElement === canvas) document.exitPointerLock();
        return;
    }
    if (state.pending || document.pointerLockElement === canvas) return;
    if (!canvas.isConnected || !document.hasFocus() || !canvas.requestPointerLock) {
        state.wanted = false;
        return;
    }
    state.pending = true;
    var done = false;
    var finish = function() {
        if (done) return;
        done = true;
        state.pending = false;
        document.removeEventListener('pointerlockchange', finish);
        document.removeEventListener('pointerlockerror', finish);
        // A focus loss or Escape may cancel a request before it completes.
        if (!state.wanted && document.pointerLockElement === canvas) document.exitPointerLock();
    };
    document.addEventListener('pointerlockchange', finish);
    document.addEventListener('pointerlockerror', finish);
    try {
        var request = canvas.requestPointerLock();
        // Older browsers report completion only through the events above.
        if (request && request.catch) request.catch(finish);
    } catch (error) {
        finish();
    }
});
EM_JS(int, chi_pointer_captured, (), {
    var state = Module['chiPointerCapture'];
    // Stop consuming look input immediately when releasing an asynchronous lock.
    return !!(state && state.wanted && document.pointerLockElement === Module['canvas']);
});
// Publishes the game's numbers on window.chiStats for browser tests and remote debugging.
EM_JS(void, chi_publish_stats, (double fps, double scale, int width, int height, int samples, int visible, int total,
    int driving, double speed, double x, double z, int offscreen, int reach, double car_distance), {
    window.chiStats = {fps: fps, scale: scale, width: width, height: height, samples: samples, visibleBoxes: visible,
        totalBoxes: total, driving: !!driving, speed: speed, x: x, z: z, offscreen: !!offscreen, carInReach: reach,
        nearestCar: car_distance};
});
#endif

namespace app {
namespace {

#if defined(CHI_GLES)
constexpr const char* kGlslVersion = "#version 300 es";
#else
constexpr const char* kGlslVersion = "#version 330";
#endif

// Touch stick: the left half of the screen acts as a virtual joystick.
constexpr float kStickDeadzone = 0.18f;
constexpr float kStickRadiusFraction = 0.12f;

// Gamepad sticks and triggers.
constexpr float kPadStickDeadzone = 0.2f;
constexpr float kPadTriggerOn = 0.3f;          // a trigger past this counts as pressed
constexpr float kPadLookPixelsPerSecond = 520.0f;  // right stick at full tilt, in orbit pixels

class App {
public:
    SDL_AppResult init() {
        SDL_SetAppMetadata("chi-tea-aye", "0.1", "dev.chi-tea-aye.prototype");

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
        if (assets_.ybot() == nullptr) {
            SDL_Log("failed to load the Y-Bot player");
            return SDL_APP_FAILURE;
        }
        world_.init();
        physics_.init(world_);
        gameplay_.init();
        if (!gameplay_.spawnWarrior(physics_, world_, assets_.character())) {
            SDL_Log("failed to create warrior NPC ragdoll");
            return SDL_APP_FAILURE;
        }
        if (world_.cityEnabled()) gameplay_.placeTargets(world_);
        gameplay_.spawnVehicles(world_);
        renderer_.init(assets_.character(), assets_.ybot(), world_, assets_.rifle(), assets_.pistol());
        debug_ui_.init(window_, gl_context_, kGlslVersion);
        applyPageOptions();

        last_ticks_ns_ = SDL_GetTicksNS();
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult iterate() {
        const uint64_t now_ns = SDL_GetTicksNS();
        const float raw_dt = static_cast<float>(now_ns - last_ticks_ns_) / 1e9f;
        // Clamp so a stall (window drag, backgrounding) does not teleport the player.
        const float dt = std::min(raw_dt, 0.1f);
        last_ticks_ns_ = now_ns;

        ++frame_count_;
        const bool captured = pointerCaptured();
        if (was_pointer_captured_ && !captured) {
            releaseHeldInput();
            input_.look_dx = input_.look_dy = 0.0f;
        }
        was_pointer_captured_ = captured;
        pollGamepad(dt);
        combineAnalogInput();
        debug_ui_.setMouseEnabled(!pointerCaptured());

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
        input_.ragdoll_pressed = false;
        input_.use_pressed = false;
        input_.tapped = app::KeyState{};

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
        // The 3D scene first; the UI goes on top at full resolution.
        renderer_.draw(gameplay_, fb_width, fb_height, raw_dt);

        debug_ui_.beginFrame();
        ui::HudContext hud;
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window_, &win_w, &win_h);
        hud.width = static_cast<float>(win_w);
        hud.height = static_cast<float>(win_h);
        hud.touch = touch_seen_;
        hud.touch_pressed = touch_buttons_down_;
        hud.pointer_captured = pointerCaptured();
        hud.gamepad = gamepad_last_used_;
        hud.show_capture_hint = !touch_seen_ && !gamepad_last_used_ && !pointerCaptured() && !gameplay_.driving();
        hud_.draw(gameplay_, world_, hud);
        debug_ui_.draw(gameplay_, input_, renderer_.styleSettings(), renderer_.stats(), frame_count_, dt,
            renderer_.playedClip(), renderer_.clipTime(), renderer_.clipDuration());
        if (wheel_open_) {
            debug_ui_.drawWeaponWheel(windowCentreX(), windowCentreY(), kWheelRadius, wheelSelection());
        }
        debug_ui_.drawShotFeedback(shotFeedback());
        if (renderer_.styleSettings().show_rifle_points) {
            debug_ui_.drawRiflePoints(riflePointsOnWindow());
        }
        debug_ui_.render();
        publishStats(raw_dt);

        SDL_GL_SwapWindow(window_);
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult event(const SDL_Event* e) {
        // While the mouse is captured for looking, the dev panel does not see it.
        const bool mouse_event = e->type == SDL_EVENT_MOUSE_MOTION || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            e->type == SDL_EVENT_MOUSE_BUTTON_UP || e->type == SDL_EVENT_MOUSE_WHEEL;
        if (!(mouse_event && pointerCaptured())) {
            (void)debug_ui_.handleEvent(e);
        }
        const bool panel_has_mouse = !pointerCaptured() && debug_ui_.wantsMouse();

        switch (e->type) {
            case SDL_EVENT_QUIT:
                return SDL_APP_SUCCESS;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Key releases never arrive for a window without focus: drop every held input,
                // or the warrior keeps walking (and the rifle keeps firing) after an alt-tab.
                releaseHeldInput();
                setPointerCaptured(false);
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                gamepad_last_used_ = false;
                if (onKey(e->key, e->type == SDL_EVENT_KEY_DOWN) == SDL_APP_SUCCESS) {
                    return SDL_APP_SUCCESS;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (e->motion.which == SDL_TOUCH_MOUSEID) {
                    break;  // touches also produce mouse events; the touch path handles those
                }
                gamepad_last_used_ = false;
                input_.mouse.x = e->motion.x;
                input_.mouse.y = e->motion.y;
                if (wheel_open_) {
                    moveWheelCursor(e->motion);
                } else if (pointerCaptured()) {
                    input_.look_dx += e->motion.xrel;
                    input_.look_dy += e->motion.yrel;
                } else if (input_.orbit_drag && !panel_has_mouse) {
                    // Drags over the dev panel, such as its sliders, must not move the camera.
                    input_.look_dx += e->motion.xrel;
                    input_.look_dy += e->motion.yrel;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e->button.which == SDL_TOUCH_MOUSEID || panel_has_mouse) {
                    break;
                }
                gamepad_last_used_ = false;
                // The first click captures the mouse for looking around; it does not fire.
                if (!pointerCaptured() && (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_RIGHT)) {
                    setPointerCaptured(true);
                    break;
                }
                // Left fires (hold for automatic fire), middle drags the camera, right aims.
                if (e->button.button == SDL_BUTTON_LEFT) {
                    input_.fire_pressed = true;
                    mouse_fire_ = true;
                }
                if (e->button.button == SDL_BUTTON_MIDDLE) {
                    input_.orbit_drag = true;
                }
                if (e->button.button == SDL_BUTTON_RIGHT) {
                    mouse_aim_ = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e->button.button == SDL_BUTTON_LEFT) {
                    mouse_fire_ = false;
                }
                if (e->button.button == SDL_BUTTON_MIDDLE) {
                    input_.orbit_drag = false;
                }
                if (e->button.button == SDL_BUTTON_RIGHT) {
                    mouse_aim_ = false;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (!panel_has_mouse) {
                    input_.zoom_steps += e->wheel.y;
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
                touch_seen_ = true;
                gamepad_last_used_ = false;
                onFingerDown(e->tfinger);
                break;
            case SDL_EVENT_FINGER_MOTION:
                onFingerMotion(e->tfinger);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                onFingerUp(e->tfinger);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (gamepad_ == nullptr) {
                    gamepad_ = SDL_OpenGamepad(e->gdevice.which);
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (gamepad_ != nullptr && SDL_GetGamepadID(gamepad_) == e->gdevice.which) {
                    SDL_CloseGamepad(gamepad_);
                    gamepad_ = nullptr;
                    releaseHeldInput();
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                onGamepadButton(e->gbutton);
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
        if (gamepad_ != nullptr) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
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
    // Web: settings from the page URL, e.g. ?msaa=0&dynres=0&scale=0.75&time=22, so a
    // weak device (or a test) can pick them without the dev panel. A missing option is -1.
    void applyPageOptions() {
#if defined(__EMSCRIPTEN__)
        auto option = [](const char* name) { return chi_page_option(name); };
        renderer::StyleSettings& style = renderer_.styleSettings();
        if (const double v = option("msaa"); v >= 0.0) style.msaa_samples = static_cast<int>(v);
        if (const double v = option("dynres"); v >= 0.0) style.dynamic_resolution = v > 0.0;
        if (const double v = option("scale"); v > 0.0) style.fixed_scale = std::clamp(static_cast<float>(v), 0.5f, 1.0f);
        if (const double v = option("time"); v >= 0.0) style.time_of_day = static_cast<float>(v);
        if (const double v = option("fog"); v >= 0.0) style.fog_scale = static_cast<float>(v);
#endif
    }

    // Web: a few numbers on window.chiStats for browser tests and remote debugging.
    void publishStats(float frame_seconds) {
        stats_time_ += frame_seconds;
        ++stats_frames_;
        if (stats_time_ < 0.5f) {
            return;
        }
#if defined(__EMSCRIPTEN__)
        const renderer::Renderer::Stats& s = renderer_.stats();
        const float speed = gameplay_.driving() ? std::fabs(gameplay_.vehicles()[gameplay_.drivenVehicle()].forwardSpeed()) : 0.0f;
        double nearest = 1e9;
        for (const physics::Vehicle& car : gameplay_.vehicles()) {
            nearest = std::min(nearest, static_cast<double>(std::hypot(car.position().x - gameplay_.playerX(), car.position().z - gameplay_.playerZ())));
        }
        chi_publish_stats(stats_frames_ / stats_time_, s.render_scale, s.scene_width, s.scene_height, s.samples, s.visible_boxes,
            s.total_boxes, gameplay_.driving() ? 1 : 0, speed, gameplay_.playerX(), gameplay_.playerZ(), s.offscreen ? 1 : 0,
            gameplay_.vehicleInReach(), nearest);
#endif
        stats_time_ = 0.0f;
        stats_frames_ = 0;
    }

    SDL_AppResult onKey(const SDL_KeyboardEvent& key, bool down) {
        // While the dev panel has the keyboard, keys go to it, not the game.
        // Key releases always pass through, so no key is left stuck down.
        const bool game_keys = down && !key.repeat && !debug_ui_.wantsKeyboard();
        if (!down && key.scancode == SDL_SCANCODE_SPACE) {
            space_held_ = false;
        }
        if (key.scancode == SDL_SCANCODE_Q) {
            if (game_keys) {
                wheel_open_ = true;
                wheel_cursor_x_ = windowCentreX();
                wheel_cursor_y_ = windowCentreY();
                if (!pointerCaptured()) {
                    SDL_GetMouseState(&wheel_cursor_x_, &wheel_cursor_y_);
                }
            }
            if (!down && wheel_open_) {
                const int slot = wheelSelection();
                if (slot >= 0) {
                    input_.weapon_request = slot;
                }
                wheel_open_ = false;
            }
        }
        if (game_keys) {
            switch (key.scancode) {
                case SDL_SCANCODE_1: input_.weapon_request = 0; break;
                case SDL_SCANCODE_2: input_.weapon_request = 1; break;
                case SDL_SCANCODE_3: input_.weapon_request = 2; break;
                case SDL_SCANCODE_F: input_.use_pressed = true; break;  // car in reach: get in; otherwise punch
                case SDL_SCANCODE_E: input_.use_pressed = true; break;
                case SDL_SCANCODE_R: input_.ragdoll_pressed = true; break;
                case SDL_SCANCODE_C: input_.crouch_pressed = true; break;
                case SDL_SCANCODE_W: input_.tapped.w = true; break;
                case SDL_SCANCODE_A: input_.tapped.a = true; break;
                case SDL_SCANCODE_S: input_.tapped.s = true; break;
                case SDL_SCANCODE_D: input_.tapped.d = true; break;
                case SDL_SCANCODE_SPACE:
                    // On foot Space fires; in a car it is the handbrake.
                    input_.fire_pressed = true;
                    space_held_ = true;
                    break;
                default: break;
            }
        }
        if (down && !key.repeat) {
            if (key.scancode == SDL_SCANCODE_F3) input_.inspect_pressed = true;
            if (key.scancode == SDL_SCANCODE_F2) {
                renderer_.styleSettings().show_rifle_points = !renderer_.styleSettings().show_rifle_points;
            }
            if (key.scancode == SDL_SCANCODE_F1) {
                renderer_.styleSettings().show_skeleton = !renderer_.styleSettings().show_skeleton;
            }
        }
        if (key.scancode == SDL_SCANCODE_ESCAPE && down) {
            // Esc frees a captured mouse first. On desktop a second Esc quits; a web page
            // never quits itself (the tab would be left with a dead canvas).
#if defined(__EMSCRIPTEN__)
            // Also cancel a browser request which has not completed yet.
            setPointerCaptured(false);
#else
            if (pointerCaptured()) {
                setPointerCaptured(false);
            } else {
                return SDL_APP_SUCCESS;
            }
#endif
        }
        if (!down || !debug_ui_.wantsKeyboard()) {
            setKey(key.scancode, down);
        }
        return SDL_APP_CONTINUE;
    }

    // The mouse is captured (pointer lock on web, relative mode on desktop).
    bool pointerCaptured() const {
        if (window_ == nullptr) return false;
#if defined(__EMSCRIPTEN__)
        // The browser drops pointer lock on Esc without telling the game's key handler.
        return chi_pointer_captured() != 0;
#else
        return SDL_GetWindowRelativeMouseMode(window_);
#endif
    }

    void setPointerCaptured(bool captured) {
#if defined(__EMSCRIPTEN__)
        chi_set_pointer_captured(captured);
#else
        if (window_ != nullptr) {
            SDL_SetWindowRelativeMouseMode(window_, captured);
        }
#endif
        if (!captured) {
            mouse_fire_ = false;
            mouse_aim_ = false;
            input_.orbit_drag = false;
            input_.look_dx = input_.look_dy = 0.0f;
        }
    }

    void releaseHeldInput() {
        input_.keys = KeyState{};
        input_.orbit_drag = false;
        mouse_fire_ = false;
        mouse_aim_ = false;
        space_held_ = false;
        pad_ = PadState{};
        touch_aim_ = false;
        touch_fire_ = false;
        touch_handbrake_ = false;
        touch_buttons_down_ = 0;
        stick_active_ = false;
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
        wheel_open_ = false;
    }

    // Sticks and triggers are read every frame; buttons arrive as events.
    void pollGamepad(float dt) {
        if (gamepad_ == nullptr) {
            pad_.move_x = pad_.move_y = pad_.throttle = pad_.brake = 0.0f;
            return;
        }
        auto axis = [this](SDL_GamepadAxis a) { return static_cast<float>(SDL_GetGamepadAxis(gamepad_, a)) / 32767.0f; };
        float lx = axis(SDL_GAMEPAD_AXIS_LEFTX);
        float ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
        float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX);
        float ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
        ui::stickDeadzone(lx, ly, kPadStickDeadzone);
        ui::stickDeadzone(rx, ry, kPadStickDeadzone);
        const float lt = std::max(0.0f, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        const float rt = std::max(0.0f, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (lx != 0.0f || ly != 0.0f || rx != 0.0f || ry != 0.0f || lt > kPadTriggerOn || rt > kPadTriggerOn) {
            gamepad_last_used_ = true;
        }
        pad_.move_x = lx;
        pad_.move_y = -ly;  // stick up is negative
        // A squared response keeps small corrections precise and full tilt fast.
        input_.look_dx += rx * std::fabs(rx) * kPadLookPixelsPerSecond * dt;
        input_.look_dy += ry * std::fabs(ry) * kPadLookPixelsPerSecond * 0.7f * dt;

        if (gameplay_.driving()) {
            pad_.throttle = rt;
            pad_.brake = lt;
            pad_.fire = false;
            pad_.aim = false;
        } else {
            pad_.throttle = pad_.brake = 0.0f;
            const bool fire = rt > kPadTriggerOn;
            if (fire && !pad_.fire) input_.fire_pressed = true;
            pad_.fire = fire;
            pad_.aim = lt > kPadTriggerOn;
        }
    }

    void onGamepadButton(const SDL_GamepadButtonEvent& b) {
        const bool down = b.down;
        gamepad_last_used_ = true;
        switch (static_cast<SDL_GamepadButton>(b.button)) {
            case SDL_GAMEPAD_BUTTON_SOUTH:  // A: sprint on foot, handbrake in a car
                pad_.run = down;
                pad_.handbrake = down;
                break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                pad_.handbrake = down;
                break;
            case SDL_GAMEPAD_BUTTON_NORTH:  // Y: get in or out
                if (down) input_.use_pressed = true;
                break;
            case SDL_GAMEPAD_BUTTON_EAST:   // B: punch
                if (down) input_.attack_pressed = true;
                break;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            case SDL_GAMEPAD_BUTTON_WEST:
                if (down) input_.crouch_pressed = true;
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                if (down) cycleWeapon(1);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                if (down) cycleWeapon(-1);
                break;
            default:
                break;
        }
    }

    void cycleWeapon(int step) {
        const int current = static_cast<int>(gameplay_.weapon().target());
        input_.weapon_request = (current + step + 3) % 3;
    }

    // Gamepad and touch analog values join the keyboard in the input state.
    void combineAnalogInput() {
        input_.move_x = std::clamp(pad_.move_x + stick_x_, -1.0f, 1.0f);
        input_.move_y = std::clamp(pad_.move_y + stick_y_, -1.0f, 1.0f);
        input_.run_held = pad_.run;
        float throttle = pad_.throttle;
        float brake = pad_.brake;
        if (gameplay_.driving()) {
            // Touch driving: the stick's forward push is the throttle, pulling back brakes.
            // A gamepad's stick only steers; its triggers are the pedals.
            throttle = std::max(throttle, std::max(0.0f, stick_y_));
            brake = std::max(brake, std::max(0.0f, -stick_y_));
            input_.move_y = 0.0f;
        }
        input_.throttle = throttle;
        input_.brake = brake;
        // A held action is held if any device holds it.
        input_.fire_held = mouse_fire_ || space_held_ || pad_.fire || touch_fire_;
        input_.aim_held = mouse_aim_ || pad_.aim || touch_aim_;
        input_.handbrake_held = space_held_ || pad_.handbrake || touch_handbrake_;
    }

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
        return feedback;
    }

    // While the wheel is open, the mouse moves a cursor over it instead of the camera.
    void moveWheelCursor(const SDL_MouseMotionEvent& motion) {
        if (pointerCaptured()) {
            wheel_cursor_x_ += motion.xrel;
            wheel_cursor_y_ += motion.yrel;
            // Keep the cursor near the wheel, so a big swipe still picks a sector.
            const float dx = wheel_cursor_x_ - windowCentreX();
            const float dy = wheel_cursor_y_ - windowCentreY();
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > kWheelRadius) {
                wheel_cursor_x_ = windowCentreX() + dx / d * kWheelRadius;
                wheel_cursor_y_ = windowCentreY() + dy / d * kWheelRadius;
            }
        } else {
            wheel_cursor_x_ = motion.x;
            wheel_cursor_y_ = motion.y;
        }
    }

    // The weapon under the wheel cursor, or -1 in the wheel's dead zone.
    int wheelSelection() const {
        return ui::weaponWheelSlot(wheel_cursor_x_, wheel_cursor_y_, windowCentreX(), windowCentreY(), kWheelDeadZone);
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

    ui::TouchLayout touchLayout() const {
        int w = 1;
        int h = 1;
        SDL_GetWindowSize(window_, &w, &h);
        return ui::TouchLayout::make(static_cast<float>(w), static_cast<float>(h), gameplay_.driving());
    }

    void pressTouchButton(int button, bool down) {
        const unsigned bit = 1u << button;
        touch_buttons_down_ = down ? (touch_buttons_down_ | bit) : (touch_buttons_down_ & ~bit);
        switch (button) {
            case ui::TouchLayout::kFire:
                touch_fire_ = down;
                if (down) input_.fire_pressed = true;
                break;
            case ui::TouchLayout::kAim:
                if (down) touch_aim_ = !touch_aim_;  // a toggle: holding two thumbs down is awkward
                break;
            case ui::TouchLayout::kUse:
                if (down) input_.use_pressed = true;
                break;
            case ui::TouchLayout::kCrouch:
                if (down) input_.crouch_pressed = true;
                break;
            case ui::TouchLayout::kHandbrake:
                touch_handbrake_ = down;
                break;
            case ui::TouchLayout::kWeapon:
                if (down) cycleWeapon(1);
                break;
            default:
                break;
        }
    }

    void onFingerDown(const SDL_TouchFingerEvent& f) {
        float x = 0.0f;
        float y = 0.0f;
        fingerPoint(f, x, y);
        // Buttons first, then the right half drags the camera; the left half is the stick.
        const int button = touchLayout().hit(x, y);
        if (button >= 0) {
            for (auto& slot : button_fingers_) {
                if (slot.button < 0) {
                    slot = {f.fingerID, button};
                    break;
                }
            }
            pressTouchButton(button, true);
            return;
        }
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
        stick_origin_x_ = x;
        stick_origin_y_ = y;
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
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
        float dx = (x - stick_origin_x_) / radius;
        float dy = (y - stick_origin_y_) / radius;
        // The stick's base follows a thumb that drifts past its edge.
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 1.0f) {
            stick_origin_x_ = x - dx / length * radius;
            stick_origin_y_ = y - dy / length * radius;
            dx /= length;
            dy /= length;
        }
        ui::stickDeadzone(dx, dy, kStickDeadzone);
        // Screen y grows downward, so pushing the stick up is forward.
        stick_x_ = dx;
        stick_y_ = -dy;
    }

    void onFingerUp(const SDL_TouchFingerEvent& f) {
        for (auto& slot : button_fingers_) {
            if (slot.button >= 0 && slot.finger == f.fingerID) {
                pressTouchButton(slot.button, false);
                slot.button = -1;
                return;
            }
        }
        if (camera_active_ && f.fingerID == camera_finger_) {
            camera_active_ = false;
            return;
        }
        if (!stick_active_ || f.fingerID != stick_finger_) {
            return;
        }
        stick_active_ = false;
        stick_x_ = 0.0f;
        stick_y_ = 0.0f;
    }

    static constexpr float kWheelRadius = 150.0f;   // pixels
    static constexpr float kWheelDeadZone = 40.0f;  // pixels

    // Gamepad state that persists between frames.
    struct PadState {
        float move_x = 0.0f;
        float move_y = 0.0f;
        float throttle = 0.0f;
        float brake = 0.0f;
        bool fire = false;
        bool aim = false;
        bool run = false;
        bool handbrake = false;
    };

    struct ButtonFinger {
        SDL_FingerID finger = 0;
        int button = -1;
    };

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    SDL_Gamepad* gamepad_ = nullptr;
    PadState pad_;
    bool gamepad_last_used_ = false;
    InputState input_;
    assets::AssetSystem assets_;
    world::World world_{false};
    physics::Physics physics_;
    gameplay::GameState gameplay_;
    renderer::Renderer renderer_;
    debug::DebugUi debug_ui_;
    ui::Hud hud_;
    uint64_t frame_count_ = 0;
    uint64_t last_ticks_ns_ = 0;
    // Holding Q opens the weapon wheel; releasing it over a sector picks that weapon.
    bool wheel_open_ = false;
    float wheel_cursor_x_ = 0.0f;
    float wheel_cursor_y_ = 0.0f;
    // Touch: the stick (analog, -1..1), the camera drag, and the on-screen buttons.
    bool touch_seen_ = false;
    bool stick_active_ = false;
    bool camera_active_ = false;
    SDL_FingerID camera_finger_ = 0;
    SDL_FingerID stick_finger_ = 0;
    float stick_origin_x_ = 0.0f;
    float stick_origin_y_ = 0.0f;
    float stick_x_ = 0.0f;
    float stick_y_ = 0.0f;
    ButtonFinger button_fingers_[4];
    unsigned touch_buttons_down_ = 0;
    bool touch_fire_ = false;
    bool touch_aim_ = false;
    bool touch_handbrake_ = false;
    // Held buttons by device; combineAnalogInput merges them into the input state.
    bool was_pointer_captured_ = false;
    bool mouse_fire_ = false;
    bool mouse_aim_ = false;
    bool space_held_ = false;
    float stats_time_ = 0.0f;
    int stats_frames_ = 0;
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

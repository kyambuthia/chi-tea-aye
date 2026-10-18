# Architecture

C++20 with SDL3 for the window, input and GL context; OpenGL 3.3 core on desktop and
OpenGL ES 3.0 (WebGL 2) on the web through Emscripten. ozz-animation for skeletal
animation, cgltf for assets, Dear ImGui for the dev panel and HUD drawing.

## Rules

1. `main.cpp` only boots the app.
2. `app` owns lifecycle: init, frame, cleanup, input events from every device.
3. `renderer` owns GL setup, shaders, and draw passes.
4. `gameplay` owns player, weapon, vehicle-driving and camera state.
5. `world` owns terrain, the city layout, and its spatial grid.
6. `physics` owns movement and the vehicle simulation.
7. `debug` owns ImGui panels; `ui` owns the player-facing HUD.
8. `assets` owns loading and caching.
9. Keep data flow explicit; no hidden globals.
10. Do not over-engineer.

## Libraries (CMake)

- `chi-world`: `World`, `City`, `SpatialGrid`. No GL, no SDL.
- `chi-character`: character models and props; links `chi-world`.
- `chi-sim`: `GameState`, `Targets`, `WeaponState`, `Physics`, `Vehicle`; links `chi-world`.
- The game links all three with ImGui and SDL3. The checks in `tests/` link only what
  they need and run headless.

## Frame

`app::App::iterate`:

1. Poll the gamepad; merge gamepad, touch, mouse and keys into one `InputState`.
2. `GameState::update`: camera input, enter/exit, weapons and on-foot movement or
   driving, then every car (`Vehicle::update`, fixed 120 Hz steps), car contacts, and
   the camera follow.
3. `Renderer::draw`: pick the quality (`ResolutionScale`), bind the offscreen target
   (`PostPass`), draw the city (`CityPass`), the warrior and props (toon shader),
   targets, contact shadows, the sky, the scope; then resolve and grade to the window.
4. ImGui frame: HUD (`ui::Hud`), dev panel, weapon wheel, shot feedback.
5. On the web, publish `window.chiStats`; swap.

## Simulation and drawing

- On-foot movement runs on the frame time (clamped to 0.1 s); it is tuned to the clips.
- Vehicles run on a fixed 120 Hz step with an accumulator and are drawn interpolated.
- The camera's steady view (`cameraView`) drives shots; the drawn view
  (`renderCameraView`) adds shake.

## Web specifics

- `web/shell.html`: WebGL 2 check, context loss recovery, crash screen, canvas sizing.
- `web/sw.js`: network-first service worker with offline fallback.
- ImGui's GL backend is compiled with `src/debug/ImGuiGlShadow.h` on the web, so it makes
  no per-frame GL state queries (see `docs/web-performance.md`).
- URL options (`?msaa=`, `?dynres=`, `?scale=`, `?time=`, `?fog=`) and `window.chiStats`.

See `docs/research-gta-iv-foundation.md` for why things are built this way and
`docs/roadmap.md` for what comes next.

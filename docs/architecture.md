# Architecture

## Rules

1. `main.cpp` only boots the app.
2. `app` owns lifecycle: init, frame, cleanup, input events.
3. `renderer` owns `sokol_gfx` setup and draw passes.
4. `gameplay` owns player and game state.
5. `world` owns level, terrain, and map data.
6. `physics` owns movement and collision.
7. `debug` owns ImGui panels.
8. `assets` owns loading and caching.
9. Keep data flow explicit.
10. Do not over-engineer.

## Current Flow

`main.cpp` returns a `sapp_desc` from `app::makeAppDesc`.

`app::App` owns the module instances and calls them in order:

1. initialize assets, world, physics, gameplay, renderer, debug UI.
2. collect Sokol input events into `InputState`.
3. update gameplay with explicit access to input, physics, and world.
4. build the debug UI for the current frame.
5. ask renderer to draw the triangle and render ImGui.
6. clean modules up in reverse order.

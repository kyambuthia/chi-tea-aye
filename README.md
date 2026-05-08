# chi-tea-aye

Minimal C++20 Sokol game prototype.

`chi-tea-aye` is the repository codename, not necessarily the final game title.

## First Milestone

- Sokol window and main loop.
- `sokol_gfx.h` renders a colored triangle.
- WASD input is tracked.
- Dear ImGui debug panel shows FPS, frame count, mouse position, and key state.
- CMake build on Linux.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/chi-tea-aye
```

Linux dependencies include an OpenGL development package and X11 development libraries.

## Layout

- `src/main.cpp`: boots the Sokol app only.
- `src/app`: lifecycle, frame orchestration, input events.
- `src/renderer`: `sokol_gfx` setup and draw passes.
- `src/gameplay`: player and game state.
- `src/world`: level, terrain, and map data.
- `src/physics`: movement and collision.
- `src/assets`: asset loading and caching.
- `src/debug`: Dear ImGui debug panels and Sokol-ImGui bridge.

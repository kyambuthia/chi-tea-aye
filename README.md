# chi-tea-aye

Minimal C++20 SDL3 game prototype.

`chi-tea-aye` is the repository codename, not necessarily the final game title.

## First Milestone

- SDL3 window and main loop.
- OpenGL renders a toon-shaded cube over a ground plane.
- WASD input is tracked.
- Dear ImGui debug panel shows FPS, frame count, mouse position, and key state.
- CMake build on Linux.

## Controls

- WASD: move. Hold Shift to run.
- Mouse: hold the left button and drag to orbit the camera; scroll to zoom.
- Touch: drag the left half of the screen to move; drag the right half to orbit.
- Weapons: 1 rifle, 2 pistol, 3 fists. Switching lowers the old weapon and raises the new one.
- Fists: F to punch.
- Rifle: hold the right mouse button to aim down the scope (the view narrows and the scope overlay appears).
- Rifle fire: Space fires one shot. The warrior plays the fire clip with the rifle held.
- F1: toggle the skeleton overlay (joints and bones), which also shows the current clip.

The warrior model is `assets/characters/african_warrior.glb`, and the weapon props are
`assets/models/rifle.glb` and `assets/models/pistol.glb`. They are loaded from the working
directory on desktop, bundled into the web build, and packaged as assets on Android.

## Build

SDL3 and Dear ImGui are git submodules. Clone with `--recurse-submodules`, or run:

```sh
git submodule update --init --recursive
```

Desktop (Linux, macOS, Windows):

```sh
cmake -S . -B build
cmake --build build
./build/chi-tea-aye
```

Web (requires Emscripten; output goes to `build-web/`):

```sh
emcmake cmake -S . -B build-web
cmake --build build-web
python3 -m http.server --directory build-web 8000
```

Android (requires the Android SDK and NDK; the project pins NDK 29):

```sh
cd android
./gradlew assembleDebug
```

Linux desktop builds need OpenGL development headers and the usual SDL3 build dependencies (see `external/SDL/docs`).

## Layout

- `src/main.cpp`: SDL3 main callbacks only.
- `src/app`: lifecycle, frame orchestration, input events.
- `src/renderer`: OpenGL setup, shaders, and draw passes.
- `src/gameplay`: player and game state.
- `src/world`: level, terrain, and map data.
- `src/physics`: movement and collision.
- `src/assets`: asset loading and caching.
- `src/debug`: Dear ImGui debug panels (SDL3 and OpenGL3 backends).

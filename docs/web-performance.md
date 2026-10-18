# Web performance and durability notes

Measurements and fixes for the browser build, with how to repeat them.

## The biggest win: no GL state queries per frame

A Chrome performance trace of the web build showed **77% of main-thread time in
`getParameter`** (`glGetIntegerv`), plus `isProgram` and `isEnabled`. The caller was
Dear ImGui's OpenGL backend, which saves and restores about 25 pieces of GL state around
every UI draw. In a browser each query is a synchronous round trip to the GPU process:
the main thread waits until every queued command has run, so the CPU and GPU never
overlap.

The vendored ImGui is not edited. On the web build only, its backend is compiled with a
forced include, `src/debug/ImGuiGlShadow.h`, that answers the per-frame state queries with
fixed, valid values; start-up queries (versions, limits, extensions) still reach GL. In
exchange the renderer sets all the state it relies on at the start of every frame
(scissor, blend, cull, depth), which is more robust anyway.

Measured on an Intel HD Graphics 3000 (2011 integrated GPU) at 1280x720:

| Setting | Before | After |
| --- | --- | --- |
| Full resolution, no MSAA | 48 fps | 57 fps |
| Default (dynamic quality, 4x MSAA) | 41 fps at 50% scale | 59-60 fps at 100% scale with 4x MSAA |

Rule for new code: **never call `glGet*`, `glIsEnabled`, `glCheckFramebufferStatus` or
`glGetError` per frame in the web build.** Look up uniform locations once at start-up.

## Dynamic quality ladder

`src/renderer/ResolutionScale.h` steps down a ladder when the average frame runs over
20.8 ms for half a second, and climbs back after 2 s of frames under 18 ms:

1. 100% resolution, MSAA
2. 85%, MSAA
3. 100%, no MSAA (on weak integrated GPUs MSAA costs more than resolution)
4. 85%, no MSAA
5. 70%, then 60%, then 50%, no MSAA

- The first 1.5 s are ignored (shader compiles and asset upload made it drop at once).
- A brief hitch does not reset the climb back.
- A level that proved too slow waits twice as long each time before it is tried again
  (2, 4, 8 ... 64 s), so a device that cannot hold MSAA settles instead of bouncing.
- A pixel budget of about 2.2 million caps the scale on 4K and high-density phone
  screens, before frame times even come into it.

All of this is in `render_math_check`.

## URL options (web)

For weak devices, and for repeatable tests:

```text
index.html?msaa=0          MSAA off (0, 2 or 4)
index.html?dynres=0        dynamic quality off
index.html?scale=0.75      fixed 3D scale when dynamic quality is off (0.5 to 1)
index.html?time=22         time of day in hours
index.html?fog=0.5         fog density multiplier
```

## Live numbers for tests: `window.chiStats`

About twice a second the game sets `window.chiStats`:
`fps, scale, width, height, samples, offscreen, visibleBoxes, totalBoxes, driving, speed,
x, z, carInReach, nearestCar`. Browser automation can read it to check frame rate and
game state without screenshots. Example, from the DevTools console:

```js
await new Promise(r => setTimeout(r, 5000)); window.chiStats
```

## Durability fixes in this pass

- **Service worker shipped stale builds forever.** It was cache-first with a fixed
  cache name. It is now network-first with the cache as the offline fallback.
- **Esc killed the web game.** The handler returned "quit", which stops the
  Emscripten main loop and leaves a dead canvas. On web, Esc now only frees the mouse.
- **Keys stuck after alt-tab.** Key releases do not reach an unfocused window. On focus
  loss every held input is dropped and the mouse is released.
- **Lost WebGL context froze the page.** The shell now handles `webglcontextlost` and
  reloads when the context comes back. Test it from the console:
  `document.getElementById('canvas').getContext('webgl2').getExtension('WEBGL_lose_context').loseContext()`.
- **Crashes froze the page.** `Module.onAbort` shows the error with a Reload button.
- **No WebGL 2.** Detected before loading, with a clear message.
- **Editing only `web/shell.html` did not rebuild the page.** It is now a link dependency.
- **The offscreen target can fail** (old driver, out of memory). The post pass then turns
  itself off and the scene draws straight to the window.

## Cheap by construction

- Every city box (buildings, walls, lamps, car parts) is one instanced draw call, culled
  per frame by frustum and distance. About 400 of 1400 boxes are drawn from the start point.
- Roads, markings, sidewalks and facades are procedural in the shaders: no textures to
  download, and the city adds no bytes to `index.data`.
- Parked cars sleep and cost nothing until touched.
- The sky draws last, only where nothing else did.

## Checking the desktop build headless

`scripts/desktop_screenshot.sh out.png` runs the desktop build on Xvfb display `:98` and
saves a frame. It forces SDL's X11 driver, because SDL3 otherwise prefers Wayland and
opens the window on the real desktop.

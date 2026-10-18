# Research notes: a GTA IV-style foundation for the browser build

Goal: make the web build play and feel like an open-world crime game in the GTA IV
mould (a city to roam, cars with weight and body roll, a chase camera, haze, a
minimap), on every current browser, and lay groundwork that later systems (traffic,
pedestrians, ragdolls, streaming) can build on.

This file records what was looked at, what was taken from each source, and where it
landed in the code.

**Current state of the playable scene:** the city is built and tested, but the game
currently starts with it switched off (`world::World world_{false}` in `src/app/App.cpp`):
open terrain, no buildings, walls or target range. Pass `true` (the default) to bring the
city back. Everything city-related below still builds and is covered by `world_check`. A source marked *secondary* was only read through a summary or a
mirror, not the original talk or paper.

## What makes GTA IV feel like GTA IV

Breaking the target down into things that can be built:

| Feel | Technique | Status |
| --- | --- | --- |
| A dense city to roam | Procedural blocks, lots, setback towers; cheap to draw | Built (blockout); off in the playable scene for now |
| Heavy cars that roll and pitch | Raycast suspension, slip-based tyres, low damping | Done |
| A chase camera that comes back behind the car | Spring follow, auto-recenter, speed FOV | Done |
| Hazy, warm air; sodium-orange nights | Exponential distance fog to the horizon colour, time of day | Done |
| Knowing where you are | Minimap that turns with the camera | Done |
| Hits that feel physical | Camera trauma shake, impulses on cars | Done (camera); bodies: roadmap |
| Bodies that react (Euphoria) | Active ragdoll | Roadmap |
| A living street | Traffic and pedestrians on lanes and sidewalks | Roadmap |
| No loading screens | World streaming by cell | Roadmap (the grid is the first step) |

## Sources and what each contributed

### Fixed timestep with interpolation — Glenn Fiedler, "Fix Your Timestep!"
<https://gafferongames.com/post/fix_your_timestep/>

Run physics in fixed steps from an accumulator, and draw a blend of the last two
states using the leftover fraction, so the simulation is the same at 30, 60 or 144 Hz
and motion does not stutter.

- `physics::Vehicle` steps at 120 Hz (`Vehicle::kStep`), caps catch-up at 0.2 s, and
  draws `renderPosition()` / `renderOrientation()` interpolated between steps.
- The camera follows the interpolated pose, so the car and the camera never jitter
  against each other.
- Checked by `vehicle_check` case 6: a drive with long, uneven frames ends within
  8 cm of the same drive at a steady 60 fps.

### Raycast vehicles — Bullet's raycast vehicle, Marco Monster, Kester Maddock, Toyful Games
- Bullet raycast vehicle (Blender wiki): <https://wiki.blender.jp/Doc:2.6/Manual/Game_Engine/Physics/Objects/Vehicle>
- GameDev.net "how to implement raycast car": <https://www.gamedev.net/forums/topic/595442-how-to-implement-raycast-car/>
- Marco Monster, "Car Physics for Games" (mirror): <https://www.asawicki.info/Mirror/Car%20Physics%20for%20Games/Car%20Physics%20for%20Games.html>
- Toyful Games, custom car physics for *Very Very Valet* (secondary, talk summary):
  <https://opentools.ai/youtube-summary/making-custom-car-physics-in-unity-for-very-very-valet>

One rigid body; each wheel casts a ray along the body's down axis; a spring on the
compression plus a damper on its rate holds the body up; tyre forces come from the
contact's velocity. Longitudinal and lateral forces are handled separately and then
limited together (the friction circle). Slip angle is `atan2(v_lat, |v_long|)` in the
wheel's frame, regularised at low speed so a parked car does not jitter.

What was kept and why, in `src/physics/Vehicle.cpp`:
- Soft springs (about 1.6 Hz on a quarter of the mass) and under-damped dampers:
  the car pitches under braking and rolls in turns, which is the GTA IV signature.
- Tyre forces applied part-way up toward the centre of mass (`roll_influence`, a
  Bullet idea): keeps visible body roll without constant roll-overs.
- Rear-wheel drive with a traction cap, and a handbrake that cuts rear grip:
  power-on and handbrake slides are possible but not automatic.
- Lateral force falls off gently past the peak slip, so the limit is a slide, not a snap.
- Body corners against the ground, and three footprint circles against buildings, with
  impulse responses: crashes stop the car, bounce it, and report an impact strength.
- A car on its roof or side for 2.5 s is set back on its wheels (GTA sets it on fire;
  that can come later).
- Cars with no driver hold all four wheels (`VehicleInput::parked`). Found in the
  browser: without it, the park car rolled 2 m down the slope after spawning.
- Parked cars sleep once still and wake on contact, so idle cars cost nothing.

### Camera — John Nesky, "50 Game Camera Mistakes" (GDC 2014); Squirrel Eiserloh, "Juicing Your Cameras With Math" (GDC 2016)
- <https://gdcvault.com/play/1021262/50-Camera>
- <https://www.gamedeveloper.com/programming/video-sprucing-up-cameras-with-math> (secondary)
- Bevy's trauma shake example, which follows the talk: <https://bevy.org/examples/camera/2d-screen-shake/>

Taken: camera springs rather than hard follows, collision pulls the camera in rather than
letting it clip, and the player's own look input always wins over auto-recentering.
Shake is driven by *trauma* (0..1, added by events, decaying), with the visible shake
being trauma squared, from smooth noise rather than random jitter, and applied for
drawing only.

- `GameState::renderCameraView` adds the shake; `cameraView` and `cameraRay` stay
  steady, so shake never moves where a bullet goes.
- Shots add a little trauma; crashes add trauma scaled by the impact impulse.
- In a car the camera recenters behind it after 0.8 s without look input, faster at
  speed, and the field of view widens by up to 16 degrees with speed.

### Frustum culling — Gribb and Hartmann plane extraction
- Ogre wiki: <https://wiki.ogre3d.org/Frustum+Culling+In+Object+Space>
- Lighthouse3D: <https://www.lighthouse3d.com/tutorials/view-frustum-culling/clip-space-approach-implementation-details/>

The six planes are sums and differences of the view-projection matrix's rows; with
normalised planes the box test is one dot product per plane on the box's farthest
corner. `src/math/Frustum.h`, checked by `render_math_check`. Every city box and car is
culled per frame, plus a distance cull at the point the fog has swallowed everything.

### Spatial partitioning — uniform grids and grid traversal
- Spatial hash and uniform grid notes: <https://gitea.moonside.games/MoonsideGames/bonk-docs/src/branch/main/content/broad_phase/spatial_hash.md>,
  <https://abratabia.com/game-architecture/spatial-partitioning.php>
- Amanatides and Woo, "A Fast Voxel Traversal Algorithm for Ray Tracing" (Eurographics 1987):
  the standard cell walk for rays (cited from memory, not re-fetched).

The city is bounded and its buildings are spread evenly, which is the case where a plain
uniform grid beats trees. `src/world/SpatialGrid.cpp` stores cells flat (an offset table
plus one index array), answers rectangle queries for the warrior and cars, and walks
rays cell by cell, stopping as soon as the nearest hit is closer than the next cell.
`world_check` compares 2000 queries and 3000 rays against brute force: zero mismatches.

### Procedural cities — Greuter et al. 2003; Kelly and McCabe (SIGGRAPH 2006)
- Greuter et al., "Real-time procedural generation of 'pseudo infinite' cities": <https://svn.sable.mcgill.ca/sable/courses/COMP763/oldpapers/greuter-03-real-time.pdf>
- Kelly and McCabe, "Interactive generation of cities for real-time applications": <https://history.siggraph.org/?p=139434>

Taken: generate from a seed with a deterministic hash per block so the city is the same
everywhere and can later be generated cell by cell on demand; keep building geometry
trivial (boxes) and put the detail in the shader. `src/world/City.cpp` splits each block
into jittered lots with alleys, sizes buildings by distance from downtown (the park),
and steps tall ones back with a tower on top. Facades (floors, window bays, shop fronts,
lit windows at night) are procedural in `CityPass.cpp`.

### Fog — Inigo Quilez, "Better Fog"; exponential height fog
- <https://iquilezles.org/articles/fog/> (read through quotes; secondary)
- Unreal's exponential height fog docs: <https://dev.epicgames.com/documentation/unreal-engine/exponential-height-fog-in-unreal-engine?lang=en-US>

Distance fog `1 - exp(-d * density)` toward the horizon colour of the sky, so distant
buildings melt into the sky instead of popping at a far plane; a little thinner high
up so skylines stand out. Shared by every pass through `ShaderChunks.h`. The full
height-integrated form is a roadmap item.

### Dynamic resolution
- Unity's dynamic resolution docs (general technique): <https://docs.unity3d.com/Manual/DynamicResolution.html>

Lower the 3D resolution from frame times, render the UI at full resolution. The
implementation here (`ResolutionScale.h`) is a ladder rather than a single scale,
because measurement on a weak integrated GPU showed MSAA cost more than pixels did
(see `docs/web-performance.md`).

### WebGL robustness — Khronos
- Handling context loss: <https://wikis.khronos.org/webgl/HandlingContextLost>
- WEBGL_lose_context (for testing): <https://registry.khronos.org/webgl/extensions/WEBGL_lose_context>
- WebGL 2 in every major browser, Safari 15 and later: <https://www.khronos.org/blog/webgl-2-achieves-pervasive-support-from-all-major-web-browsers>

`web/shell.html` cancels the default on `webglcontextlost` (which tells the browser the
page will recover), shows a message, and reloads on `webglcontextrestored`. It also
checks for WebGL 2 before loading and shows a reload button if the runtime aborts.

### Physics-driven bodies — Jakobsen (Hitman), NaturalMotion Euphoria (GTA IV)
- Thomas Jakobsen, "Advanced Character Physics" (GDC 2001): <https://www.gamedeveloper.com/programming/advanced-character-physics>
- Euphoria in GTA IV and Max Payne 3 (press coverage; secondary): <https://www.techradar.com/news/gaming/the-technology-of-max-payne-3-1082751>

Not built yet; it is the largest piece of "GTA IV feel" still missing. The plan in
`docs/roadmap.md` starts with a Verlet ragdoll on the existing ozz skeleton (Jakobsen's
particles-and-constraints method is cheap enough for wasm on one thread), then blends
animation and physics, and only then adds balance behaviours.

## Browser support decisions

The build has to run on every current browser, so:
- **WebGL 2 only, no WebGPU.** WebGL 2 is in Chrome, Edge, Firefox and Safari 15+.
- **One thread, no SharedArrayBuffer.** Threads in wasm need cross-origin isolation
  headers that many hosts and the PWA setup do not send.
- **Pointer lock with a fallback.** Click to capture the mouse; Esc frees it (the browser
  enforces that); middle-drag still orbits when the mouse is free. iOS Safari has no
  pointer lock, which is fine because it is touch-only.
- **Gamepad through SDL's Gamepad API backend**, standard mapping.
- **Touch:** an analog stick on the left half; buttons on the right.
- **No sync GL queries per frame.** See `docs/web-performance.md`.
- **Esc never quits the web build** (it used to stop the main loop and leave a dead canvas).

## Where things are

| Area | Files |
| --- | --- |
| City layout, surfaces, parking | `src/world/City.*` |
| Spatial grid, rays | `src/world/SpatialGrid.*`, `src/math/Geometry.h` |
| Vehicle simulation | `src/physics/Vehicle.*`, `src/math/Vec3.h` |
| Driving, enter/exit, car contacts, camera, shake | `src/gameplay/GameState.*` |
| City rendering, roads, sky, contact shadows | `src/renderer/CityPass.*`, `src/renderer/ShaderChunks.h` |
| Offscreen target, MSAA, grade | `src/renderer/PostPass.*` |
| Time of day | `src/renderer/Atmosphere.h` |
| Dynamic quality | `src/renderer/ResolutionScale.h` |
| Culling | `src/math/Frustum.h` |
| HUD: minimap, speedometer, prompts, touch buttons | `src/ui/Hud.*`, `src/ui/HudLayout.h` |
| Input: pointer lock, gamepad, touch, focus loss | `src/app/App.cpp`, `src/app/InputState.h` |
| Page shell, service worker | `web/shell.html`, `web/sw.js` |
| Checks | `tests/world_check.cpp`, `tests/vehicle_check.cpp`, `tests/render_math_check.cpp`, `tests/hud_check.cpp`, `tests/movement_check.cpp` |

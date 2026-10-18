# Roadmap toward a GTA IV feel (web build)

Ordered by how much each step adds to the feel, and by what it depends on. Each item
names the code it builds on. Research and sources are in
`docs/research-gta-iv-foundation.md`; performance rules in `docs/web-performance.md`.

All of it must keep running in current Chrome, Edge, Firefox and Safari: WebGL 2, one
thread, no per-frame GL queries, touch and gamepad as well as mouse and keyboard.

## Foundation in place

- City generator, spatial grid and grid rays (`src/world`). Currently off in the
  playable scene; `World(true)` turns it on.
- Raycast car with fixed-step physics, sleeping, parking brake, crashes, recovery
  (`src/physics/Vehicle.*`).
- Enter and exit cars, driving controls on every device, chase camera, speed FOV,
  trauma shake (`src/gameplay/GameState.*`).
- Instanced, culled city rendering; procedural roads and facades; fog, sky and time of
  day; contact shadows; MSAA and dynamic quality (`src/renderer`).
- Minimap, speedometer, prompts, touch buttons (`src/ui`).
- Pointer lock, gamepad, analog touch stick, focus-loss safety (`src/app`).
- Web shell hardening and a network-first service worker (`web/`).

## Next, in order

### 1. Feel of the car (small, high value)
- **Engine and tyre audio.** Web Audio through SDL3 audio: a looped engine sample
  pitched by wheel spin rate, a skid layer driven by `Wheel::slip`, impact thumps from
  `Vehicle::lastImpact()`. Browsers need a user gesture first; start audio on the first
  click or tap, the same gesture that captures the mouse.
- **Headlight beams and brake lights.** Brake lights brighter while braking (pass the
  brake input into the instance params); at night a projected cone on the road in the
  floor shader, two per car near the camera.
- **Damage.** Accumulate impact energy per car; dent by squashing the body box toward
  the hit, then smoke, then fire. This is the GTA alternative to the current flip-back.
- **Tuning by class.** `VehicleSpec` presets: saloon (current), sports, truck, taxi.

### 2. A living street
- **Traffic.** Lanes come straight from `CityLayout` (two 4 m lanes each way, centrelines
  at 30 + 60k). A lane graph with junction nodes; each AI car follows its lane with pure
  pursuit steering on the same `physics::Vehicle`, keeps distance to the car ahead with
  a ray along the lane, and picks a turn at junctions. Sleep and despawn cars far from
  the player; spawn just outside the camera's view, using the frustum test.
- **Pedestrians.** Walk the sidewalk band (`Surface::Sidewalk`) with simple steering and
  separation; reuse the warrior or Y-Bot skinned mesh at a lower update rate far away.
  Flee from gunfire and from cars on the sidewalk.
- **Traffic lights** at junctions, timed per junction; the floor shader can draw stop lines.

### 3. Bodies that react (the Euphoria gap)
- **Verlet ragdoll** on the ozz skeleton (Jakobsen): particles at joints, distance
  constraints along bones, simple angle limits, ground and box collisions through the
  spatial grid. Hit by a car or shot: switch the warrior or a pedestrian to the ragdoll,
  seeded with the current pose and velocity.
- **Get-up**: blend from the ragdoll's final pose into a get-up clip.
- **Partial ragdoll**: physics on the upper body only for flinches while the legs keep
  animating; the start of balance behaviours.

### 4. Combat in the city
- **Cover**: the walls and building faces are already boxes in the grid. Snap to the
  nearest face in front on a key press, slide along it, peek and fire from the edge.
- **Drive-by**: aim from the car with the pistol; the camera already has the aim ray.
- **Wanted level**: police cars from the traffic system, heat that decays out of sight.

### 5. World scale
- **Streaming by cell**: the generator is deterministic per block (`hashBlock`), so
  blocks can be built when they come within range and dropped when far. Keep the grid
  per cell and the instance list per cell; cull whole cells first.
- **Building LODs and impostors**: past a distance, merge each block into one box per
  building with a flat facade; far blocks as one textured card.
- **Districts**: vary palette, height and lot size by region (docks, downtown,
  projects); add rivers and bridges for a Liberty City silhouette.

### 6. Image
- **Shadow map** for the sun: one cascade around the player, then two. WebGL 2 has depth
  textures and comparison samplers. Keep the blob shadows for cars and people as contact
  shadows.
- **Bloom** on lit windows, lamps and headlights at night: a half-resolution
  bright-pass and blur in the post pass.
- **Height fog** integrated along the view ray (Quilez), and a sun-glow term in the fog.
- **Wet roads and rain**: the floor shader already has a sheen term; add puddles and
  reflections of lights.
- **Anti-aliasing of procedural edges**: road edges and sidewalk joints are computed per
  pixel, so MSAA does not smooth them; blend surfaces with `fwidth`-based smoothstep.

## Rules for every step

- Add the check first: each system above has a pure core that can be tested headless,
  like `vehicle_check` and `world_check`.
- Keep simulation on fixed steps and drawing interpolated.
- Profile in Chrome on a weak GPU before and after (`window.chiStats`, a performance
  trace); no new per-frame GL state queries.
- Run `scripts/verify.sh`, look at the result in the browser, and rebuild `build-web/`.

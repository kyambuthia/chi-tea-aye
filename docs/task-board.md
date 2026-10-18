# Task Board

## Done

- SDL3 app with desktop (GL 3.3) and web (WebGL 2, PWA) targets; one `scripts/verify.sh`.
- Skinned warrior with ozz-animation clips, foot IK, weapon props, aiming and firing.
- Target range, crouching, weapon wheel, low walls.
- City generator, spatial grid with grid rays (`world_check`).
- Raycast car: suspension, slip tyres, handbrake, crashes, sleeping, parking brake,
  flip recovery (`vehicle_check`).
- Enter and exit cars; driving on keyboard, gamepad and touch; chase camera; speed FOV;
  trauma camera shake (`movement_check`).
- Instanced, culled city rendering; procedural roads and facades; fog, sky, time of day;
  contact shadows; MSAA; dynamic quality ladder (`render_math_check`).
- HUD: minimap, speedometer, prompts, touch buttons (`hud_check`).
- Pointer-lock mouse look, gamepad, analog touch stick, input dropped on focus loss.
- Web durability: context loss, crash screen, WebGL 2 check, network-first service
  worker, Esc no longer stops the web game, no per-frame GL queries from ImGui.

## In progress

- Playable scene on open terrain with the city off (`World{false}`), and the Y-Bot
  character with a companion.

## Next

See `docs/roadmap.md`, in order: car audio, lights and damage; traffic and pedestrians;
ragdolls; cover and drive-bys; streaming and LODs; sun shadows and bloom.

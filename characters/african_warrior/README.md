# African warrior character

Original adult male African warrior, with a fictional East African-inspired design.
References were generated using Codex's built-in image generation tool. Final prompts
are saved beside the front and back reference images. Mesh generation uses Meshy 7.1;
rigging, skinning, animation and export use Blender **3.0.0**.

## Files

- `references/front.png`, `references/back.png`: consistent T-pose references.
- `meshy/model.glb`: the original textured, unrigged Meshy result.
- `blender/african_warrior_rigged.blend`: editable Blender source with packed textures.
- `exports/african_warrior.glb`: skinned character with embedded PBR textures and animation.
- `exports/african_warrior.fbx`: skinned character with embedded textures and animation takes.
- `exports/african_warrior_mixamo.glb` and `.fbx`: skinned exports with the full retargeted animation set.
- `blender/african_warrior_mixamo.blend`: editable source with all retargeted Mixamo actions.
- `exports/textures/`: separate 2048×2048 base color, normal, roughness and metallic maps.
- `renders/`: actual Blender renders of the mesh and posed character.
- `validation.json`, `export-validation.json`: original rig skinning and export checks.
- `animations/mixamo/retarget-validation.json` and
  `animations/mixamo/export-validation.json`: retarget metrics and 16-clip export
  round-trip checks.
- `landmarks.json`: measured joint placement in metres, in the normalized T-pose.

## Rig and import

The source character is 1.85 metres tall, Z up, facing -Y. The GLB uses the glTF Y-up
convention. Use your engine's normal FBX/GLB importer to convert axes and units.
The root bone is `Root`; `Hips` begins the body hierarchy. The left/right bone names
use common humanoid names such as `LeftArm`, `RightUpLeg`, and `LeftHand`.
Attach separately authored equipment to the `LeftHand` or `RightHand` bone.

The original `exports/african_warrior.glb` and `.fbx` contain three independent
procedural clips at 30 fps: `Idle`, `Walk`, and `Run`. They are in place, with no
horizontal root motion. In Blender, `Idle` is the active action in that original
source file.

The Mixamo source library in `animations/mixamo/` has 16 clips. All 16 have been
baked onto `AfricanWarrior_Rig` with horizontal hips motion locked and exported to
the `_mixamo` GLB/FBX files. The manifest records looping and in-place settings.

This is an FK body rig. The generated geometry has no dedicated facial or individual
finger rig, no hand-retopologized deformation loops, and no cloth simulation. The
wrap is skinned to the body. Further retargeting, foot contact polish, combat logic,
collision/physics, and a full player controller belong to game-engine integration.
This asset has not been tested in a particular game engine.

The GLB embeds the full PBR material. Blender 3.0's FBX round-trip retains the base
color and normal textures; connect the supplied separate roughness and metallic
maps in your engine's material editor when importing FBX.

## Measured result and verification

- 31,153 triangles; 15,582 welded vertices in the Blender source.
- 23 skeleton bones (22 deform bones and a root); 1.85 m normalized height.
- One UV set and one material; four 2048×2048 source PBR maps.
- Zero unweighted vertices; at most four normalized influences per vertex.
- Blender pose check moved 6,015 vertices; no non-finite geometry.
- The original GLB and FBX re-import with all 23 bones and their three procedural
  clips. The Mixamo GLB and FBX re-import with all 23 bones and all 16 clips; every
  clip deforms the exported skin.
- The original GLB contains three embedded images (base color, normal, packed
  metallic/roughness).
- The original `.blend` reopens in Blender 3.0.0 with three packed images and
  `Idle` active. The separate Mixamo `.blend` contains all 16 actions.
- Meshy task `01a14663-553f-7421-937e-c80dbf95bffa` succeeded and consumed 30 credits.

The inspection, rig-building and round-trip verification commands below completed
successfully. The initial Eevee inspection could not open a display in the sandbox;
the completed pipeline renders with CPU Cycles.

## Reproduce and verify

Run from the repository root. Original head-study files are independent of this asset.

```bash
# Inspect the exact request without accessing credentials or spending credits.
.venv/bin/python tools/meshy_character.py \
  --images characters/african_warrior/references/front.png characters/african_warrior/references/back.png \
  --out characters/african_warrior/meshy --dry-run

# Resume the existing paid task and download results; do not submit a second task.
.venv/bin/python -u tools/meshy_character.py \
  --task 01a14663-553f-7421-937e-c80dbf95bffa \
  --out characters/african_warrior/meshy

# Inspect actual geometry before adjusting landmarks.
blender --factory-startup -b --python-exit-code 1 -P blender/scripts/warrior_probe.py -- \
  --mesh characters/african_warrior/meshy/model.glb \
  --out characters/african_warrior/renders/inspection

# Build the rig, skin, animations, source file and game exports.
blender --factory-startup -b --python-exit-code 1 -P blender/scripts/rig_warrior.py -- \
  --mesh characters/african_warrior/meshy/model.glb \
  --landmarks characters/african_warrior/landmarks.json \
  --out characters/african_warrior

# Re-import both exported formats and check animation and skinning contracts.
blender --factory-startup -b --python-exit-code 1 -P blender/scripts/verify_warrior_exports.py -- \
  --out characters/african_warrior

# Retarget the 16 Mixamo clips to the warrior, then verify and publish the IQM.
blender -b --python-exit-code 1 -P blender/scripts/retarget_mixamo.py -- \
  --out characters/african_warrior
blender -b --python-exit-code 1 -P blender/scripts/verify_mixamo.py -- \
  --out characters/african_warrior
```

## Sources

- [Meshy multi-image API](https://docs.meshy.ai/en/api/multi-image-to-3d): model pin,
  image ordering, T-pose, remeshing, polygon targets and PBR output options.
- [Blender 3.0 object API](https://docs.blender.org/api/3.0/bpy.ops.object.html):
  transform application and automatic armature parenting.
- [Blender 3.0 edit bones](https://docs.blender.org/api/3.0/bpy.types.EditBone.html):
  bone placement and connected parent hierarchy.
- [Blender 3.0 scene exporters](https://docs.blender.org/api/3.0/bpy.ops.export_scene.html)
  and the installed Blender 3.0 glTF/FBX add-on source: animation and texture export.

"""Usage: blender -b --python scripts/retarget_root_motion.py -- <mixamo fbx folder> <output folder>

Root-motion retarget: same bake as retarget_mixamo.py, but the source's horizontal
hips travel is kept and written to the Root bone, instead of being dropped. The Hips
bone stays in place, so the warrior's body and its root path are separate."""
import importlib.util
import json
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector

RM_PATH = Path(__file__).resolve().parent.parent / 'blender/scripts/retarget_mixamo.py'
spec = importlib.util.spec_from_file_location('rm', RM_PATH)
rm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rm)


def source_samples_root(path, target):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True)
    imported = set(bpy.data.objects) - before
    source = next(o for o in imported if o.type == 'ARMATURE')
    action = source.animation_data.action
    mapping = {b.name.split(':')[-1]: b.name for b in source.data.bones}
    names = [b.name for b in target.data.bones if b.name != 'Root']
    missing = set(names) - set(mapping)
    if missing:
        raise RuntimeError(f'Missing source bones: {sorted(missing)}')
    rest = {name: (source.matrix_world @ source.data.bones[mapping[name]].matrix_local).to_quaternion() for name in names}
    hip_height = (source.matrix_world @ source.data.bones[mapping['Hips']].head_local).z
    start, end = (int(v) for v in action.frame_range)
    frames = []
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        matrices = {name: source.matrix_world @ source.pose.bones[mapping[name]].matrix for name in names}
        hips = matrices['Hips'].translation
        frames.append({
            'rotations': {name: matrices[name].to_quaternion() @ rest[name].inverted() for name in names},
            'hip_z': hips.z,
            'hip_xy': (hips.x, hips.y),
            'foot_z': min(matrices[name].translation.z for name in ['LeftToeBase', 'RightToeBase', 'LeftFoot', 'RightFoot']),
        })
    for obj in imported:
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.actions.remove(action)
    return frames, hip_height


def bake_root_clip(target, body, name, frames, source_hip_height, loop):
    scene = bpy.context.scene
    rest_q = {b.name: b.matrix_local.to_quaternion() for b in target.data.bones}
    world_q = target.matrix_world.to_quaternion()
    world_inv = world_q.inverted()
    hip_rest = target.data.bones['Hips']
    scale = (target.matrix_world @ hip_rest.head_local).z / source_hip_height
    source_floor = min(f['foot_z'] for f in frames)
    start_xy = frames[0]['hip_xy']
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    action['source'] = 'Adobe Mixamo / Y Bot (root motion)'
    action['in_place'] = False
    action['loop'] = loop
    target.animation_data.action = action
    previous = {}
    minima, maxima, rotations_error = [], [], 0.0
    for frame, sample in enumerate(frames, 1):
        scene.frame_set(frame)
        desired = {'Root': rest_q['Root']}
        for pb in target.pose.bones:
            pb.location = (0, 0, 0)
            pb.scale = (1, 1, 1)
            pb.rotation_mode = 'QUATERNION'
            if pb.name == 'Root':
                pb.rotation_quaternion = Quaternion()
                continue
            desired[pb.name] = world_inv @ sample['rotations'][pb.name] @ world_q @ rest_q[pb.name]
            parent = pb.parent.name
            local_rest = rest_q[parent].inverted() @ rest_q[pb.name]
            rotation = local_rest.inverted() @ desired[parent].inverted() @ desired[pb.name]
            rotation.normalize()
            if pb.name in previous and rotation.dot(previous[pb.name]) < 0:
                rotation.negate()
            previous[pb.name] = rotation.copy()
            pb.rotation_quaternion = rotation
        # Root path: the source's horizontal travel from its first frame, scaled to the warrior.
        dx = (sample['hip_xy'][0] - start_xy[0]) * scale
        dy = (sample['hip_xy'][1] - start_xy[1]) * scale
        root = target.pose.bones['Root']
        root.location = rest_q['Root'].inverted() @ Vector((dx, dy, 0.0))
        hips = target.pose.bones['Hips']
        vertical = (sample['hip_z'] - source_hip_height) * scale
        hips.location = rest_q['Hips'].inverted() @ Vector((0, 0, vertical))
        bpy.context.view_layer.update()
        low, high = rm.skin_bounds(body)
        clearance = max(0.0, sample['foot_z'] - source_floor) * scale
        hips.location += rest_q['Hips'].inverted() @ Vector((0, 0, clearance - low[2]))
        bpy.context.view_layer.update()
        low, high = rm.skin_bounds(body)
        minima.append(low)
        maxima.append(high)
        for pb in target.pose.bones:
            if pb.name != 'Root':
                error = pb.matrix.to_quaternion().rotation_difference(desired[pb.name]).angle
                rotations_error = max(rotations_error, min(error, abs(2 * 3.141592653589793 - error)))
            pb.keyframe_insert('rotation_quaternion', frame=frame, group=pb.name)
        hips.keyframe_insert('location', frame=frame, group='Hips')
        root.keyframe_insert('location', frame=frame, group='Root')
    if rotations_error > 0.002:
        raise RuntimeError(f'{name}: rotation transfer error {rotations_error}')
    for curve in action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
    travel = Vector((dx, dy, 0.0)).length
    target.animation_data.action = None
    track = target.animation_data.nla_tracks.new()
    track.name = name
    track.strips.new(name, 1, action)
    track.mute = True
    return action, {'frames': len(frames), 'fps': 30, 'loop': loop,
                    'root_travel_m_units': travel,
                    'maximum_rotation_error_radians': rotations_error}


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    src_dir, out_dir = Path(argv[0]), Path(argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    rig = Path(__file__).resolve().parent.parent / 'characters/african_warrior/blender/african_warrior_rigged.blend'
    bpy.ops.wm.open_mainfile(filepath=str(rig))
    target = bpy.data.objects['AfricanWarrior_Rig']
    body = bpy.data.objects['AfricanWarrior_Body']
    target.animation_data_clear()
    target.animation_data_create()
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    bpy.context.scene.render.fps = 30
    clips = [
        ('Idle', 'idle.fbx', True),
        ('Walk', 'walking.fbx', True),
        ('Run', 'standard run.fbx', True),
        # The walking strafes: the non-walking strafe clips shuffle without planting their feet.
        ('StrafeLeft', 'left strafe walking.fbx', True),
        ('StrafeRight', 'right strafe walking.fbx', True),
        ('WalkBackwards', 'walking_backwards.fbx', True),
        # One-shot: played once for a fist attack.
        ('PunchCombo', 'punch_combo.fbx', False),
        # Weapon-held locomotion, from the Rifle 8-Way and Pistol packs.
        ('RifleIdleAim', 'rifle_idle_aim.fbx', True),
        ('RifleWalk', 'rifle_walk.fbx', True),
        ('RifleRun', 'rifle_run.fbx', True),
        ('PistolIdle', 'pistol_idle.fbx', True),
        ('PistolWalk', 'pistol_walk.fbx', True),
        ('PistolRun', 'pistol_run.fbx', True),
    ]
    actions, report = [], {'clips': {}}
    for title, filename, loop in clips:
        frames, hip_height = source_samples_root(src_dir / filename, target)
        action, metrics = bake_root_clip(target, body, title, frames, hip_height, loop)
        actions.append(action)
        report['clips'][title] = metrics
        print('BAKED', title, json.dumps(metrics), flush=True)
    bpy.ops.object.select_all(action='DESELECT')
    target.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = target
    target.animation_data.action = None
    for pb in target.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = Quaternion()
        pb.scale = (1, 1, 1)
    print('NLA tracks before export', len(target.animation_data.nla_tracks), [t.name for t in target.animation_data.nla_tracks], 'selected', target.select_get(), body.select_get(), flush=True)
    bpy.context.scene.frame_set(1)
    bpy.ops.export_scene.gltf(filepath=str(out_dir / 'african_warrior_root.glb'),
        export_format='GLB', use_selection=True, export_animations=True,
        export_nla_strips=True, export_force_sampling=True, export_frame_range=False,
        export_skins=True, export_all_influences=False, export_def_bones=False)
    (out_dir / 'root-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('ROOT RETARGET EXPORTED', flush=True)


if __name__ == '__main__':
    main()

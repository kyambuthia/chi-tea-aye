"""Bake the downloaded Mixamo Y Bot motions onto the existing Blender 3.0 rig.

Uses rest-relative world rotations, preserves the warrior's bone lengths and
skin weights, and locks horizontal hips motion for the game controller.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
import sys
from array import array
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector


def skin_bounds(body):
    mesh = body.evaluated_get(bpy.context.evaluated_depsgraph_get()).data
    coords = array('f', [0.0]) * (len(mesh.vertices) * 3)
    mesh.vertices.foreach_get('co', coords)
    if not all(math.isfinite(v) for v in coords):
        raise RuntimeError('Non-finite deformed skin')
    return [min(coords[i::3]) for i in range(3)], [max(coords[i::3]) for i in range(3)]


def source_samples(path, target):
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
        frames.append({
            'rotations': {name: matrices[name].to_quaternion() @ rest[name].inverted() for name in names},
            'hip_z': matrices['Hips'].translation.z,
            'foot_z': min(matrices[name].translation.z for name in ['LeftToeBase', 'RightToeBase', 'LeftFoot', 'RightFoot']),
        })
    for obj in imported:
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.actions.remove(action)
    return frames, hip_height


def bake_clip(target, body, name, frames, source_hip_height, loop):
    scene = bpy.context.scene
    rest_q = {b.name: b.matrix_local.to_quaternion() for b in target.data.bones}
    world_q = target.matrix_world.to_quaternion()
    world_inv = world_q.inverted()
    hip_rest = target.data.bones['Hips']
    scale = (target.matrix_world @ hip_rest.head_local).z / source_hip_height
    source_floor = min(f['foot_z'] for f in frames)
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    action['source'] = 'Adobe Mixamo / Y Bot'
    action['in_place'] = True
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
        hips = target.pose.bones['Hips']
        vertical = (sample['hip_z'] - source_hip_height) * scale
        hips.location = rest_q['Hips'].inverted() @ Vector((0, 0, vertical))
        bpy.context.view_layer.update()
        low, high = skin_bounds(body)
        # Retain source airborne clearance while keeping the differently
        # proportioned skin above the floor. This is a body offset, not foot IK.
        clearance = max(0.0, sample['foot_z'] - source_floor) * scale
        hips.location += rest_q['Hips'].inverted() @ Vector((0, 0, clearance - low[2]))
        bpy.context.view_layer.update()
        low, high = skin_bounds(body)
        minima.append(low)
        maxima.append(high)
        for pb in target.pose.bones:
            error = pb.matrix.to_quaternion().rotation_difference(desired[pb.name]).angle
            rotations_error = max(rotations_error, min(error, abs(2 * math.pi - error)))
            pb.keyframe_insert('rotation_quaternion', frame=frame, group=pb.name)
        hips.keyframe_insert('location', frame=frame, group='Hips')
    if rotations_error > 0.002:
        raise RuntimeError(f'{name}: rotation transfer error {rotations_error}')
    for curve in action.fcurves:
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
    target.animation_data.action = None
    track = target.animation_data.nla_tracks.new()
    track.name = name
    track.strips.new(name, 1, action)
    track.mute = True
    return action, {'frames': len(frames), 'fps': 30, 'loop': loop,
                    'mapped_bones': len(rest_q) - 1,
                    'maximum_rotation_error_radians': rotations_error,
                    'minimum_skin_z': min(v[2] for v in minima),
                    'maximum_skin_z': max(v[2] for v in maxima),
                    'maximum_airborne_clearance': max(v[2] for v in minima)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    out = args.out.resolve()
    bpy.ops.wm.open_mainfile(filepath=str(out / 'blender/african_warrior_rigged.blend'))
    downloads = out / 'animations/mixamo'
    manifest = json.loads((downloads / 'manifest.json').read_text())
    clips = []
    for record in manifest['clips']:
        runtime_name = record.get('runtime_name')
        if not runtime_name:
            raise RuntimeError(f"Missing runtime_name in manifest entry: {record.get('file')}")
        source = downloads / record['file']
        if not source.is_file():
            raise RuntimeError(f'Missing Mixamo source clip: {source}')
        expected_hash = record.get('sha256')
        if expected_hash and hashlib.sha256(source.read_bytes()).hexdigest() != expected_hash:
            raise RuntimeError(f'Mixamo source checksum mismatch: {source}')
        clips.append((runtime_name, record['file'], bool(record.get('loop', False))))
    if not clips:
        raise RuntimeError('The Mixamo manifest contains no animation clips')

    target = bpy.data.objects['AfricanWarrior_Rig']
    body = bpy.data.objects['AfricanWarrior_Body']
    target.animation_data_clear()
    target.animation_data_create()
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    bpy.context.scene.render.fps = 30
    actions, report = [], {'blender_version': bpy.app.version_string, 'clips': {}}
    for title, filename, loop in clips:
        samples, hip_height = source_samples(downloads / filename, target)
        action, metrics = bake_clip(target, body, title, samples, hip_height, loop)
        actions.append(action)
        report['clips'][title] = metrics
        print('BAKED', title, json.dumps(metrics), flush=True)
    bpy.ops.object.select_all(action='DESELECT')
    target.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = target
    target.animation_data.action = None
    for pb in target.pose.bones:
        pb.matrix_basis.identity()
    bpy.context.view_layer.update()
    spec = importlib.util.spec_from_file_location('iqm_export', str(out / 'blender/iqm_export.py'))
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)
    animspecs = ','.join(f'{title}:1:{report["clips"][title]["frames"]}:30:{int(loop)}' for title, _, loop in clips)
    runtime = out / 'lobster_scene/models/african_warrior.iqm'
    runtime_tmp = runtime.with_name('african_warrior_mixamo.candidate.iqm')
    if runtime_tmp.exists():
        runtime_tmp.unlink()
    exporter.exportIQM(bpy.context, str(runtime_tmp), usemesh=True, usemods=True,
        useskel=True, usebbox=True, usecol=False, scale=1.0, animspecs=animspecs,
        matfun=lambda material, image: material or image or 'warrior')
    if not runtime_tmp.exists() or runtime_tmp.stat().st_size < 1024:
        raise RuntimeError('IQM exporter did not produce a valid-sized file')

    target.animation_data.action = None
    for pb in target.pose.bones:
        pb.matrix_basis.identity()
    bpy.context.view_layer.update()
    for track in target.animation_data.nla_tracks:
        track.mute = False
    glb_tmp = out / 'exports/african_warrior_mixamo.pending.glb'
    fbx_tmp = out / 'exports/african_warrior_mixamo.pending.fbx'
    bpy.ops.export_scene.gltf(filepath=str(glb_tmp),
        export_format='GLB', use_selection=True, export_animations=True,
        export_nla_strips=True, export_force_sampling=True, export_frame_range=False,
        export_skins=True, export_all_influences=False, export_def_bones=False)
    bpy.ops.export_scene.fbx(filepath=str(fbx_tmp),
        use_selection=True, object_types={'MESH', 'ARMATURE'}, add_leaf_bones=False,
        use_armature_deform_only=False, bake_anim=True,
        bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False,
        bake_anim_simplify_factor=0, path_mode='COPY', embed_textures=True,
        axis_forward='-Z', axis_up='Y')
    for track in target.animation_data.nla_tracks:
        track.mute = True

    final_glb = out / 'exports/african_warrior_mixamo.glb'
    final_fbx = out / 'exports/african_warrior_mixamo.fbx'
    if not glb_tmp.is_file() or glb_tmp.stat().st_size < 1024:
        raise RuntimeError('glTF exporter did not produce a valid-sized GLB')
    if not fbx_tmp.is_file() or fbx_tmp.stat().st_size < 1024:
        raise RuntimeError('FBX exporter did not produce a valid-sized file')
    os.replace(glb_tmp, final_glb)
    os.replace(fbx_tmp, final_fbx)

    target.animation_data.action = actions[0]
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = int(actions[0].frame_range[1])
    bpy.context.scene.frame_set(1)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(out / 'blender/african_warrior_mixamo.blend'))
    (downloads / 'retarget-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print('MIXAMO RETARGET AND EXPORT PASSED', flush=True)


if __name__ == '__main__':
    main()

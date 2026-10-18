"""Verify raw downloads, IQM clips, skinned exports and optional pose renders."""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from warrior_common import render_view


def check_skin(body, rig, action):
    rig.animation_data.action = action
    start, end = action.frame_range
    reference, movement = None, 0.0
    low = float('inf')
    for fraction in [0, 0.25, 0.5, 0.75, 1]:
        bpy.context.scene.frame_set(round(start + (end - start) * fraction))
        vertices = [v.co.copy() for v in body.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
        assert all(math.isfinite(v) for p in vertices for v in p)
        if reference is None:
            reference = vertices
        else:
            movement = max(movement, max((a-b).length for a, b in zip(vertices, reference)))
        low = min(low, min(v.z for v in vertices))
    assert movement > 1e-5, f'{action.name}: skin does not animate'
    return {'frames': [start, end], 'maximum_sampled_skin_motion': movement, 'minimum_sampled_skin_z': low}


def imported_action_title(action_name, expected):
    """Normalize Blender's different glTF and FBX action naming conventions."""
    title = action_name.split('|')[-1]
    if title in expected:
        return title
    for candidate in expected:
        if action_name.startswith(candidate + '_'):
            return candidate
    return title


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    out = args.out.resolve()
    report = {}
    downloads = out / 'animations/mixamo'
    manifest = json.loads((downloads / 'manifest.json').read_text())
    clips = {clip['runtime_name']: clip for clip in manifest['clips']}
    assert len(clips) == len(manifest['clips']), 'Runtime animation names must be unique'
    for clip in manifest['clips']:
        source = downloads / clip['file']
        assert source.is_file(), f"Missing Mixamo source: {source}"
        assert hashlib.sha256(source.read_bytes()).hexdigest() == clip['sha256'], f"Source checksum mismatch: {source}"
    report['raw_download_checksums'] = 'passed'
    retarget = json.loads((downloads / 'retarget-validation.json').read_text())
    expected = retarget['clips']
    assert set(expected) == set(clips), 'Retarget report and manifest animation names differ'
    candidate = out / 'lobster_scene/models/african_warrior_mixamo.candidate.iqm'
    data = candidate.read_bytes()
    assert data[:16] == b'INTERQUAKEMODEL\0'
    header = struct.unpack_from('<27I', data, 16)
    assert header[0] == 2 and header[1] == len(data)
    assert header[13] == 23 and header[17] == len(expected)
    assert header[19] == sum(metrics['frames'] for metrics in expected.values())
    text = data[header[4]:header[4] + header[3]]
    animations = {}
    next_frame = 0
    for i in range(header[17]):
        name, first, count, fps, flags = struct.unpack_from('<IIIfI', data, header[18] + i * 20)
        title = text[name:].split(b'\0', 1)[0].decode()
        assert title in expected, f'Unexpected IQM animation: {title}'
        assert first == next_frame, f'{title}: IQM frame range is not contiguous'
        assert count == expected[title]['frames'] and fps == 30
        assert flags == int(bool(clips[title].get('loop', False)))
        animations[title] = {'first': first, 'frames': count, 'fps': fps, 'flags': flags}
        next_frame += count
    assert set(animations) == set(expected)
    report['iqm'] = {'bones': header[13], 'frames': header[19], 'clips': animations}
    bpy.ops.wm.open_mainfile(filepath=str(out / 'blender/african_warrior_mixamo.blend'))
    body = bpy.data.objects['AfricanWarrior_Body']
    rig = bpy.data.objects['AfricanWarrior_Rig']
    assert len(rig.data.bones) == 23
    assert {a.name for a in bpy.data.actions} == set(expected)
    assert all(i.packed_file for i in bpy.data.images if i.type == 'IMAGE')
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    report['blend'] = {}
    for name, metrics in expected.items():
        frames = metrics['frames']
        action = bpy.data.actions[name]
        assert int(action.frame_range[1]) == frames
        report['blend'][name] = check_skin(body, rig, action)
        assert report['blend'][name]['minimum_sampled_skin_z'] > -0.0001
        rest = rig.data.bones['Hips'].head_local
        for frame in [1, frames // 2, frames]:
            bpy.context.scene.frame_set(frame)
            delta = rig.pose.bones['Hips'].matrix.translation - rest
            assert abs(delta.x) < 1e-5 and abs(delta.y) < 1e-5, f'{name}: horizontal root drift'
    if args.render:
        directory = out / 'renders/mixamo'
        directory.mkdir(parents=True, exist_ok=True)
        bpy.context.scene.cycles.samples = 12
        bpy.context.scene.render.resolution_x = 480
        bpy.context.scene.render.resolution_y = 640
        bpy.context.scene.render.resolution_percentage = 100
        camera = bpy.context.scene.camera
        for name, frame in [('Idle', 1), ('Walk', 9), ('Run', 6), ('Jump', 32),
                            ('PunchCombo', 20), ('SideKick', 20), ('Death', 30), ('PickUp', 20)]:
            rig.animation_data.action = bpy.data.actions[name]
            bpy.context.scene.frame_set(frame)
            render_view(camera, directory / (name.lower() + '.png'), angle=25, center=(0, 0, 1.1), scale=2.7)
    for suffix in ['glb', 'fbx']:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        path = out / 'exports' / ('african_warrior_mixamo.' + suffix)
        if suffix == 'glb':
            bpy.ops.import_scene.gltf(filepath=str(path))
        else:
            bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True)
        rigs = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
        bodies = [o for o in bpy.context.scene.objects if o.type == 'MESH']
        assert len(rigs) == 1 and len(bodies) == 1
        rig, body = rigs[0], bodies[0]
        assert len(rig.data.bones) == 23 and body.data.uv_layers
        assert any(m.type == 'ARMATURE' and m.object == rig for m in body.modifiers)
        assert len(bpy.data.actions) == len(expected)
        assert {imported_action_title(a.name, expected) for a in bpy.data.actions} == set(expected)
        for track in rig.animation_data.nla_tracks:
            track.mute = True
        report[suffix + '_round_trip'] = {a.name: check_skin(body, rig, a) for a in bpy.data.actions}
    (downloads / 'export-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    runtime = out / 'lobster_scene/models/african_warrior.iqm'
    os.replace(candidate, runtime)
    print('MIXAMO VALIDATION PASSED', json.dumps(report), flush=True)


if __name__ == '__main__':
    main()

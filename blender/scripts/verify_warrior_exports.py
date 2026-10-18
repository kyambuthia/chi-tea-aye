"""Round-trip both game exports in fresh Blender scenes and verify skin motion."""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import bpy


def glb_manifest(path):
    with path.open('rb') as source:
        magic, version, length = struct.unpack('<4sII', source.read(12))
        assert magic == b'glTF' and version == 2 and length == path.stat().st_size
        chunk_size, chunk_type = struct.unpack('<II', source.read(8))
        assert chunk_type == 0x4E4F534A
        manifest = json.loads(source.read(chunk_size))
    assert len(manifest.get('skins', [])) == 1
    assert {a['name'] for a in manifest.get('animations', [])} == {'Idle', 'Walk', 'Run'}
    primitives = [p for mesh in manifest['meshes'] for p in mesh['primitives']]
    assert all('JOINTS_0' in p['attributes'] and 'WEIGHTS_0' in p['attributes'] for p in primitives)
    assert all('TEXCOORD_0' in p['attributes'] for p in primitives)
    assert all('bufferView' in image for image in manifest.get('images', [])), 'Textures must be embedded'
    assert len(manifest.get('images', [])) >= 1
    return {'animations': [a['name'] for a in manifest['animations']],
            'joints': len(manifest['skins'][0]['joints']),
            'embedded_images': len(manifest['images'])}


def round_trip(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    if path.suffix == '.glb':
        bpy.ops.import_scene.gltf(filepath=str(path))
    else:
        bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True)
    rigs = [obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE']
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    assert len(rigs) == 1 and meshes
    rig = rigs[0]
    assert len(rig.data.bones) >= 23
    assert all(any(mod.type == 'ARMATURE' and mod.object == rig for mod in mesh.modifiers)
               for mesh in meshes)
    actions = list(bpy.data.actions)
    assert len(actions) >= 3, 'Missing animation takes'
    if rig.animation_data:
        for track in rig.animation_data.nla_tracks:
            track.mute = True
    rig.animation_data_create()
    spans = {}
    for action in actions:
        rig.animation_data.action = action
        bone = rig.pose.bones['LeftArm']
        start, end = action.frame_range
        bpy.context.scene.frame_set(int(start))
        p1 = bone.matrix.copy()
        initial = [[vertex.co.copy() for vertex in mesh.evaluated_get(
            bpy.context.evaluated_depsgraph_get()).data.vertices] for mesh in meshes]
        bpy.context.scene.frame_set(int(start+(end-start)*0.25))
        p2 = bone.matrix.copy()
        difference = sum(abs(p1[i][j]-p2[i][j]) for i in range(4) for j in range(4))
        assert difference > 1e-5, f'Animation {action.name} does not move LeftArm'
        displacement = max((vertex.co-initial[index][i]).length
            for index, mesh in enumerate(meshes)
            for i, vertex in enumerate(mesh.evaluated_get(
                bpy.context.evaluated_depsgraph_get()).data.vertices))
        assert displacement > 1e-5, f'Animation {action.name} does not deform the exported skin'
        spans[action.name] = {'frames': [start, end], 'arm_matrix_change': difference,
                              'skin_maximum_displacement': displacement}
    for mesh in meshes:
        assert mesh.data.uv_layers
        assert mesh.data.materials
        evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        assert all(math.isfinite(c) for vertex in evaluated.data.vertices for c in vertex.co)
    images = [node.image for mesh in meshes for material in mesh.data.materials
              if material and material.use_nodes for node in material.node_tree.nodes
              if node.type == 'TEX_IMAGE' and node.image]
    assert len(images) >= 2 and all(image.size[0] > 0 for image in images)
    return {'bones': len(rig.data.bones), 'meshes': len(meshes), 'actions': spans,
            'loaded_texture_nodes': len(images)}


def verify_source(path):
    bpy.ops.wm.open_mainfile(filepath=str(path))
    rig = bpy.data.objects['AfricanWarrior_Rig']
    assert len(rig.data.bones) == 23
    assert rig.animation_data.action.name == 'Idle'
    assert {a.name for a in bpy.data.actions} == {'Idle', 'Walk', 'Run'}
    images = [image for image in bpy.data.images if image.type == 'IMAGE']
    assert len(images) >= 3 and all(image.packed_file is not None for image in images)
    return {'blender_version': bpy.app.version_string, 'packed_images': len(images),
            'active_action': rig.animation_data.action.name}


parser = argparse.ArgumentParser()
parser.add_argument('--out', required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
out = Path(args.out).resolve()
report = {'glb_manifest': glb_manifest(out / 'exports' / 'african_warrior.glb')}
for suffix in ['glb', 'fbx']:
    report[suffix+'_round_trip'] = round_trip(out / 'exports' / ('african_warrior.'+suffix))
report['blend_source'] = verify_source(out / 'blender' / 'african_warrior_rigged.blend')
(out / 'export-validation.json').write_text(json.dumps(report, indent=2))
print('EXPORT VALIDATION PASSED', json.dumps(report), flush=True)

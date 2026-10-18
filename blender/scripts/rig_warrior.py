"""Build, skin, animate and export the warrior using Blender 3.0.

Landmarks are measured in metres after import normalization (Z up, front -Y).
This is a body FK rig; facial and individual finger controls are out of scope.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from warrior_common import bounds, import_character, render_view, setup_studio


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesh', required=True)
    parser.add_argument('--landmarks', required=True)
    parser.add_argument('--out', required=True)
    return parser.parse_args(sys.argv[sys.argv.index('--')+1:])


def build_rig(landmarks):
    armature = bpy.data.armatures.new('AfricanWarrior_Skeleton')
    rig = bpy.data.objects.new('AfricanWarrior_Rig', armature)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    rig.show_in_front = True
    rig.data.display_type = 'OCTAHEDRAL'
    bpy.ops.object.mode_set(mode='EDIT')
    def bone(name, start, end, parent=None, deform=True):
        b = armature.edit_bones.new(name)
        b.head, b.tail = start, end
        b.use_deform = deform
        if parent:
            b.parent = armature.edit_bones[parent]
            b.use_connect = (b.parent.tail-b.head).length < 0.0001
        return b
    p = {k: Vector(v) for k, v in landmarks['center'].items()}
    bone('Root', (0, 0, 0), (0, 0, 0.15), deform=False)
    bone('Hips', p['hips'], p['spine'], 'Root')
    bone('Spine', p['spine'], p['spine1'], 'Hips')
    bone('Spine1', p['spine1'], p['chest'], 'Spine')
    bone('Spine2', p['chest'], p['neck'], 'Spine1')
    bone('Neck', p['neck'], p['head'], 'Spine2')
    bone('Head', p['head'], p['crown'], 'Neck')
    for side, sign in [('Left', 1), ('Right', -1)]:
        q = {k: Vector((sign*v[0], v[1], v[2])) for k, v in landmarks['left'].items()}
        bone(side+'Shoulder', p['chest'], q['shoulder'], 'Spine2')
        bone(side+'Arm', q['shoulder'], q['elbow'], side+'Shoulder')
        bone(side+'ForeArm', q['elbow'], q['wrist'], side+'Arm')
        bone(side+'Hand', q['wrist'], q['hand'], side+'ForeArm')
        bone(side+'UpLeg', q['hip'], q['knee'], 'Hips')
        bone(side+'Leg', q['knee'], q['ankle'], side+'UpLeg')
        bone(side+'Foot', q['ankle'], q['ball'], side+'Leg')
        bone(side+'ToeBase', q['ball'], q['toe'], side+'Foot')
    bpy.ops.object.mode_set(mode='OBJECT')
    rig['forward_axis'] = '-Y'
    rig['height_metres'] = 1.85
    rig['rig_type'] = 'FK body rig, 22 deform bones plus Root'
    rig['LeftHand_socket'] = 'Attach equipment to LeftHand pose bone'
    rig['RightHand_socket'] = 'Attach equipment to RightHand pose bone'
    rig['animation_note'] = 'Procedural in-place starter cycles; polish foot contacts in your engine'
    return rig


def skin(body, rig):
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    deform = {b.name for b in rig.data.bones if b.use_deform}
    ids = {g.index: g.name for g in body.vertex_groups if g.name in deform}
    missing = 0
    for vertex in body.data.vertices:
        weights = sorted([(ids[g.group], g.weight) for g in vertex.groups
                          if g.group in ids and g.weight > 1e-8], key=lambda item: -item[1])
        if not weights:
            missing += 1
    if missing:
        raise RuntimeError(f'Automatic skinning left {missing} vertices unweighted; inspect mesh before continuing')
    # Exactly four normalized influences keep skinning portable across game importers.
    for vertex in body.data.vertices:
        weights = sorted([(ids[g.group], g.weight) for g in vertex.groups
                          if g.group in ids and g.weight > 1e-8], key=lambda item: -item[1])[:4]
        total = sum(w for _, w in weights)
        for group in body.vertex_groups:
            group.remove([vertex.index])
        for name, weight in weights:
            body.vertex_groups[name].add([vertex.index], weight/total, 'REPLACE')
    return 'Blender automatic bone-heat weights; limited to four normalized influences'


def reset_pose(rig):
    for bone in rig.pose.bones:
        bone.rotation_mode = 'QUATERNION'
        bone.rotation_quaternion = (1, 0, 0, 0)
        bone.location = (0, 0, 0)
        bone.scale = (1, 1, 1)


def rotate(rig, name, rotations):
    bone = rig.pose.bones[name]
    q = Quaternion((1, 0, 0, 0))
    inverse = bone.bone.matrix_local.to_3x3().inverted()
    for axis, degrees in rotations:
        q = q @ Quaternion((inverse @ Vector(axis)).normalized(), math.radians(degrees))
    bone.rotation_quaternion = q


def pose_cycle(rig, kind, phase):
    reset_pose(rig)
    wave = math.sin(phase)
    for side, sign in [('Left', 1), ('Right', -1)]:
        if kind == 'Idle':
            arm_swing = 1.5*wave
            thigh = 0
            knee = 1.5
        else:
            amplitude = 23 if kind == 'Walk' else 40
            thigh = sign*amplitude*wave
            knee = 5 + (28 if kind == 'Walk' else 70)*max(0, sign*math.cos(phase))
            arm_swing = -sign*(18 if kind == 'Walk' else 32)*wave
        rotate(rig, side+'Arm', [((1, 0, 0), arm_swing), ((0, 1, 0), sign*80)])
        rotate(rig, side+'ForeArm', [((0, 0, 1), -sign*(12 if kind != 'Run' else 65))])
        rotate(rig, side+'UpLeg', [((1, 0, 0), thigh)])
        rotate(rig, side+'Leg', [((1, 0, 0), knee)])
        rotate(rig, side+'Foot', [((1, 0, 0), -thigh-knee)])
    rotate(rig, 'Spine1', [((0, 0, 1), 2*wave if kind != 'Idle' else 0)])
    rotate(rig, 'Spine2', [((1, 0, 0), -5 if kind == 'Run' else 0)])
    # Hips bone's local Y is up, since the spine chain is vertical.
    rig.pose.bones['Hips'].location.y = (0.008*math.sin(phase) if kind == 'Idle'
                                         else -0.025+0.015*math.cos(2*phase))


def animations(rig):
    scene = bpy.context.scene
    scene.render.fps = 30
    rig.animation_data_create()
    created = []
    for name, frames in [('Idle', 61), ('Walk', 49), ('Run', 33)]:
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        rig.animation_data.action = action
        for frame in range(1, frames+1, 2):
            phase = 2*math.pi*(frame-1)/(frames-1)
            pose_cycle(rig, name, phase)
            for bone in rig.pose.bones:
                bone.keyframe_insert(data_path='rotation_quaternion', frame=frame, group=bone.name)
                bone.keyframe_insert(data_path='location', frame=frame, group=bone.name)
        for curve in action.fcurves:
            for point in curve.keyframe_points:
                point.interpolation = 'LINEAR'
        created.append(action)
    rig.animation_data.action = None
    for action in created:
        track = rig.animation_data.nla_tracks.new()
        track.name = action.name
        track.strips.new(action.name, 1, action)
        track.mute = True
    return created


def validate(body, rig, out):
    deform = {b.name for b in rig.data.bones if b.use_deform}
    names = {g.index: g.name for g in body.vertex_groups}
    unweighted, max_influences, max_error = 0, 0, 0
    for vertex in body.data.vertices:
        weights = [g.weight for g in vertex.groups if names[g.group] in deform and g.weight > 1e-8]
        unweighted += not weights
        max_influences = max(max_influences, len(weights))
        max_error = max(max_error, abs(sum(weights)-1))
    assert unweighted == 0 and max_influences <= 4 and max_error < 1e-5
    reset_pose(rig)
    bpy.context.view_layer.update()
    rest = [v.co.copy() for v in body.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
    rotate(rig, 'LeftArm', [((0, 1, 0), 65)])
    rotate(rig, 'RightUpLeg', [((1, 0, 0), -35)])
    rotate(rig, 'RightLeg', [((1, 0, 0), 65)])
    bpy.context.view_layer.update()
    moved = body.evaluated_get(bpy.context.evaluated_depsgraph_get()).data
    distances = [(vertex.co-rest[i]).length for i, vertex in enumerate(moved.vertices)]
    assert all(math.isfinite(c) for vertex in moved.vertices for c in vertex.co)
    assert max(distances) > 0.1, 'Pose does not deform mesh'
    report = {
        'blender_version': bpy.app.version_string,
        'height_metres': 1.85,
        'vertices': len(body.data.vertices),
        'triangles': sum(len(p.vertices)-2 for p in body.data.polygons),
        'bones': len(rig.data.bones),
        'deform_bones': len(deform),
        'unweighted_vertices': unweighted,
        'maximum_influences': max_influences,
        'maximum_weight_sum_error': max_error,
        'pose_moved_vertices': sum(d > 0.001 for d in distances),
        'pose_maximum_displacement_metres': max(distances),
        'uv_layers': len(body.data.uv_layers),
        'materials': len(body.data.materials),
        'limitations': ['Body FK rig; no facial or individual finger rig',
                        'Starter procedural in-place animations require gameplay polish',
                        'Woven wrap is skinned with the body; no cloth simulation',
                        'Generated topology is not hand-retopologized'],
    }
    (out / 'validation.json').write_text(json.dumps(report, indent=2))
    print('VALIDATION', json.dumps(report), flush=True)
    reset_pose(rig)
    return report


def export(body, rig, out):
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    rig.animation_data.action = None
    reset_pose(rig)
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    bpy.ops.export_scene.gltf(filepath=str(out / 'exports' / 'african_warrior.glb'),
        export_format='GLB', use_selection=True, export_animations=True,
        export_nla_strips=True, export_force_sampling=True, export_frame_range=False,
        export_skins=True, export_all_influences=False, export_def_bones=False)
    # Separate NLA tracks export as distinct FBX takes; all-actions off avoids duplicates.
    for track in rig.animation_data.nla_tracks:
        track.mute = False
    bpy.ops.export_scene.fbx(filepath=str(out / 'exports' / 'african_warrior.fbx'),
        use_selection=True, object_types={'MESH', 'ARMATURE'},
        add_leaf_bones=False, use_armature_deform_only=False,
        bake_anim=True, bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False,
        bake_anim_simplify_factor=0, path_mode='COPY', embed_textures=True,
        axis_forward='-Z', axis_up='Y')
    for track in rig.animation_data.nla_tracks:
        track.mute = True


def main():
    args = arguments()
    out = Path(args.out).resolve()
    for directory in ['blender', 'exports', 'renders']:
        (out / directory).mkdir(parents=True, exist_ok=True)
    landmarks = json.loads(Path(args.landmarks).read_text())
    body = import_character(args.mesh, turn=landmarks.get('turn_degrees', 0))
    body.select_set(False)
    rig = build_rig(landmarks)
    method = skin(body, rig)
    print('SKINNING', method, flush=True)
    validate(body, rig, out)
    camera = setup_studio()
    # T pose and a representative bent-limb pose expose skinning defects.
    render_view(camera, out / 'renders' / 't-pose.png', 0)
    actions = animations(rig)
    rig.animation_data.action = actions[0]
    bpy.context.scene.frame_set(1)
    render_view(camera, out / 'renders' / 'idle-front.png', 0, scale=2.15)
    render_view(camera, out / 'renders' / 'idle-threequarter.png', 32, scale=2.15)
    rig.animation_data.action = actions[1]
    bpy.context.scene.frame_set(13)
    render_view(camera, out / 'renders' / 'walk-check.png', 45, scale=2.15)
    rig.animation_data.action = None
    reset_pose(rig)
    export(body, rig, out)
    rig.animation_data.action = actions[0]
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 61
    bpy.context.scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    # Embedded images make the source .blend portable.
    bpy.ops.file.pack_all()
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_distance = 3.4
                area.spaces.active.region_3d.view_location = (0, 0, 1)
                area.spaces.active.shading.type = 'MATERIAL'
    bpy.ops.wm.save_as_mainfile(filepath=str(out / 'blender' / 'african_warrior_rigged.blend'))
    print('SAVED rigged Blender file and FBX/GLB exports', flush=True)


if __name__ == '__main__':
    main()

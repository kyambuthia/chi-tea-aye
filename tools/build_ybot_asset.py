import bpy
import os

bpy.ops.wm.read_factory_settings(use_empty=True)

# 1. Open african_warrior_rigged.blend to get the canonical 25-bone rig and bone rest poses
rig_blend = 'characters/african_warrior/blender/african_warrior_rigged.blend'
bpy.ops.wm.open_mainfile(filepath=rig_blend)

rig = bpy.data.objects['AfricanWarrior_Rig']
warrior_body = bpy.data.objects['AfricanWarrior_Body']

# Remove warrior body
bpy.data.objects.remove(warrior_body, do_unlink=True)

# Clear any existing actions from the blend file
for act in list(bpy.data.actions):
    bpy.data.actions.remove(act)

# 2. Import Y Bot.fbx
fbx_path = '/home/mbuthi/Projects/blndr/Y Bot.fbx'
bpy.ops.import_scene.fbx(filepath=fbx_path)
joints_obj = bpy.data.objects['Alpha_Joints']
surface_obj = bpy.data.objects['Alpha_Surface']
imported_arm = bpy.data.objects['Armature']

# Transform vertices to meters, Z-up using the imported world matrix
for obj in [joints_obj, surface_obj]:
    obj.data.transform(obj.matrix_world)
    obj.parent = None
    obj.matrix_world.identity()
    for m in list(obj.modifiers):
        obj.modifiers.remove(m)

# Scale slightly to match the 1.85m warrior rig height
scale_factor = 1.85 / 1.8046
for obj in [joints_obj, surface_obj]:
    for v in obj.data.vertices:
        v.co *= scale_factor

# Remove Mixamo armature
bpy.data.objects.remove(imported_arm, do_unlink=True)
for o in list(bpy.data.objects):
    if o not in [rig, joints_obj, surface_obj]:
        bpy.data.objects.remove(o, do_unlink=True)

# 3. Create single 1024x1024 texture for Y-Bot (Teal on left, Graphite on right)
img = bpy.data.images.new("YBot_BaseColor", width=1024, height=1024, alpha=True)
pixels = []
for y in range(1024):
    for x in range(1024):
        if x < 512:
            pixels.extend([0.098, 0.423, 0.521, 1.0])  # Teal
        else:
            pixels.extend([0.153, 0.212, 0.224, 1.0])  # Graphite
img.pixels = pixels
img.pack()

mat = bpy.data.materials.new(name="YBot_Mat")
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get("Principled BSDF")
tex_node = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex_node.image = img
mat.node_tree.links.new(tex_node.outputs['Color'], bsdf.inputs['Base Color'])

# Remap UVs
for d in surface_obj.data.uv_layers['map1'].data:
    d.uv[0] = d.uv[0] * 0.48 + 0.01
    d.uv[1] = d.uv[1] * 0.98 + 0.01

for d in joints_obj.data.uv_layers['map1'].data:
    d.uv[0] = 0.5 + d.uv[0] * 0.48 + 0.01
    d.uv[1] = d.uv[1] * 0.98 + 0.01

# Join meshes into one single mesh
bpy.ops.object.select_all(action='DESELECT')
surface_obj.select_set(True)
joints_obj.select_set(True)
bpy.context.view_layer.objects.active = surface_obj
bpy.ops.object.join()
ybot_mesh = surface_obj
ybot_mesh.name = "YBot_Body"

# Assign single material
ybot_mesh.data.materials.clear()
ybot_mesh.data.materials.append(mat)

# 4. Remap Mixamo vertex groups directly to AfricanWarrior_Rig bone hierarchy
rig_bones = {b.name for b in rig.data.bones}
for vg in list(ybot_mesh.vertex_groups):
    clean = vg.name.replace('mixamorig:', '')
    if clean in rig_bones:
        vg.name = clean
    elif any(f in clean for f in ['Thumb', 'Index', 'Middle', 'Ring', 'Pinky']):
        target = 'LeftHand' if 'Left' in clean else 'RightHand'
        if target not in ybot_mesh.vertex_groups:
            ybot_mesh.vertex_groups.new(name=target)
        target_group = ybot_mesh.vertex_groups[target]
        for v in ybot_mesh.data.vertices:
            for g in v.groups:
                if g.group == vg.index:
                    target_group.add([v.index], g.weight, 'ADD')
        ybot_mesh.vertex_groups.remove(vg)
    else:
        ybot_mesh.vertex_groups.remove(vg)

# Normalize influences to max 4 influences per vertex
ids = {g.index: g.name for g in ybot_mesh.vertex_groups if g.name in rig_bones}
for v in ybot_mesh.data.vertices:
    weights = sorted([(g.group, g.weight) for g in v.groups if g.group in ids and g.weight > 1e-6], key=lambda x: -x[1])[:4]
    tot = sum(w for _, w in weights)
    for g in list(v.groups):
        ybot_mesh.vertex_groups[g.group].remove([v.index])
    if tot > 0:
        for g_idx, w in weights:
            ybot_mesh.vertex_groups[g_idx].add([v.index], w / tot, 'REPLACE')

# Bind to rig with Armature modifier
ybot_mesh.parent = rig
mod = ybot_mesh.modifiers.new('Armature', 'ARMATURE')
mod.object = rig

print("Mesh rigged to canonical rig. Loading animations from african_warrior.glb...")
# Load actions from african_warrior.glb
before_acts = set(bpy.data.actions)
bpy.ops.import_scene.gltf(filepath='assets/characters/african_warrior.glb')
imported_acts = set(bpy.data.actions) - before_acts
for o in list(bpy.data.objects):
    if o not in [rig, ybot_mesh]:
        bpy.data.objects.remove(o, do_unlink=True)

# Assign actions to NLA with exact clean names
rig.animation_data_clear()
rig.animation_data_create()
for act in imported_acts:
    clean_name = act.name.split('.')[0].replace('_AfricanWarrior_Rig', '')
    act.name = clean_name
    track = rig.animation_data.nla_tracks.new()
    track.name = clean_name
    strip = track.strips.new(clean_name, int(act.frame_range[0]), act)
    strip.name = clean_name

# Reset pose and mute NLA tracks for clean rest-pose mesh and IBM export
rig.animation_data.action = None
for bone in rig.pose.bones:
    bone.rotation_mode = 'QUATERNION'
    bone.rotation_quaternion = (1, 0, 0, 0)
    bone.location = (0, 0, 0)
    bone.scale = (1, 1, 1)

for track in rig.animation_data.nla_tracks:
    track.mute = True

out_path = 'assets/characters/y_bot.glb'
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
ybot_mesh.select_set(True)
bpy.context.view_layer.objects.active = rig

bpy.ops.export_scene.gltf(
    filepath=out_path,
    export_format='GLB',
    use_selection=True,
    export_animations=True,
    export_nla_strips=True,
    export_force_sampling=True,
    export_frame_range=False,
    export_skins=True,
    export_all_influences=False,
    export_def_bones=False
)
print("SUCCESSFULLY EXPORTED Y-BOT GLB!")

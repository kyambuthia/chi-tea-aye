"""Shared Blender 3.0 helpers for the isolated warrior asset pipeline."""
import math
from pathlib import Path

import bpy
from mathutils import Vector


def bounds(obj):
    points = [obj.matrix_world @ Vector(p) for p in obj.bound_box]
    return (Vector(tuple(min(p[i] for p in points) for i in range(3))),
            Vector(tuple(max(p[i] for p in points) for i in range(3))))


def import_character(path, height=1.85, turn=0):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(Path(path).resolve()))
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if not meshes:
        raise RuntimeError('No meshes imported')
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    for other in list(bpy.context.scene.objects):
        if other != obj:
            bpy.data.objects.remove(other, do_unlink=True)
    obj.rotation_euler.z += math.radians(turn)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    low, high = bounds(obj)
    obj.scale *= height / (high.z - low.z)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    low, high = bounds(obj)
    obj.location = (-(low.x+high.x)/2, -(low.y+high.y)/2, -low.z)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # GLB splits vertices at UV/normal seams. Welding coincident geometry restores
    # connectivity for bone-heat skinning; UVs are per-loop and remain intact.
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.00001)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.name = 'AfricanWarrior_Body'
    for face in obj.data.polygons:
        face.use_smooth = True
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.unit_settings.scale_length = 1
    return obj


def setup_studio():
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 900
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'Medium High Contrast'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    scene.world = bpy.data.worlds.new('StudioWorld')
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (0.055, 0.065, 0.085, 1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value = 0.55
    studio = bpy.data.collections.new('PreviewStudio')
    scene.collection.children.link(studio)
    for name, position, energy, size in [
        ('Key', (3, -4, 5), 500, 4),
        ('Fill', (-3, -1, 3), 280, 3),
        ('Rim', (1, 3, 4), 650, 3),
    ]:
        data = bpy.data.lights.new(name, 'AREA')
        data.energy, data.size = energy, size
        light = bpy.data.objects.new(name, data)
        studio.objects.link(light)
        light.location = position
        light.rotation_euler = (Vector((0, 0, 1))-light.location).to_track_quat('-Z', 'Y').to_euler()
    data = bpy.data.cameras.new('PreviewCamera')
    data.type = 'ORTHO'
    data.ortho_scale = 2.6
    camera = bpy.data.objects.new('PreviewCamera', data)
    studio.objects.link(camera)
    scene.camera = camera
    return camera


def render_view(camera, path, angle=0, center=(0, 0, 0.95), scale=2.6):
    angle = math.radians(angle)
    target = Vector(center)
    camera.location = target + Vector((4*math.sin(angle), -4*math.cos(angle), 0.12))
    camera.rotation_euler = (target-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = scale
    bpy.context.scene.render.filepath = str(Path(path).resolve())
    bpy.ops.render.render(write_still=True)

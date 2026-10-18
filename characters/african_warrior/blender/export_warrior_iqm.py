"""Export the existing Blender 3.0 character to Lobster's animated IQM format."""

import argparse
import importlib.util
import sys
from pathlib import Path

import bpy


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exporter", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])

    exporter_path = Path(args.exporter).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    spec = importlib.util.spec_from_file_location("iqm_export", str(exporter_path))
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)

    mesh = bpy.data.objects["AfricanWarrior_Body"]
    rig = bpy.data.objects["AfricanWarrior_Rig"]
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    mesh.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.context.scene.render.fps = 30

    exporter.exportIQM(
        bpy.context,
        str(output_path),
        usemesh=True,
        usemods=True,
        useskel=True,
        usebbox=True,
        usecol=False,
        scale=1.0,
        animspecs="Idle:1:61:30:1,Walk:1:49:30:1,Run:1:33:30:1",
        matfun=lambda material, image: material or image or "warrior",
    )

    if not output_path.exists() or output_path.stat().st_size < 1024:
        raise RuntimeError("IQM exporter did not produce a valid-sized file")
    print("WARRIOR IQM EXPORT PASSED", output_path, output_path.stat().st_size)


if __name__ == "__main__":
    main()

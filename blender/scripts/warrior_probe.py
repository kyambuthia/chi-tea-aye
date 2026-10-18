"""Import and inspect the actual generated character before placing its bones."""
import argparse
import json
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from warrior_common import bounds, import_character, render_view, setup_studio

parser = argparse.ArgumentParser()
parser.add_argument('--mesh', required=True)
parser.add_argument('--out', required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
out = Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=True)
obj = import_character(args.mesh)
low, high = bounds(obj)
report = {'blender': bpy.app.version_string, 'vertices': len(obj.data.vertices),
          'triangles': sum(len(p.vertices)-2 for p in obj.data.polygons),
          'bounds': [list(low), list(high)], 'materials': [m.name for m in obj.data.materials],
          'uv_layers': len(obj.data.uv_layers), 'slices': {}}
for height in [0.1, 0.3, 0.5, 0.8, 0.95, 1.1, 1.25, 1.4, 1.5, 1.6, 1.75]:
    points = [v.co for v in obj.data.vertices if abs(v.co.z-height)<0.015]
    if points:
        report['slices'][str(height)] = {axis: [min(p[i] for p in points), max(p[i] for p in points)]
                                         for i, axis in enumerate('xyz')}
(out / 'mesh-inspection.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2), flush=True)
camera = setup_studio()
for name, angle in [('front', 0), ('side', 90), ('back', 180)]:
    render_view(camera, out / ('raw-'+name+'.png'), angle)
bpy.ops.wm.save_as_mainfile(filepath=str(out / 'inspection.blend'))

"""Shrink a skinned glTF character for the web build.

Keeps the skeleton, animations, and mesh; drops the normal, metallic, and roughness
maps the game does not read; and re-encodes the base-color texture at 1024 px.

Usage: python3 scripts/slim_glb.py <input.glb> <output.glb>
"""
import io, json, struct, sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
json_len = struct.unpack('<I', raw[12:16])[0]
gltf = json.loads(raw[20:20 + json_len])
bin_off = 20 + json_len
bin_len = struct.unpack('<I', raw[bin_off:bin_off + 4])[0]
bin_data = raw[bin_off + 8:bin_off + 8 + bin_len]

# Only the base-color texture is read by the game.
mat = gltf['materials'][0]
pbr = mat['pbrMetallicRoughness']
bc_tex = pbr['baseColorTexture']['index']
bc_img = gltf['textures'][bc_tex]['source']
for key in ('normalTexture', 'occlusionTexture', 'emissiveTexture'):
    mat.pop(key, None)
pbr.pop('metallicRoughnessTexture', None)

# Re-encode the base color at 1024 px.
view = gltf['bufferViews'][gltf['images'][bc_img]['bufferView']]
img = Image.open(io.BytesIO(bin_data[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']])).convert('RGBA')
img = img.resize((1024, 1024), Image.LANCZOS)
buf = io.BytesIO()
img.save(buf, format='PNG', optimize=True)
new_png = buf.getvalue()

# Keep every buffer view that accessors use, plus the new base-color image.
used = set()
for acc in gltf['accessors']:
    if 'bufferView' in acc:
        used.add(acc['bufferView'])
    if 'sparse' in acc:
        raise SystemExit('sparse accessors not handled')

new_views, new_bin = [], bytearray()
remap = {}
def copy_view(old_index, payload=None):
    v = dict(gltf['bufferViews'][old_index])
    data = payload if payload is not None else bin_data[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
    while len(new_bin) % 4:
        new_bin.append(0)
    v['byteOffset'] = len(new_bin)
    v['byteLength'] = len(data)
    new_bin.extend(data)
    remap[old_index] = len(new_views)
    new_views.append(v)
    return len(new_views) - 1

for old in sorted(used):
    copy_view(old)
for acc in gltf['accessors']:
    if 'bufferView' in acc:
        acc['bufferView'] = remap[acc['bufferView']]

new_img_view = copy_view(gltf['images'][bc_img]['bufferView'], new_png)
gltf['images'] = [{'bufferView': new_img_view, 'mimeType': 'image/png', 'name': 'base_color'}]
gltf['textures'] = [{'sampler': 0, 'source': 0}]
gltf['materials'][0]['pbrMetallicRoughness']['baseColorTexture'] = {'index': 0}
gltf['bufferViews'] = new_views
gltf['buffers'][0]['byteLength'] = len(new_bin)

js = json.dumps(gltf, separators=(',', ':')).encode()
js += b' ' * ((4 - len(js) % 4) % 4)
bin_pad = bytes(new_bin) + b'\0' * ((4 - len(new_bin) % 4) % 4)
out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_pad))
out += struct.pack('<II', len(js), 0x4E4F534A) + js
out += struct.pack('<II', len(bin_pad), 0x004E4942) + bin_pad
open(dst, 'wb').write(out)
print('written', len(out), 'bytes; base color png', len(new_png))

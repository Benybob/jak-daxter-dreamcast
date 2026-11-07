"""Bundle skinned merc models for the level ELF's model pass (AI-assisted), ISC.

Reads the decompiler's skinned GLB exports (one skinned mesh each, named after
the original merc-ctrl, e.g. eichar-lod0) and writes one DSKB bundle. The
runtime (level_models.c) skins each model with the original game's joint
matrices, bind-pose times bone transform, read from GOAL memory every frame,
so nothing here is animation: only bind-space geometry, textures and the
GLB's inverse bind matrices, which the runtime compares with the game's own
joint bind-poses to prove the joint numbering matches.

Output is PRIVATE game-derived data: write it under build/ (git-ignored).

DSKB: 'DSKB', u32 version 1, u32 count, u32 reserved; count 48-byte entries
(char name[32] NUL padded, u32 offset, u32 bytes, u32 crc32, u32 reserved);
then each model, 32-byte aligned:
  'DSK1', u32 version 1, joints, vertices, indices, batches, textures,
  texture bytes, float centre x, y, z, scale (48 bytes);
  joints x 12 floats: the inverse bind matrix in the game's row-vector layout
  (rows 0-2 of the 3x3, then the translation in meters);
  vertices x 20 bytes: s16 x, y, z (centre + q * scale, meters, bind space),
  s16 u, v (1/1024), u8 joint[3], u8 weight[2] (the third is 255 minus both),
  u8 0, u32 ARGB8888;
  u16 indices, three a triangle, padded to 4 bytes;
  batches x 12 bytes: u32 first index, u32 index count, u16 texture (0xffff
  none), u16 flags (bit 0 punch-through, bit 1 translucent);
  textures: u16 width, u16 height, u32 format (1 ARGB1555, 3 ARGB4444, both
  twiddled), u32 bytes, pixels; each record 4-byte aligned.
"""
import argparse
import base64
import io
import json
import struct
import zlib
from pathlib import Path

try:  # --help works without them; converting needs both
    import numpy as np
    from PIL import Image
except ImportError:
    np = Image = None

from convert_scene import read_glb, accessor, node_matrix, WIDTHS
from convert_textured import twiddle

WIDTHS['MAT4'] = 16
TEXTURE_CAP = 128
MAX_VERTICES = 8192
MAX_JOINTS = 255


def image_rgba(doc, binary, image_id):
    if image_id is None:
        return Image.new('RGBA', (8, 8), (255, 255, 255, 255))
    info = doc['images'][image_id]
    if 'bufferView' in info:
        view = doc['bufferViews'][info['bufferView']]
        start = view.get('byteOffset', 0)
        raw = binary[start:start+view['byteLength']]
    elif info.get('uri', '').startswith(('data:image/png;base64,', 'data:image/jpeg;base64,')):
        raw = base64.b64decode(info['uri'].split(',', 1)[1], validate=True)
    else:
        raise ValueError('only embedded texture images are supported')
    image = Image.open(io.BytesIO(raw))
    if max(image.size) > 4096:
        raise ValueError('source image too large')
    return image.convert('RGBA')


def texture(doc, binary, image_id, mode, cutoff):
    image = image_rgba(doc, binary, image_id)
    dims = tuple(max(8, min(TEXTURE_CAP, 1 << (v.bit_length()-1))) for v in image.size)
    image = image.resize(dims, Image.Resampling.LANCZOS)
    width, height = dims
    out = bytearray(width*height*2)
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if mode == 'BLEND':
                value = (a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | b >> 4
            else:
                opaque = mode == 'OPAQUE' or a/255 >= cutoff
                value = int(opaque) << 15 | (r >> 3) << 10 | (g >> 3) << 5 | b >> 3
            struct.pack_into('<H', out, twiddle(x, y, width, height)*2, value)
    return width, height, 3 if mode == 'BLEND' else 1, bytes(out)


def top3(joints, weights):
    order = np.argsort(-weights, kind='stable')[:3]
    j = joints[order].astype(int)
    w = weights[order].astype(float)
    total = w.sum()
    if not total > 0:
        raise ValueError('vertex without skin weight')
    w = w/total
    q0 = int(round(w[0]*255))
    q1 = int(round(w[1]*255))
    q1 = min(q1, 255-q0)
    j = [int(j[k]) if w[k] > 0 else int(j[0]) for k in range(3)]
    return j, [q0, q1]


def model(path):
    doc, binary = read_glb(path)

    def acc(i):
        return np.array(accessor(doc, binary, i))
    nodes = doc['nodes']
    meshes = [n for n in nodes if 'mesh' in n]
    if len(meshes) != 1 or 'skin' not in meshes[0]:
        raise ValueError(f'{path.name}: exactly one skinned mesh node required')
    node = meshes[0]
    if np.max(np.abs(np.array(node_matrix(node)).reshape(4, 4).T-np.eye(4))) > 1e-7:
        raise ValueError(f'{path.name}: transformed mesh node unsupported')
    skin = doc['skins'][node['skin']]
    joint_count = len(skin['joints'])
    if not 3 <= joint_count <= MAX_JOINTS:
        raise ValueError(f'{path.name}: joint count {joint_count}')
    ibm = acc(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)  # column-vector M
    if len(ibm) != joint_count or not np.isfinite(ibm).all():
        raise ValueError(f'{path.name}: inverse bind matrices')
    rows = np.concatenate([ibm[:, :3, 0], ibm[:, :3, 1], ibm[:, :3, 2], ibm[:, :3, 3]], axis=1)

    vertices = []
    keys = {}
    indices = []
    batches = []
    textures = []
    texture_slots = {}
    for primitive in doc['meshes'][node['mesh']]['primitives']:
        if primitive.get('mode', 4) != 4:
            raise ValueError(f'{path.name}: triangle primitives required')
        attrs = primitive['attributes']
        pos = acc(attrs['POSITION'])
        uv = acc(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(pos), 2))
        colour = acc(attrs['COLOR_0']) if 'COLOR_0' in attrs else np.ones((len(pos), 4))
        if colour.shape[1] == 3:
            colour = np.column_stack((colour, np.ones(len(pos))))
        joints = acc(attrs['JOINTS_0']).astype(int)
        weights = acc(attrs['WEIGHTS_0'])
        raw = acc(primitive['indices']).astype(int).ravel()
        if len(raw) % 3 or raw.max() >= len(pos) or raw.min() < 0:
            raise ValueError(f'{path.name}: bad indices')
        joints = np.where(weights > 0, joints, 0)  # unweighted slots may name any joint
        if joints[raw].max() >= joint_count or joints[raw].min() < 0:
            raise ValueError(f'{path.name}: joint index outside the skin')
        material = doc['materials'][primitive['material']] if 'material' in primitive else {}
        pbr = material.get('pbrMetallicRoughness', {})
        factor = np.array(pbr.get('baseColorFactor', [1, 1, 1, 1]), dtype=float)
        mode = material.get('alphaMode', 'OPAQUE')
        cutoff = material.get('alphaCutoff', 0.5)
        tex = pbr.get('baseColorTexture')
        image = doc['textures'][tex['index']]['source'] if tex else None
        slot = 0xffff
        if image is not None:
            key = (image, mode, cutoff)
            if key not in texture_slots:
                texture_slots[key] = len(textures)
                textures.append(texture(doc, binary, image, mode, cutoff))
            slot = texture_slots[key]
        flags = 1 if mode == 'MASK' else 2 if mode == 'BLEND' else 0
        first = len(indices)
        for original in raw:
            j, w = top3(joints[original], weights[original])
            c = np.clip(np.rint(colour[original]*factor*255), 0, 255).astype(int)
            argb = int(c[3]) << 24 | int(c[0]) << 16 | int(c[1]) << 8 | int(c[2])
            p = tuple(float(v) for v in pos[original])
            t = (float(uv[original][0]), float(uv[original][1]))
            key = (p, t, argb, tuple(j), tuple(w))
            if key not in keys:
                keys[key] = len(vertices)
                vertices.append(key)
            indices.append(keys[key])
        batches.append((first, len(raw), slot, flags))
    if not 0 < len(vertices) <= MAX_VERTICES:
        raise ValueError(f'{path.name}: vertex budget {len(vertices)}')

    p = np.array([v[0] for v in vertices])
    lo, hi = p.min(axis=0), p.max(axis=0)
    centre = (lo+hi)/2
    scale = float(max(np.max(hi-lo)/2, 1e-6)/32767)
    q = np.rint((p-centre)/scale).astype(int)
    if np.max(np.abs(q)) > 32767:
        raise ValueError(f'{path.name}: position quantization')
    uvq = np.rint(np.array([v[1] for v in vertices])*1024).astype(int)
    if np.max(np.abs(uvq)) > 32767:
        raise ValueError(f'{path.name}: UV outside +-32')

    out = bytearray(struct.pack('<4s7I4f', b'DSK1', 1, joint_count, len(vertices), len(indices),
                                len(batches), len(textures), sum(len(t[3]) for t in textures),
                                *centre, scale))
    out += rows.astype('<f4').tobytes()
    for k, (_, _, argb, j, w) in enumerate(vertices):
        out += struct.pack('<5h3B2BBI', *q[k], *uvq[k], *j, *w, 0, argb)
    out += struct.pack(f'<{len(indices)}H', *indices)
    if len(out) % 4:
        out += b'\0'*(4-len(out) % 4)
    for batch in batches:
        out += struct.pack('<IIHH', *batch)
    for width, height, fmt, pixels in textures:
        out += struct.pack('<HHII', width, height, fmt, len(pixels))+pixels
        if len(out) % 4:
            out += b'\0'*(4-len(out) % 4)
    position_error = float(np.max(np.abs(centre+q*scale-p)))
    report = dict(joints=joint_count, vertices=len(vertices), triangles=len(indices)//3, batches=len(batches),
                  textures=len(textures), texture_bytes=sum(len(t[3]) for t in textures), bytes=len(out),
                  position_error_m=position_error,
                  modes={m: sum(1 for b in batches if b[3] == f) for m, f in (('opaque', 0), ('punch', 1), ('translucent', 2))})
    return bytes(out), report


def bundle(paths):
    blobs = []
    report = {}
    for path in paths:
        name = path.stem
        if len(name.encode()) >= 32:
            raise ValueError(f'{name}: name longer than 31 bytes')
        blob, info = model(path)
        blobs.append((name, blob))
        report[name] = info
    header = bytearray(struct.pack('<4s3I', b'DSKB', 1, len(blobs), 0))
    offset = len(header)+48*len(blobs)
    offset = (offset+31) & ~31
    entries = bytearray()
    body = bytearray()
    for name, blob in blobs:
        entries += struct.pack('<32s4I', name.encode(), offset+len(body), len(blob), zlib.crc32(blob), 0)
        body += blob
        body += b'\0'*((-len(body)) % 32)
    data = header+entries
    data += b'\0'*(offset-len(data))
    return bytes(data+body), report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('glb', type=Path, nargs='+')
    parser.add_argument('--out', type=Path, required=True, help='DSKB bundle to write (under build/)')
    args = parser.parse_args()
    if np is None or Image is None:
        parser.error('numpy and Pillow are required')
    data, report = bundle(args.glb)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    summary = dict(ai_assisted=True, bytes=len(data), models=report)
    args.out.with_suffix('.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(dict(bytes=len(data), models={k: (v['vertices'], v['triangles'], v['bytes']) for k, v in report.items()})))


if __name__ == '__main__':
    main()

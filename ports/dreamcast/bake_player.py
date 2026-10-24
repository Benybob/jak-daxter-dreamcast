"""Bake selected original GLB skeletal clips for the native prototype (AI-assisted).

Requires numpy and Pillow. Outputs PRIVATE game-derived DCT/DCA files; never
commit these files. Facial morph animation and environment mapping are omitted.
This is a deliberately bounded exporter for OpenGOAL's shared-attribute mesh.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import numpy as np
from convert_scene import read_glb, accessor, node_matrix, WIDTHS
from convert_textured import texture_pixels

WIDTHS['MAT4'] = 16
CLIPS = ('stance-loop', 'walk', 'run', 'jump', 'jump-loop', 'jump-land')


def quaternion_matrix(q):
    return np.array(node_matrix({'rotation': list(q)})).reshape(4, 4).T


def rotation_quaternion(r):
    # Robust matrix -> quaternion via the largest eigenvector (xyzw).
    xx, xy, xz = r[0]; yx, yy, yz = r[1]; zx, zy, zz = r[2]
    k = np.array([[xx-yy-zz, xy+yx, xz+zx, zy-yz],
                  [xy+yx, yy-xx-zz, yz+zy, xz-zx],
                  [xz+zx, yz+zy, zz-xx-yy, yx-xy],
                  [zy-yz, xz-zx, yx-xy, xx+yy+zz]]) / 3
    return np.linalg.eigh(k)[1][:, -1]


def decompose(node):
    m = np.array(node_matrix(node)).reshape(4, 4).T
    s = np.linalg.norm(m[:3, :3], axis=0)
    if np.min(s) < 1e-8: raise ValueError('singular bind transform')
    r = m[:3, :3] / s
    if np.linalg.det(r) < 0 or np.max(np.abs(r.T @ r-np.eye(3))) > 1e-4:
        raise ValueError('reflection/shear bind transform unsupported')
    q = rotation_quaternion(r)
    if np.max(np.abs(quaternion_matrix(q)[:3, :3]-r)) > 1e-5:
        raise ValueError('quaternion decomposition mismatch')
    return m[:3, 3].copy(), q, s


def sample(times, values, t, rotation=False):
    if len(times) != len(values) or not len(times) or np.any(np.diff(times) <= 0):
        raise ValueError('invalid animation timeline')
    hi = int(np.searchsorted(times, t, side='right'))
    if hi == 0: return values[0].copy()
    if hi >= len(times): return values[-1].copy()
    lo = hi-1; f = (t-times[lo])/(times[hi]-times[lo])
    a, b = values[lo].copy(), values[hi].copy()
    if rotation:
        a /= np.linalg.norm(a); b /= np.linalg.norm(b)
        dot = float(a @ b)
        if dot < 0: b = -b; dot = -dot
        dot = min(1.0, dot)
        if dot < 0.9995:
            angle = math.acos(dot)
            q = (math.sin((1-f)*angle)*a+math.sin(f*angle)*b)/math.sin(angle)
        else: q = a*(1-f)+b*f
        return q/np.linalg.norm(q)
    return a*(1-f)+b*f


def bake(path, output):
    doc, binary = read_glb(path)
    def acc(i): return np.array(accessor(doc, binary, i))
    nodes = doc['nodes']; mesh_nodes = [n for n in nodes if 'mesh' in n and 'skin' in n]
    if len(mesh_nodes) != 1: raise ValueError('exactly one skinned mesh required')
    mesh = doc['meshes'][mesh_nodes[0]['mesh']]; skin = doc['skins'][mesh_nodes[0]['skin']]
    if len(skin['joints']) < 3 or nodes[skin['joints'][2]].get('name') != 'main':
        raise ValueError('expected original main skeletal root')
    if np.max(np.abs(np.array(node_matrix(mesh_nodes[0])).reshape(4, 4).T-np.eye(4))) > 1e-7:
        raise ValueError('transformed mesh node unsupported')
    if any(i in node.get('children', []) for i, n in enumerate(nodes) if n is mesh_nodes[0] for node in nodes):
        raise ValueError('parented mesh node unsupported')
    primitives = mesh['primitives']; attrs = primitives[0]['attributes']
    if any(p['attributes'] != attrs or p.get('mode', 4) != 4 for p in primitives):
        raise ValueError('shared attributes and triangle primitives required')
    raw_indices = [acc(p['indices']).astype(np.uint32).ravel() for p in primitives]
    used = np.unique(np.concatenate(raw_indices)); n = len(used)
    if not 0 < n <= 8192 or sum(map(len, raw_indices)) > 24576:
        raise ValueError('character geometry budget exceeded')
    positions = acc(attrs['POSITION'])[used]
    if max(used) >= len(acc(attrs['POSITION'])): raise ValueError('bad mesh index')
    joints = acc(attrs['JOINTS_0'])[used].astype(np.int32)
    weights = acc(attrs['WEIGHTS_0'])[used]
    if np.any(joints < 0) or np.max(joints) >= len(skin['joints']) or np.any(weights < 0):
        raise ValueError('invalid skin weights/joints')
    if np.max(np.abs(weights.sum(axis=1)-1)) > 0.002: raise ValueError('weights not normalized')
    weights = weights/weights.sum(axis=1)[:, None]
    inv = acc(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    base = [decompose(node) for node in nodes]
    parents = {}
    for i, node in enumerate(nodes):
        for child in node.get('children', []):
            if child in parents: raise ValueError('multiple node parents')
            parents[child] = i
    def pose(channels=(), t=0):
        trs = [[v.copy() for v in b] for b in base]
        for target, kind, times, values in channels:
            if kind == 'weights': continue  # Explicitly frozen facial morphs.
            if kind not in ('translation', 'rotation', 'scale'): raise ValueError('bad channel')
            trs[target][('translation', 'rotation', 'scale').index(kind)] = sample(times, values, t, kind == 'rotation')
        # Native controller owns planar displacement. Keep exported main-root
        # vertical bob and rotation; remove only its animated X/Z translation.
        root = skin['joints'][2]
        trs[root][0][[0, 2]] = base[root][0][[0, 2]]
        local = []
        for translation, rotation, scale in trs:
            m = quaternion_matrix(rotation); m[:3, :3] *= scale; m[:3, 3] = translation; local.append(m)
        world = {}; visiting = set()
        def world_at(i):
            if i in visiting: raise ValueError('node cycle')
            if i not in world:
                visiting.add(i); world[i] = world_at(parents[i]) @ local[i] if i in parents else local[i]; visiting.remove(i)
            return world[i]
        matrices = np.stack([world_at(i) for i in skin['joints']]) @ inv
        hp = np.column_stack((positions, np.ones(n)))
        result = np.zeros((n, 3))
        for slot in range(4):
            result += np.einsum('nij,nj->ni', matrices[joints[:, slot]], hp)[:, :3]*weights[:, slot, None]
        return result, matrices
    _, bind = pose()
    error = float(np.max(np.abs(bind-np.eye(4))))
    if error > 0.01: raise ValueError(f'bind/inverse-bind mismatch {error}')
    animations = {a['name']: a for a in doc['animations']}
    clips = []; all_poses = []
    for suffix in CLIPS:
        anim = animations['eichar-'+suffix]; channels = []; duration = 0
        for channel in anim['channels']:
            sampler = anim['samplers'][channel['sampler']]
            if sampler.get('interpolation', 'LINEAR') != 'LINEAR': raise ValueError('LINEAR clips required')
            times = acc(sampler['input']).ravel(); values = acc(sampler['output'])
            if channel['target']['path'] == 'weights': continue
            duration = max(duration, float(times[-1])); channels.append((channel['target']['node'], channel['target']['path'], times, values))
        if not 0 < duration <= 5: raise ValueError('clip duration budget exceeded')
        count = max(2, math.ceil(duration*15)+1)
        first = len(all_poses)
        for t in np.linspace(0, duration, count): all_poses.append(pose(channels, t)[0])
        clips.append((first, count, duration, int(suffix in ('stance-loop', 'walk', 'run', 'jump-loop'))))
    frames = np.stack(all_poses); anchor = (frames[0].min(axis=0)+frames[0].max(axis=0))/2
    anchor[1] = frames[0, :, 1].min()
    scale = 1.75/(frames[0, :, 1].max()-anchor[1])/4
    frames = (frames-anchor)*scale
    quantized = np.rint(frames*4096)
    if not np.isfinite(frames).all() or np.max(np.abs(quantized)) > 32767 or len(frames) > 256:
        raise ValueError('pose budget/range exceeded')
    data = bytearray(struct.pack('<4s4I', b'DCA1', 1, n, len(clips), len(frames)))
    for record in clips: data += struct.pack('<IIfI', *record)
    data += quantized.astype('<i2').tobytes()
    remap = {int(v): i for i, v in enumerate(used)}; indices = []; batches = []; textures = []; texmap = {}
    uvs = acc(attrs['TEXCOORD_0'])[used]; colors = acc(attrs['COLOR_0'])[used]
    if colors.shape[1] == 3: colors = np.column_stack((colors, np.ones(n)))
    factors = np.ones((n, 4)); assigned = set()
    for p, raw in zip(primitives, raw_indices):
        mat = doc['materials'][p['material']]; pbr = mat.get('pbrMetallicRoughness', {})
        factor = np.array(pbr.get('baseColorFactor', [1, 1, 1, 1]))
        if factor.shape != (4,) or not np.isfinite(factor).all(): raise ValueError('invalid material factor')
        for original in np.unique(raw):
            target = remap[int(original)]
            if target in assigned and np.max(np.abs(factors[target]-factor)) > 1e-6:
                raise ValueError('shared vertex has conflicting material factors')
            assigned.add(target); factors[target] = factor
        texture = pbr.get('baseColorTexture'); image = doc['textures'][texture['index']]['source'] if texture else None
        mode = mat.get('alphaMode', 'OPAQUE'); cutoff = mat.get('alphaCutoff', 0.5)
        if mode not in ('MASK', 'OPAQUE'): raise ValueError('transparent material unsupported')
        key = (image, mode, cutoff)
        if key not in texmap: texmap[key] = len(textures); textures.append(texture_pixels(doc, binary, image, mode, cutoff, 64))
        first = len(indices); indices.extend(remap[int(v)] for v in raw)
        batches.append((first, len(raw), texmap[key], int(mode == 'MASK')))
    tb = sum(len(x[2]) for x in textures)
    scene = bytearray(struct.pack('<4s7I', b'DCT1', 1, n, len(indices), len(batches), len(textures), tb, 0))
    for i in range(n):
        color = np.clip(np.rint(colors[i]*factors[i]*255), 0, 255).astype(int)
        if len(color) == 3: color = list(color)+[255]
        argb = int(color[3]) << 24 | int(color[0]) << 16 | int(color[1]) << 8 | int(color[2])
        scene += struct.pack('<5fI', *frames[0, i], *uvs[i], argb)
    scene += struct.pack('<'+'H'*len(indices), *indices)
    for batch in batches: scene += struct.pack('<4I', *batch)
    for w, h, pixels in textures: scene += struct.pack('<HHII', w, h, 1, len(pixels))+pixels
    output.mkdir(parents=True, exist_ok=True)
    (output/'player.dca').write_bytes(data); (output/'player.dct').write_bytes(scene)
    report = dict(ai_assisted=True, vertices=n, triangles=len(indices)//3, frames=len(frames),
                  pose_bytes=len(data), scene_bytes=len(scene), texture_bytes=tb, textures=len(textures), clips=dict(zip(CLIPS, clips)),
                  bind_matrix_error=error, quantization_world_units=4/4096,
                  limitations=['15Hz baked skeletal poses', 'frozen facial morphs', 'no environment mapping', 'native movement tuning'])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n'); return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('input', type=Path); parser.add_argument('output', type=Path)
    args = parser.parse_args(); print(json.dumps(bake(args.input, args.output), indent=2))

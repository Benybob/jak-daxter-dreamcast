"""Prepare a bounded original training render/collision bundle (AI-assisted), ISC.

Input is a locally extracted static background GLB and a DCC1 patch. Output
contains game assets and must remain private. Select whole intersecting triangles,
apply active-scene node transforms, rebase into collision-local meters, then use
the existing DCT1 material/texture converter. WLD2 binds the exact two payloads.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import zlib
from convert_scene import read_glb, accessor, multiply, transform, node_matrix, IDENTITY
from convert_textured import convert

try:
    import numpy as np
except ImportError:
    np = None  # Same selection, slower offline broad phase; not a target dependency.


def collision_info(data):
    if len(data) < 24: raise ValueError('short DCC1 header')
    magic, version, count, *origin = struct.unpack_from('<4sII3f', data)
    if magic != b'DCC1' or version != 1 or not 0 < count <= 8192 or len(data) != 24+count*40:
        raise ValueError('invalid DCC1 length/count/version')
    if not all(math.isfinite(v) and abs(v) <= 4096 for v in origin):
        raise ValueError('invalid DCC1 origin')
    for values in struct.iter_unpack('<9fI', data[24:]):
        if not all(math.isfinite(v) and abs(v) <= 4096 for v in values[:9]):
            raise ValueError('invalid DCC1 position')
        if ((values[9] >> 3) & 7) > 2: raise ValueError('unsupported collision mode')
        if not (values[9] & 1) and ((values[9] >> 14) & 63):
            raise ValueError('unsupported entity surface event')
        a, b, c = (values[i:i+3] for i in (0, 3, 6))
        u, v = ([b[k]-a[k] for k in range(3)], [c[k]-a[k] for k in range(3)])
        n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        if sum(x*x for x in n) < 1e-14: raise ValueError('degenerate collision triangle')
    return origin, count


def intersects_box(points, low, high):
    """Clip positions only to decide overlap; output retains original triangles."""
    poly = [tuple(p) for p in points]
    for axis in range(3):
        for edge, sign in ((low[axis], 1), (high[axis], -1)):
            if not poly: return False
            out = []
            previous = poly[-1]; pd = (previous[axis]-edge)*sign
            for point in poly:
                qd = (point[axis]-edge)*sign
                if (pd >= 0) != (qd >= 0):
                    t = pd/(pd-qd)
                    out.append(tuple(previous[k]+(point[k]-previous[k])*t for k in range(3)))
                if qd >= 0: out.append(point)
                previous, pd = point, qd
            poly = out
    return bool(poly)


def convert_patch(doc, binary, origin, extent=(12, 12, 12), texture_size=64):
    if len(origin) != 3 or not all(math.isfinite(v) and abs(v) <= 4096 for v in origin):
        raise ValueError('invalid collision origin')
    if len(extent) != 3 or not all(math.isfinite(v) and 0 < v <= 64 for v in extent):
        raise ValueError('invalid render selection extent')
    if texture_size not in (8, 16, 32, 64): raise ValueError('training textures must be 8..64 power of two')
    low, high = ([origin[k]-extent[k] for k in range(3)], [origin[k]+extent[k] for k in range(3)])
    out = copy.deepcopy(doc); raw = bytearray(binary)
    selected = []; cache = {}; triangle_count = 0; instances = 0

    def decoded(index):
        if index not in cache: cache[index] = accessor(doc, binary, index)
        return cache[index]

    def append(values, width, integer=False):
        while len(raw) % 4: raw.append(0)
        start = len(raw)
        layout = struct.Struct('<'+('I' if integer else 'f')*width)
        for value in values: raw.extend(layout.pack(*value))
        view = len(out['bufferViews']); out['bufferViews'].append(dict(buffer=0, byteOffset=start, byteLength=len(raw)-start))
        index = len(out['accessors']); out['accessors'].append(dict(bufferView=view, componentType=5125 if integer else 5126,
            count=len(values), type={1:'SCALAR', 2:'VEC2', 3:'VEC3', 4:'VEC4'}[width]))
        return index

    def walk(index, parent, ancestors):
        nonlocal triangle_count, instances
        if not isinstance(index, int) or not 0 <= index < len(doc['nodes']): raise ValueError('invalid node reference')
        if index in ancestors: raise ValueError('cycle in active node graph')
        node = doc['nodes'][index]
        if 'matrix' in node and any(k in node for k in ('translation', 'rotation', 'scale')):
            raise ValueError('ambiguous node transform')
        matrix = multiply(parent, node_matrix(node))
        if not all(math.isfinite(v) for v in matrix): raise ValueError('non-finite world transform')
        if 'skin' in node: raise ValueError('skinned background node unsupported')
        if 'mesh' in node:
            instances += 1
            mesh_index = node['mesh']
            if not isinstance(mesh_index, int) or not 0 <= mesh_index < len(doc['meshes']):
                raise ValueError('invalid mesh reference')
            for primitive in doc['meshes'][mesh_index]['primitives']:
                if primitive.get('mode', 4) != 4: raise ValueError('background TRIANGLES required')
                attrs = primitive['attributes']; positions = decoded(attrs['POSITION'])
                if any(len(p) != 3 for p in positions): raise ValueError('invalid POSITION width')
                indices = decoded(primitive['indices']) if 'indices' in primitive else [(i,) for i in range(len(positions))]
                if len(indices) % 3: raise ValueError('incomplete triangle')
                if any(len(e) != 1 or not isinstance(e[0], int) or not 0 <= e[0] < len(positions) for e in indices):
                    raise ValueError('invalid triangle index')
                if np is not None:
                    points = np.asarray(positions, dtype=np.float64).reshape(-1, 3)
                    mat = np.array(matrix).reshape((4, 4), order='F')
                    points = points @ mat[:3, :3].T+mat[:3, 3]
                    if not np.isfinite(points).all(): raise ValueError('non-finite world positions')
                    tris = np.array(indices, dtype=np.uint32).reshape(-1, 3)
                    corners = points[tris]
                    candidates = tris[np.all((corners.min(1) <= high) & (corners.max(1) >= low), axis=1)] if len(tris) else []
                else:
                    points = [transform(matrix, p) for p in positions]
                    if not all(math.isfinite(v) for p in points for v in p): raise ValueError('non-finite world positions')
                    candidates = [tuple(e[0] for e in indices[i:i+3]) for i in range(0, len(indices), 3)]
                    candidates = [t for t in candidates if all(min(points[j][k] for j in t) <= high[k] and
                                  max(points[j][k] for j in t) >= low[k] for k in range(3))]
                chosen = [tuple(int(i) for i in tri) for tri in candidates if intersects_box([points[int(i)] for i in tri], low, high)]
                if not chosen: continue
                if 'targets' in primitive: raise ValueError('selected morph targets unsupported')
                triangle_count += len(chosen)
                if triangle_count > 8192: raise ValueError('render triangle budget exceeded')
                # Compact attributes before the shared converter sees them. Large shared
                # source accessors are decoded once; original nodes/instances stay distinct.
                used = sorted({i for tri in chosen for i in tri}); remap = {i:j for j,i in enumerate(used)}
                attributes = {'POSITION':append([tuple(float(points[i][k])-origin[k] for k in range(3)) for i in used], 3)}
                for name, widths in (('TEXCOORD_0', (2,)), ('COLOR_0', (3, 4))):
                    if name in attrs:
                        values = decoded(attrs[name])
                        if len(values) != len(positions) or any(len(v) not in widths for v in values):
                            raise ValueError('invalid selected attribute count/width')
                        attributes[name] = append([values[i] for i in used], len(values[0]))
                cp = copy.deepcopy(primitive); cp['attributes'] = attributes
                cp['indices'] = append([(remap[i],) for tri in chosen for i in tri], 1, True)
                selected.append(cp)
        for child in node.get('children', []): walk(child, matrix, ancestors | {index})

    for root in doc['scenes'][doc.get('scene', 0)]['nodes']: walk(root, IDENTITY, set())
    if not selected: raise ValueError('no intersecting render triangles')
    out['meshes'] = [dict(primitives=selected)]; out['nodes'] = [dict(mesh=0)]
    out['scenes'] = [dict(nodes=[0])]; out['scene'] = 0; out['buffers'][0]['byteLength'] = len(raw)
    data, report = convert(out, raw, 0, texture_size, 'argb1555')
    if report['vertices'] > 8192 or report['texture_bytes'] > 512*1024 or len(data) > 1024*1024:
        raise ValueError('training render memory budget exceeded')
    if not 1e-6 <= report['original_radius'] <= 256 or any(abs(x) > 256 for x in report['original_center']):
        raise ValueError('training render transform outside bounds')
    report.update(ai_assisted=True, coordinate_space='collision-local meters before DCT normalization',
        collision_origin=list(origin), selection_extent=list(extent), whole_intersecting_triangles=True,
        decoded_source_accessors=len(cache), active_mesh_instances=instances, native_texture_size=texture_size)
    return data, report


def manifest(dct, dcc, report, spawn, extent=None):
    origin, count = collision_info(dcc)
    if len(spawn) != 3 or not all(math.isfinite(v) and abs(v) <= 256 for v in spawn):
        raise ValueError('invalid spawn')
    if extent is not None and (len(extent)!=3 or not all(math.isfinite(v) and 0<v<=64 for v in extent)):
        raise ValueError('invalid collision coverage')
    data = struct.pack('<4sI10f9I', b'WLD3' if extent else b'WLD2', 3 if extent else 2, *origin, *report['original_center'], report['original_radius'], *spawn,
        len(dct), zlib.crc32(dct), len(dcc), zlib.crc32(dcc), report['vertices'], report['triangles']*3,
        report['batches'], report['textures'], count)
    return data+(struct.pack('<3f',*extent) if extent else b'')


def prepare(glb, collision, output, spawn, extent=(12, 12, 12), texture_size=64, collision_report=None):
    if output.exists(): raise ValueError('output must be a new private directory')
    dcc = collision.read_bytes(); origin, count = collision_info(dcc)
    doc, binary = read_glb(glb); dct, report = convert_patch(doc, binary, origin, extent, texture_size)
    coverage=None
    if collision_report:
        provenance=json.loads(collision_report.read_text(encoding='utf-8'))
        radius=provenance['selection_radius'];coverage=provenance['coverage_half_extent_meters']
        if (provenance['payload_sha256']!=hashlib.sha256(dcc).hexdigest() or
            provenance['selected_triangles']!=count or provenance['payload_bytes']!=len(dcc) or
            not provenance['whole_intersecting_triangles'] or
            struct.pack('<3f',*provenance['origin_meters'])!=struct.pack('<3f',*origin) or
            provenance['coverage_format_version']!=1 or provenance['source_units_per_meter']!=4096 or
            not math.isfinite(radius) or not 1<=radius<=64 or coverage!=[radius,12,radius]):
            raise ValueError('collision coverage provenance mismatch')
    metadata = manifest(dct, dcc, report, spawn, coverage)
    report.update(source_glb_sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),
        collision_sha256=hashlib.sha256(dcc).hexdigest(), dct_sha256=hashlib.sha256(dct).hexdigest(),
        manifest_sha256=hashlib.sha256(metadata).hexdigest(), collision_triangles=count,
        spawn_meters=list(spawn), metadata_format='WLD3' if coverage else 'WLD2',
        collision_half_extent=coverage, private_game_assets=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Commit the completed bundle as one new directory; never overwrite a working bundle.
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        stage = Path(tmp)/'bundle'; stage.mkdir()
        for name, data in (('world.dct', dct), ('world.dcc', dcc), ('world.xfm', metadata)):
            (stage/name).write_bytes(data)
        (stage/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        stage.rename(output)
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('glb', type=Path); p.add_argument('collision', type=Path); p.add_argument('output', type=Path)
    p.add_argument('--spawn', type=float, nargs=3, required=True)
    p.add_argument('--extent', type=float, nargs=3, default=(12, 12, 12))
    p.add_argument('--texture-size', type=int, default=64)
    p.add_argument('--collision-report', type=Path, help='Matching exporter report with payload hash and coverage; writes WLD3')
    a = p.parse_args()
    print(json.dumps(prepare(a.glb, a.collision, a.output, a.spawn, a.extent, a.texture_size,a.collision_report), indent=2))

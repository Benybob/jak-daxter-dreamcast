"""TIE and shrub prototypes cut out of the background GLB, in a PowerVR-ready
package (AI-assisted), ISC.

The decompiler's GLB expands every instance into world space. For each
prototype this takes the triangles of the GLB nodes named after it, cuts out
the ones inside one instance's bounding sphere, and maps them back into the
prototype's own frame with the inverse of that instance's matrix (decoded as
instanced_level.instance_matrix does). A cut is kept only if a second instance
cut the same way gives the same triangles within 2 mm, and, for TIE, as many as
the BSP's LOD 0 count. Nothing from the disc is committed; the package is written
where you say.

The package, DCI1 (little endian):
  header      'DCI1', prototype count, instance count
  prototype   vertex count, index count, first vertex, first index, material
              of each triangle run (see below), name (32 bytes)
  vertex      24 bytes: float x, y, z (meters, prototype frame), float u, v,
              ARGB8888: the PowerVR's float-colour-free vertex layout
  index       u16, three a triangle; the run pads to 4 bytes
  instance    u16 prototype, u16 pad, then a 3x4 float matrix (rows 0-2 and the
              translation, meters): what the renderer multiplies by

  python prototype_geometry.py DGO GLB [--out PACKAGE] [--json SUMMARY]
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

PORT = Path(__file__).resolve().parent
sys.path.insert(0, str(PORT))
import instanced_level  # noqa: E402
import level_contents  # noqa: E402
import prepare_render_sectors as rs  # noqa: E402

TOLERANCE_M = 0.002


def inverse_affine(m):
    """Inverse of a row-major affine matrix (rows 0-2 linear, row 3 translation)."""
    a = [row[:3] for row in m[:3]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-12:
        raise ValueError('singular instance matrix')
    inv = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3] -
             a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det for j in range(3)] for i in range(3)]
    t = m[3][:3]
    return inv, t


def to_local(point_m, inv, t):
    """world = l.x row0 + l.y row1 + l.z row2 + t (game units); invert it."""
    d = [point_m[k] * instanced_level.UNITS_PER_M - t[k] for k in range(3)]
    return [sum(d[j] * inv[j][i] for j in range(3)) / instanced_level.UNITS_PER_M for i in range(3)]


def world_triangles(doc, binary):
    """node name -> list of ((p0, p1, p2), (uv0, uv1, uv2), (c0, c1, c2), material) in meters."""
    nodes, meshes, cache, out = doc['nodes'], doc['meshes'], {}, {}

    def acc(i):
        if i not in cache:
            cache[i] = rs.accessor(doc, binary, i)
        return cache[i]

    def flat(v):
        return v[0] if isinstance(v, (list, tuple)) else v

    def walk(i, parent):
        node = nodes[i]
        matrix = node.get('matrix')
        if matrix and matrix[3] == matrix[7] == matrix[11] == 0 and matrix[15] != 1:
            node = dict(node, matrix=matrix[:15] + [1.0])  # the decompiler's wind instances
        world = rs.multiply(parent, rs.node_matrix(node))
        if 'mesh' in node:
            tris = out.setdefault(node.get('name', ''), [])
            for prim in meshes[node['mesh']]['primitives']:
                attrs = prim['attributes']
                pos = acc(attrs['POSITION'])
                uv = acc(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else None
                col = acc(attrs['COLOR_0']) if 'COLOR_0' in attrs else None
                idx = [flat(v) for v in acc(prim['indices'])]
                for k in range(0, len(idx) - 2, 3):
                    tri = idx[k:k + 3]
                    tris.append((tuple(tuple(rs.transform(world, pos[j])) for j in tri),
                                 tuple(tuple(uv[j]) if uv else (0.0, 0.0) for j in tri),
                                 tuple(tuple(col[j]) if col else (1.0, 1.0, 1.0, 1.0) for j in tri),
                                 prim.get('material', -1)))
        for c in node.get('children', []):
            walk(c, world)

    for r in doc['scenes'][doc.get('scene', 0)]['nodes']:
        walk(r, rs.IDENTITY)
    return out


def cut(tris, inst):
    """The triangles wholly inside an instance's bounding sphere, in its prototype frame."""
    centre = (inst['x'], inst['y'], inst['z'])
    reach = inst['r'] * 1.01 + TOLERANCE_M
    inv, t = inverse_affine(instance_level_matrix(inst))
    out = []
    for points, uvs, cols, material in tris:
        if all(math.dist(p, centre) <= reach for p in points):
            out.append((tuple(tuple(to_local(p, inv, t)) for p in points), uvs, cols, material))
    return out


def instance_level_matrix(inst):
    return instanced_level.instance_matrix(inst)


def signature(local):
    return sorted(tuple(round(c / TOLERANCE_M) for p in points for c in p) for points, _, _, _ in local)


def shared(a, b):
    """The triangles of cut a that cut b also has, each vertex within the tolerance,
    each matched once. Triangles are hashed by centroid on a coarse grid and
    compared vertex by vertex, so values either side of a grid line still match."""
    cell = 4 * TOLERANCE_M

    def centre(tri):
        return tuple(sum(p[k] for p in tri[0]) / 3 for k in range(3))

    def key(c):
        return tuple(math.floor(x / cell) for x in c)

    grid = {}
    for n, tri in enumerate(b):
        grid.setdefault(key(centre(tri)), []).append(n)
    used, out = set(), []
    for tri in a:
        k = key(centre(tri))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for n in grid.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        if n in used:
                            continue
                        if all(abs(p - q) <= TOLERANCE_M for u, v in zip(tri[0], b[n][0]) for p, q in zip(u, v)):
                            used.add(n)
                            out.append(tri)
                            break
                    else:
                        continue
                    break
                else:
                    continue
                break
    return out


def same(a, b):
    """Two cuts agree: the same triangles, each vertex within the tolerance."""
    if len(a) != len(b):
        return False
    for x, y in zip(signature(a), signature(b)):
        if any(abs(p - q) > 1 for p, q in zip(x, y)):
            return False
    return True


def argb(c):
    v = [max(0, min(255, round(255 * min(1.0, c[k])))) for k in range(4)]
    return v[3] << 24 | v[0] << 16 | v[1] << 8 | v[2]


def build(dgo, glb):
    name = next(n for n, _, kind in level_contents.objects(dgo) if kind == 'level')
    prototypes, instances = instanced_level.model(level_contents.object_bytes(dgo, name))
    doc, binary = rs.read_glb(glb)
    tris = world_triangles(doc, binary)
    wind = [tri for name, group in tris.items() if name.startswith('wind-group') for tri in group]
    by_proto = {}
    for inst in instances:
        by_proto.setdefault(inst['proto'], []).append(inst)
    results = []
    for i, proto in enumerate(prototypes):
        own = by_proto.get(i, [])
        row = {'name': proto['name'], 'kind': proto['kind'], 'instances': len(own), 'status': 'no instances'}
        results.append(row)
        if not own:
            continue
        source = tris.get(proto['name'])
        if not source and proto['kind'] == 'tie':
            # Wind-swayed TIE (palms, the fish) is exported one node an instance,
            # named wind-group-*: cut from all of those together.
            source = wind
        if not source:
            row['status'] = 'no GLB node by that name'
            continue
        # Where instances crowd together a bounding sphere also takes in neighbours'
        # triangles; those land elsewhere in the prototype frame, so keep only the
        # triangles two instances' cuts share, and confirm with a third.
        cuts = [c for c in (cut(source, inst) for inst in own[:8]) if c]
        expected = proto['lods'][0]['tris'] if proto['kind'] == 'tie' else None
        found = None
        for a_i in range(len(cuts)):
            for b_i in range(a_i + 1, len(cuts)):
                common = shared(cuts[a_i], cuts[b_i])
                if not common or (expected is not None and len(common) != expected):
                    continue
                others = [c for k, c in enumerate(cuts) if k not in (a_i, b_i)]
                if expected is None and others and len(shared(common, others[0])) != len(common):
                    continue
                found = common
                break
            if found:
                break
        if found:
            row['status'] = 'ok'
            row['geometry'], row['tris'] = found, len(found)
        elif len(cuts) == 1 and (expected is None or len(cuts[0]) == expected):
            row['status'] = 'ok (one instance only)'
            row['geometry'], row['tris'] = cuts[0], len(cuts[0])
        else:
            row['status'] = 'no two instances agree'
            row['cut_tris'] = [len(c) for c in cuts]
            if expected is not None:
                row['bsp_tris'] = expected
    return results, instances, prototypes


def package(results, instances):
    """DCI1 bytes for the prototypes that cut cleanly, and their instances."""
    keep = [i for i, r in enumerate(results) if 'geometry' in r]
    remap = {old: new for new, old in enumerate(keep)}
    verts, idx, protos = bytearray(), bytearray(), bytearray()
    nverts = nidx = 0
    for old in keep:
        r = results[old]
        lookup, first_v, first_i, material = {}, nverts, nidx, r['geometry'][0][3]
        for points, uvs, cols, _ in r['geometry']:
            for p, uv, c in zip(points, uvs, cols):
                key = (tuple(round(x / TOLERANCE_M) for x in p), uv, tuple(c))
                if key not in lookup:
                    lookup[key] = nverts - first_v
                    verts += struct.pack('<5fI', *p, *uv, argb(c))
                    nverts += 1
                idx += struct.pack('<H', lookup[key])
                nidx += 1
        protos += struct.pack('<5I', nverts - first_v, nidx - first_i, first_v, first_i, material & 0xffffffff)
        protos += r['name'].encode()[:31].ljust(32, b'\0')
    inst_bytes = bytearray()
    count = 0
    for inst in instances:
        if inst['proto'] in remap:
            m = instanced_level.instance_matrix(inst)
            rows = [x for row in m[:3] for x in row[:3]] + [x / instanced_level.UNITS_PER_M for x in m[3][:3]]
            inst_bytes += struct.pack('<2H12f', remap[inst['proto']], 0, *rows)
            count += 1
    if len(idx) % 4:
        idx += b'\0\0'  # the instance table stays 4-byte aligned
    head = struct.pack('<4s2I', b'DCI1', len(keep), count)
    return head + protos + verts + idx + inst_bytes, {'prototypes': len(keep), 'instances': count,
                                                      'vertices': nverts, 'indices': nidx}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dgo', type=Path)
    parser.add_argument('glb', type=Path)
    parser.add_argument('--out', type=Path, help='write the DCI1 package here')
    parser.add_argument('--json', type=Path, help='write the per-prototype results here')
    args = parser.parse_args()
    results, instances, _ = build(args.dgo, args.glb)
    blob, counts = package(results, instances)
    if args.out:
        args.out.write_bytes(blob)
    status = {}
    for r in results:
        status[r['status']] = status.get(r['status'], 0) + 1
    if args.json:
        args.json.write_text(json.dumps([{k: v for k, v in r.items() if k != 'geometry'} for r in results], indent=1))
    print(json.dumps({'status': status, 'package_bytes': len(blob), **counts}, indent=1))


if __name__ == '__main__':
    main()

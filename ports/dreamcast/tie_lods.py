"""TIE's lower LODs added to a DCI1 package, as DCI2 (AI-assisted), ISC.

    python tie_lods.py PACKAGE.dci1 DGO DUMP_DIR --out PACKAGE.dci2 [--json SUMMARY]
    python tie_lods.py --textured GLB DGO DUMP_DIR --out PACKAGE.dci2 [--json SUMMARY] [--compact]

The GLB holds TIE geometry 0 only. The decompiler emulates all four geometries
(extract_tie.cpp) and, with its level dump switched on (the `dump_level`
argument extract_level.cpp passes to extract_tie, false upstream), writes each
prototype's strips per geometry to DUMP_DIR/lod{0..3}-tie-*/NAME.obj in the
prototype frame at 1/16 meter (strip positions are game units over 65536).
Geometry 0 there matches the package's cut prototype; geometries 1 to 3 are
added from it. The OBJ has no colours, so a lower LOD's vertex takes the colour
of the nearest geometry-0 vertex of the same prototype. A geometry identical to
the one before it is shared, not stored again. Each TIE prototype's switch
planes are its bucket's near, mid and far planes (instance_budget.bsp_lod_table);
shrubs and anything not in the dump keep one geometry.

DCI2 (little endian), DCI1 with a geometry table between prototypes and data:
  header      'DCI2', prototype count, geometry count, instance count
  geometry    as a DCI1 prototype: vertex count, index count, first vertex,
              first index, material, name (32 bytes)
  prototype   u16 geometry per LOD 0..3 (0xffff: none; LOD 0 always set),
              float near, mid and far plane (meters; NO_SWITCH for none),
              u8 extra geometries per LOD 0..3: LOD n is geometry[n] and the
              count[n] geometries after it (one per material; 0 before --textured)
  vertex, index (padded to 4 bytes), instance: as DCI1, the instance's
              prototype indexing the prototype table

LOD rule, shared with instance_pass.c: d2 is the draw rule's single-precision
squared horizontal distance from the camera target; the LOD is the number of
planes p with d2 > f32(p * p), stepped down to the nearest geometry that exists.
flatten() resolves that for one camera target and returns the DCI1 a DCI1
reader (dci1_reference.py) draws.

DCI3 (--compact) is DCI2 with 16-byte vertices: after the prototype table, a
quantization record a geometry (float centre x, y, z and scale, meters a step),
then vertices of s16 x, y, z (position = centre + q * scale), s16 u, v (in
1/2048), u16 0 and ARGB8888. Positions land within half a step of the float
ones (11 mm on village1's 718 m low-res mountains, under a millimetre on props).

--textured builds from the level's GLB and DGO directly (prototype_geometry.build)
so each prototype keeps its per-triangle materials: every LOD is split into one
geometry per GLB material, its vertex colours are the sector builder's
(clamp(colour x baseColorFactor), the PS2's x2 compensation included) so they
match the terrain under a texture, and lower LODs keep their dump UVs (they
agree with the GLB's) and take the material and colour of the nearest LOD 0
triangle and vertex.
"""
import argparse
import glob
import json
import os
from pathlib import Path
import struct

import instance_budget
import make_instance_fixture as mf

NONE = 0xffff
NO_SWITCH = 1e18  # squares to 1e36, still a finite float32


def read_obj(path, uvs=None):
    """Triangles of an extract_tie debug OBJ, in meters; with a list for uvs,
    each triangle's three texture coordinates are appended to it."""
    verts, tcs, tris = [], [], []
    with open(path) as f:
        for line in f:
            s = line.split()
            if not s:
                continue
            if s[0] == 'v':
                verts.append(tuple(float(x) * 16.0 for x in s[1:4]))
            elif s[0] == 'vt':
                tcs.append((float(s[1]), float(s[2])))
            elif s[0] == 'f':
                ids = [int(x.split('/')[0]) - 1 for x in s[1:4]]
                tids = [int(x.split('/')[1]) - 1 if '/' in x and x.split('/')[1] else i
                        for x, i in zip(s[1:4], ids)]
                tri = tuple(verts[i] for i in ids)
                a, b, c = tri
                u = [b[k] - a[k] for k in range(3)]
                v = [c[k] - a[k] for k in range(3)]
                cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                if sum(x * x for x in cross) > 1e-12:  # drop the strips' degenerate joins
                    tris.append(tri)
                    if uvs is not None:
                        uvs.append(tuple(tcs[i] if i < len(tcs) else (0.0, 0.0) for i in tids))
    return tris


def dump_lods(dump_dir, with_uvs=False):
    """{prototype name: [triangles of geometry 0..3]} from a decompiler level dump;
    with_uvs, each entry is (triangles, their UVs)."""
    out = {}
    for lod in range(4):
        for path in glob.glob(os.path.join(str(dump_dir), f'lod{lod}-tie-*', '*.obj')):
            name = os.path.basename(path)[:-4]
            if name == 'ALL':
                continue
            uvs = [] if with_uvs else None
            tris = read_obj(path, uvs)
            out.setdefault(name[:31], [None] * 4)[lod] = (tris, uvs) if with_uvs else tris
    return out


def read_dci1(blob):
    protos, instances = mf.read(blob)
    names = [blob[12 + 52 * i + 20:12 + 52 * i + 52].split(b'\0')[0].decode('ascii', 'replace')
             for i in range(len(protos))]
    vstart = 12 + 52 * len(protos)
    nv = sum(p[0] for p in protos)
    istart = vstart + 24 * nv
    geoms = []
    for (vc, ic, fv, fi, material), name in zip(protos, names):
        verts = [struct.unpack_from('<5fI', blob, vstart + 24 * (fv + k)) for k in range(vc)]
        idx = list(struct.unpack_from(f'<{ic}H', blob, istart + 2 * fi))
        geoms.append({'name': name, 'material': material, 'verts': verts, 'indices': idx})
    return geoms, instances


def from_triangles(tris, base, name):
    """A geometry from world-free triangles, coloured from base's nearest vertex."""
    lookup, verts, idx = {}, [], []
    for tri in tris:
        for p in tri:
            key = tuple(round(x * 1000) for x in p)
            if key not in lookup:
                near = min(base['verts'], key=lambda v: (v[0] - p[0]) ** 2 + (v[1] - p[1]) ** 2 + (v[2] - p[2]) ** 2)
                lookup[key] = len(verts)
                verts.append((p[0], p[1], p[2], near[3], near[4], near[5]))
            idx.append(lookup[key])
    return {'name': name, 'material': base['material'], 'verts': verts, 'indices': idx}


def same(a, b):
    """Two triangle lists equal within a millimetre, as sets."""
    if a is None or b is None or len(a) != len(b):
        return False
    key = lambda t: tuple(sorted(tuple(round(x * 1000) for x in p) for p in t))  # noqa: E731
    return sorted(map(key, a)) == sorted(map(key, b))


def build(dci1, lods, planes):
    """DCI2 bytes and a summary from a DCI1 package, {name: [tris of geometry 0..3]}
    and {name: (near, mid, far)}."""
    geoms, instances = read_dci1(dci1)
    table, out_geoms = [], list(geoms)
    summary = {'prototypes': len(geoms), 'with_lods': 0, 'lod_geometries_added': 0, 'shared': 0,
               'geometry0_mismatch': []}
    for i, g in enumerate(geoms):
        row = [i, NONE, NONE, NONE]
        lod = lods.get(g['name'])
        plane = planes.get(g['name'])
        if lod and plane and lod[0] is not None:
            if len(lod[0]) != len(g['indices']) // 3:
                summary['geometry0_mismatch'].append(g['name'])
            summary['with_lods'] += 1
            previous = lod[0]
            for n in (1, 2, 3):
                if not lod[n]:
                    continue
                if same(lod[n], previous):
                    row[n] = row[n - 1] if row[n - 1] != NONE else i
                    summary['shared'] += 1
                    continue
                row[n] = len(out_geoms)
                out_geoms.append(from_triangles(lod[n], g, f"{g['name'][:28]}:{n}"))
                summary['lod_geometries_added'] += 1
                previous = lod[n]
            table.append((row, plane))
        else:
            table.append((row, (NO_SWITCH,) * 3))
    blob = pack(out_geoms, table, instances)
    summary.update(geometries=len(out_geoms), instances=len(instances), package_bytes=len(blob),
                   vertices=sum(len(g['verts']) for g in out_geoms),
                   indices=sum(len(g['indices']) for g in out_geoms))
    return blob, summary


def colour(c, factor):
    """The sector builder's vertex ARGB: clamp(colour x baseColorFactor)."""
    rgba = list(c) + [1.0] * (4 - len(c))
    v = [round(max(0.0, min(1.0, rgba[k] * factor[k])) * 255) for k in range(4)]
    return v[3] << 24 | v[0] << 16 | v[1] << 8 | v[2]


def material_geometries(tris, name, factors):
    """One geometry per material from (points, uvs, colours, material) triangles."""
    groups = {}
    for pts, uvs, cols, mat in tris:
        groups.setdefault(mat, []).append((pts, uvs, cols))
    out = []
    for mat in sorted(groups):
        lookup, verts, idx = {}, [], []
        factor = factors.get(mat, (1.0, 1.0, 1.0, 1.0))
        for pts, uvs, cols in groups[mat]:
            for p, uv, c in zip(pts, uvs, cols):
                col = c if isinstance(c, int) else colour(c, factor)
                key = (tuple(round(x * 1000) for x in p), tuple(uv), col)
                if key not in lookup:
                    lookup[key] = len(verts)
                    verts.append((p[0], p[1], p[2], uv[0], uv[1], col))
                idx.append(lookup[key])
        out.append({'name': f'{name[:26]}#{mat & 0xffff}', 'material': mat & 0xffffffff, 'verts': verts,
                    'indices': idx})
    return out


def lower_lod(tris, uvs, base):
    """A lower LOD's (points, uvs, colours, material) triangles: material from the
    nearest LOD 0 triangle (by centroid), colour from that material's nearest vertex."""
    cent = lambda pts: tuple(sum(p[k] for p in pts) / 3 for k in range(3))  # noqa: E731
    base_c = [(cent(pts), mat) for pts, _, _, mat in base]
    by_mat = {}
    for pts, _, cols, mat in base:
        by_mat.setdefault(mat, []).extend(zip(pts, cols))
    out = []
    for pts, tuv in zip(tris, uvs):
        c = cent(pts)
        mat = min(base_c, key=lambda e: sum((e[0][k] - c[k]) ** 2 for k in range(3)))[1]
        cols = [min(by_mat[mat], key=lambda e: sum((e[0][k] - p[k]) ** 2 for k in range(3)))[1] for p in pts]
        out.append((pts, tuv, cols, mat))
    return out


def build_textured(results, instances, lods, planes, factors, compact=False):
    """DCI2 with per-material geometries from prototype_geometry.build's results
    and instances, a with_uvs dump, the BSP planes and {GLB material: factor}."""
    import instanced_level
    keep = [i for i, r in enumerate(results) if 'geometry' in r]
    remap = {old: new for new, old in enumerate(keep)}
    geoms, table = [], []
    summary = {'prototypes': len(keep), 'with_lods': 0, 'shared': 0, 'materials': set()}
    for old in keep:
        r = results[old]
        base = [(pts, uvs, cols, mat) for pts, uvs, cols, mat in r['geometry']]
        runs, row, counts = [], [NONE] * 4, [0] * 4
        lod = lods.get(r['name'][:31])
        plane = planes.get(r['name'][:31])
        levels = [base]
        if lod and plane and lod[0] is not None:
            summary['with_lods'] += 1
            previous = lod[0][0]
            for n in (1, 2, 3):
                if not lod[n] or not lod[n][0]:
                    levels.append(None)
                    continue
                if same(lod[n][0], previous):
                    levels.append('shared')
                    summary['shared'] += 1
                    continue
                levels.append(lower_lod(lod[n][0], lod[n][1], base))
                previous = lod[n][0]
        else:
            plane = (NO_SWITCH,) * 3
        for n, tris in enumerate(levels):
            if tris is None:
                continue
            if tris == 'shared':
                row[n], counts[n] = row[n - 1], counts[n - 1]
                continue
            made = material_geometries(tris, r['name'] if n == 0 else f"{r['name'][:24]}:{n}", factors)
            row[n], counts[n] = len(geoms), len(made) - 1
            summary['materials'].update(g['material'] for g in made)
            geoms += made
        runs.append(row)
        table.append((row, plane, counts))
    inst = []
    for i in instances:
        if i['proto'] in remap:
            m = instanced_level.instance_matrix(i)
            rows = [x for row in m[:3] for x in row[:3]] + [x / instanced_level.UNITS_PER_M for x in m[3][:3]]
            inst.append((remap[i['proto']], 0, *rows))
    blob = pack(geoms, table, inst, compact)
    summary['materials'] = sorted(summary['materials'])
    summary.update(geometries=len(geoms), instances=len(inst), package_bytes=len(blob),
                   vertices=sum(len(g['verts']) for g in geoms), indices=sum(len(g['indices']) for g in geoms))
    return blob, summary


UV_STEPS = 2048.0


def quantize(verts):
    """(centre x, y, z, scale) for a geometry's vertices: 16-bit steps over its bounds."""
    lo = [min(v[k] for v in verts) for k in range(3)]
    hi = [max(v[k] for v in verts) for k in range(3)]
    centre = [mf.f32((lo[k] + hi[k]) / 2) for k in range(3)]
    half = max(max(hi[k] - centre[k], centre[k] - lo[k]) for k in range(3))
    return (*centre, mf.f32(max(half / 32767.0, 1e-7)))


def pack(geoms, table, instances, compact=False):
    """table rows are (geometry per LOD, planes) or (geometry per LOD, planes, extra counts)."""
    head = struct.pack('<4s3I', b'DCI3' if compact else b'DCI2', len(table), len(geoms), len(instances))
    gt, vb, ib, qt = bytearray(), bytearray(), bytearray(), bytearray()
    nv = ni = 0
    for g in geoms:
        gt += struct.pack('<5I', len(g['verts']), len(g['indices']), nv, ni, g['material'])
        gt += g['name'].encode()[:31].ljust(32, b'\0')
        if compact:
            q = quantize(g['verts'])
            qt += struct.pack('<4f', *q)
            for v in g['verts']:
                pos = [max(-32767, min(32767, round((v[k] - q[k]) / q[3]))) for k in range(3)]
                uv = [max(-32767, min(32767, round(v[3 + k] * UV_STEPS))) for k in range(2)]
                vb += struct.pack('<5hHI', *pos, *uv, 0, v[5])
        else:
            for v in g['verts']:
                vb += struct.pack('<5fI', *v)
        ib += struct.pack(f'<{len(g["indices"])}H', *g['indices'])
        nv += len(g['verts'])
        ni += len(g['indices'])
    if len(ib) % 4:
        ib += b'\0\0'
    pt = b''.join(struct.pack('<4H3f4B', *r[0], *r[1], *(r[2] if len(r) > 2 else (0, 0, 0, 0))) for r in table)
    inst = b''.join(struct.pack('<2H12f', proto, 0, *m) for proto, _, *m in instances)
    return head + bytes(gt) + pt + bytes(qt) + bytes(vb) + bytes(ib) + inst


def read_dci2(blob):
    """(geometries, prototype table, vertex start, index start, instances) of a
    DCI2 or DCI3 package (DCI3: 16-byte vertices after the quantization table)."""
    magic, np_, ng, ni = struct.unpack_from('<4s3I', blob, 0)
    if magic not in (b'DCI2', b'DCI3'):
        raise ValueError('DCI2 magic')
    stride = 16 if magic == b'DCI3' else 24
    at = 16
    geoms = [struct.unpack_from('<5I', blob, at + 52 * i) + (blob[at + 52 * i + 20:at + 52 * i + 52],) for i in range(ng)]
    at += 52 * ng
    table = [struct.unpack_from('<4H3f4B', blob, at + 24 * i) for i in range(np_)]
    at += 24 * np_
    if stride == 16:
        at += 16 * ng
    nv = sum(g[0] for g in geoms)
    nx = sum(g[1] for g in geoms)
    vstart, istart = at, at + stride * nv
    inst_at = (istart + 2 * nx + 3) & ~3
    instances = [struct.unpack_from('<2H12f', blob, inst_at + 52 * i) for i in range(ni)]
    return geoms, table, vstart, istart, instances


def float_vertices(blob):
    """Every vertex as DCI1's 24-byte float record (DCI3 decoded)."""
    geoms, _, vstart, istart, _ = read_dci2(blob)
    if blob[:4] == b'DCI2':
        return blob[vstart:istart]
    qstart = vstart - 16 * len(geoms)
    out = bytearray()
    for i, g in enumerate(geoms):
        cx, cy, cz, s = struct.unpack_from('<4f', blob, qstart + 16 * i)
        for k in range(g[0]):
            x, y, z, u, v, _, argb = struct.unpack_from('<5hHI', blob, vstart + 16 * (g[2] + k))
            out += struct.pack('<5fI', cx + x * s, cy + y * s, cz + z * s, u / UV_STEPS, v / UV_STEPS, argb)
    return bytes(out)


def lod_index(row, plane, d2):
    """The LOD an instance draws at squared distance d2 (single precision)."""
    f32 = mf.f32
    lod = sum(1 for p in plane[:3] if d2 > f32(f32(p) * f32(p)))
    while row[lod] == NONE:
        lod -= 1
    return lod


def lod_of(row, plane, d2):
    """The (first) geometry an instance draws at squared distance d2."""
    return row[lod_index(row, plane, d2)]


def flatten(blob, tx, tz):
    """The DCI1 a DCI1 reader draws for a DCI2 package seen from target (tx, tz):
    one prototype per geometry, each instance pointing at the geometry its LOD
    picks there."""
    geoms, table, vstart, istart, instances = read_dci2(blob)
    nv = sum(g[0] for g in geoms)
    nx = sum(g[1] for g in geoms)
    head = struct.pack('<4s2I', b'DCI1', len(geoms), len(instances))
    gt = b''.join(struct.pack('<5I', *g[:5]) + g[5] for g in geoms)
    stride = 16 if blob[:4] == b'DCI3' else 24
    data = float_vertices(blob) + blob[istart:istart + 2 * nx]
    if len(data) % 4:
        data += b'\0\0'
    inst = bytearray()
    count = 0
    for proto, _, *m in instances:
        row = table[proto]
        d2 = instance_budget.distance2(m, tx, tz)
        lod = lod_index(row[:4], row[4:7], d2)
        for g in range(row[lod], row[lod] + 1 + row[7 + lod]):  # one instance a material
            inst += struct.pack('<2H12f', g, 0, *m)
            count += 1
    assert nv == (istart - vstart) // stride
    head = struct.pack('<4s2I', b'DCI1', len(geoms), count)
    return head + gt + data + bytes(inst)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('package', type=Path, help='a DCI1 package, or with --textured the background GLB')
    ap.add_argument('dgo', type=Path)
    ap.add_argument('dump_dir', type=Path)
    ap.add_argument('--textured', action='store_true')
    ap.add_argument('--compact', action='store_true', help='DCI3: 16-byte vertices (with --textured)')
    ap.add_argument('--out', type=Path)
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    _, planes = instance_budget.bsp_lod_table(a.dgo)
    if a.textured:
        import prepare_render_sectors as rs
        import prototype_geometry
        results, instances, _ = prototype_geometry.build(a.dgo, a.package)
        doc, _ = rs.read_glb(a.package)
        factors = {i: tuple(m.get('pbrMetallicRoughness', {}).get('baseColorFactor', (1, 1, 1, 1)))
                   for i, m in enumerate(doc.get('materials', []))}
        blob, summary = build_textured(results, instances, dump_lods(a.dump_dir, True), planes, factors, a.compact)
    else:
        blob, summary = build(a.package.read_bytes(), dump_lods(a.dump_dir), planes)
    if a.out:
        a.out.write_bytes(blob)
    text = json.dumps(summary, indent=1)
    if a.json:
        a.json.write_text(text, newline='\n')
    print(text)


if __name__ == '__main__':
    main()

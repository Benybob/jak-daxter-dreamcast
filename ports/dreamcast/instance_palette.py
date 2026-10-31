"""DCI4 native RGB palettes and shared placement slots (AI-assisted), ISC.

Static COLOR_0 only. Unmatched GLB corners retain donor RGB; lower LODs use
the existing nearest LOD-0 transfer, including its slot. No lower-LOD truth
is present in the dump. The binary layout is in instance_pass.h.
"""
from collections import defaultdict
import struct

import tie_lods


def align(n):
    return (n + 3) & ~3


class SlotColour(int):
    def __new__(cls, argb, slot):
        obj = int.__new__(cls, argb)
        obj.slot = slot
        return obj


def prototype(base, rows, native, factors):
    """Slot-bearing triangles, raw RGB palette, and one index table per row."""
    nc = 3 * len(base)
    if not rows or any(len(r) != 3 * nc for r in rows):
        raise ValueError('palette rows')
    lookup, slots, corners = {}, [], []
    for i in range(nc):
        key = b''.join(row[3 * i:3 * i + 3] for row in rows)
        if key not in lookup:
            lookup[key] = len(slots)
            slots.append(i)
        corners.append(lookup[key])
    observed = {tuple(row[3 * i:3 * i + 3]) for row in rows for i in slots}
    palette = list(dict.fromkeys(tuple(c[:3]) for c in native))
    palette += sorted(observed - set(palette))
    palette = [c for c in palette if c in observed]
    if len(slots) > 65536 or len(palette) > 65536:
        raise ValueError('palette u16 limit')
    indices = {c: i for i, c in enumerate(palette)}
    width = 1 if len(palette) <= 256 else 2
    tables = [b''.join(struct.pack('<B' if width == 1 else '<H', indices[tuple(row[3 * i:3 * i + 3])])
                       for i in slots) for row in rows]
    tris = []
    for t, (pts, uvs, cols, mat) in enumerate(base):
        colours = [SlotColour(tie_lods.colour(c, factors.get(mat, (1.0,) * 4)), corners[3 * t + j])
                   for j, c in enumerate(cols)]
        tris.append((pts, uvs, colours, mat))
    return tris, palette, tables, len(slots)


def prepare(results, instances, dgo, glb, factors):
    """Match every kept placement using the report's strict-material oracle."""
    import instance_lighting as il
    import prototype_geometry as pg
    import level_contents
    doc, binary = pg.rs.read_glb(glb)
    world = pg.world_triangles(doc, binary)
    wind = il.CutSource([t for n, ts in world.items() if n.startswith('wind-group') for t in ts])
    name = next(n for n, _, kind in level_contents.objects(dgo) if kind == 'level')
    native = il.native_data(level_contents.object_bytes(dgo, name))
    if len(native) != len(instances):
        raise ValueError('palette instance order')
    own = defaultdict(list)
    for i, inst in enumerate(instances):
        own[inst['proto']].append(i)
    out, matched, unmatched = [], 0, 0
    for pi, r in enumerate(results):
        r = dict(r)
        out.append(r)
        if 'geometry' not in r:
            continue
        ids = own[pi]
        source = il.CutSource(world[r['name']]) if world.get(r['name']) else wind
        raw, masks, _, _ = il.collect(r['geometry'], (source.cut(instances[i]) for i in ids))
        matched += sum(sum(m) for m in masks)
        unmatched += sum(len(m) - sum(m) for m in masks)
        colours = native[ids[0]]['palette'] if native[ids[0]] else []
        tris, palette, tables, slots = prototype(r['geometry'], raw, colours, factors)
        r.update(geometry=tris, palette=palette, slots=slots, tables=dict(zip(ids, tables)))
    return out, {'matched_corners': matched, 'donor_fallback_corners': unmatched,
                 'lower_lod_colours': 'nearest LOD-0 slot; unverified against original lower LODs'}


def extension(geoms, palettes, slots, tables, factors):
    """Resident descriptors/palettes, offsets, then deduplicated index tables."""
    np, ng = len(palettes), len(geoms)
    data = bytearray(16 + 8 * np + 16 * ng)
    for i, (palette, count) in enumerate(zip(palettes, slots)):
        struct.pack_into('<2I', data, 16 + 8 * i, count, len(palette))
    shared = {}
    for i, g in enumerate(geoms):
        pi = g['palette_proto']
        palette = palettes[pi]
        factor = factors.get(g['material'], (1.0,) * 4)
        # Match the exporter's byte/255 float32 before material scaling.
        rgb = [tuple(struct.unpack('<f', struct.pack('<f', x / 255))[0] for x in c) for c in palette]
        baked = b''.join(struct.pack('<I', tie_lods.colour((*c, 1.0), factor) & 0xffffff) for c in rgb)
        if baked not in shared:
            shared[baked] = len(data)
            data += baked
        struct.pack_into('<4I', data, 16 + 8 * np + 16 * i, shared[baked], len(palette), slots[pi],
                         1 if len(palette) <= 256 else 2)
    resident = len(data)
    data += bytes(4 * len(tables))
    shared = {}
    for i, table in enumerate(tables):
        if table not in shared:
            shared[table] = len(data)
            data += table
            data += bytes(align(len(data)) - len(data))
        struct.pack_into('<I', data, resident + 4 * i, shared[table])
    struct.pack_into('<4s3I', data, 0, b'DCP4', len(data), resident, len(tables))
    return bytes(data)


def locate(blob):
    geoms, table, _, istart, instances = tie_lods.read_dci2(blob)
    at = align(istart + 2 * sum(g[1] for g in geoms))
    size, resident, count = struct.unpack_from('<3I', blob, at + 4)
    return at, blob[at:at + size], geoms, table, instances


def baseline(blob):
    """DCI3 donor snapshot for existing geometry/count-only fixture tools."""
    if blob[:4] != b'DCI4':
        return blob
    at, ext, _, _, _ = locate(blob)
    _, _, vstart, istart, _ = tie_lods.read_dci2(blob)
    data = bytearray(b'DCI3' + blob[4:at] + blob[at + len(ext):])
    for pos in range(vstart + 10, istart, 16):
        struct.pack_into('<H', data, pos, 0)
    return bytes(data)


def table(ext, index, proto):
    slots, entries = struct.unpack_from('<2I', ext, 16 + 8 * proto)
    resident, count = struct.unpack_from('<2I', ext, 8)
    if index >= count:
        raise ValueError('palette placement')
    offset, = struct.unpack_from('<I', ext, resident + 4 * index)
    width = 1 if entries <= 256 else 2
    end = offset + slots * width
    if offset < resident + 4 * count or end > len(ext):
        raise ValueError('palette table range')
    raw = ext[offset:end]
    if any(i[0] >= entries for i in struct.iter_unpack('<B' if width == 1 else '<H', raw)):
        raise ValueError('palette index')
    return raw


def colour(blob, placement, geometry, vertex):
    at, ext, geoms, table_rows, instances = locate(blob)
    _, _, vstart, _, _ = tie_lods.read_dci2(blob)
    raw = table(ext, placement, instances[placement][0])
    return vertex_colour(blob, ext, raw, len(table_rows), geometry, vertex, vstart, geoms)


def vertex_colour(blob, ext, raw, prototypes, geometry, vertex, vstart, geoms):
    offset, entries, slots, width = struct.unpack_from('<4I', ext, 16 + 8 * prototypes + 16 * geometry)
    slot, argb = struct.unpack_from('<HI', blob, vstart + 16 * (geoms[geometry][2] + vertex) + 10)
    if slot >= slots or offset + 4 * entries > struct.unpack_from('<I', ext, 8)[0]:
        raise ValueError('palette geometry')
    index, = struct.unpack_from('<B' if width == 1 else '<H', raw, slot * width)
    if index >= entries:
        raise ValueError('palette index')
    return (argb & 0xff000000) | struct.unpack_from('<I', ext, offset + 4 * index)[0]


def flatten(blob, tx, tz):
    """DCI1 snapshot with the selected placement colours, for host references."""
    import instance_budget
    geoms, rows, vstart, istart, instances = tie_lods.read_dci2(blob)
    floats = tie_lods.float_vertices(blob)
    vb, ib, gt, records = bytearray(), bytearray(), bytearray(), bytearray()
    shared, nv, ni = {}, 0, 0
    _, ext, _, _, _ = locate(blob)
    for placement, (proto, _, *matrix) in enumerate(instances):
        row = rows[proto]
        lod = tie_lods.lod_index(row[:4], row[4:7], instance_budget.distance2(matrix, tx, tz))
        raw = table(ext, placement, proto)
        for g in range(row[lod], row[lod] + 1 + row[7 + lod]):
            key = g, raw
            if key not in shared:
                shared[key] = len(shared)
                vc, ic, fv, fi, mat, name = geoms[g]
                gt += struct.pack('<5I32s', vc, ic, nv, ni, mat, name)
                for v in range(vc):
                    xyzuv = struct.unpack_from('<5f', floats, 24 * (fv + v))
                    vb += struct.pack('<5fI', *xyzuv, vertex_colour(blob, ext, raw, len(rows), g, v, vstart, geoms))
                ib += blob[istart + 2 * fi:istart + 2 * (fi + ic)]
                nv += vc
                ni += ic
            records += struct.pack('<2H12f', shared[key], 0, *matrix)
    ib += bytes(align(len(ib)) - len(ib))
    return struct.pack('<4s2I', b'DCI1', len(shared), len(records) // 52) + gt + vb + ib + records

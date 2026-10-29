"""A level's TIE and shrub kept instanced, and its density by cell (AI-assisted), ISC.

The decompiler's background GLB expands every TIE and shrub instance into world
space (village1: 405,043 TIE tris from 35,269 prototype tris). This reads the
level's BSP from the user's own DGO instead (nothing is committed) and keeps the
instancing: each prototype once, with its triangles and vertices per LOD, and
each instance as a reference to it with its position. Then it bins instances
into XZ cells and reports, per cell, the instance records a streamed cell would
carry and the triangles they draw at LOD 0.

TIE counts are exact (tie-fragment vertex-count and num-tris). Shrub prototypes
record no triangle count; their vertices are estimated from the UV block
(stq-qwc, four 4-byte UVs a quadword) and their triangles as one a vertex, as
for strips.

  python instanced_level.py DGO [--level-object NAME] [--cell-size 16] [--json OUT]
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct
import sys

PORT = Path(__file__).resolve().parent
sys.path.insert(0, str(PORT))
import level_contents  # noqa: E402

UNITS_PER_M = 4096.0
INSTANCE_BYTES = 64  # an instance record: a 4x4 matrix, enough for the renderer


def u16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def string_at(data, at):
    length = u32(data, at)
    return data[at + 4:at + 4 + length].decode('ascii', errors='replace')


def model(obj):
    """Prototypes and instances of a BSP object's TIE and shrub trees."""
    code_size, found = level_contents.basics(obj)
    data = obj[16:16 + code_size]  # unlinked: pointer words hold offsets into this
    kinds = {at + 4: type_name for at, type_name in found}  # basic pointer -> type
    order = [(at + 4, type_name) for at, type_name in found]
    index = {p: i for i, (p, _) in enumerate(order)}

    def lods(bucket, kind):
        out = []
        for lod in range(4):
            geo = u32(data, bucket + 12 + 4 * lod)
            if not geo or geo not in kinds:
                out.append({'verts': 0, 'tris': 0})
                continue
            verts = tris = 0
            if kinds[geo] == 'prototype-tie':
                count = struct.unpack_from('<h', data, geo + 2)[0]
                i = index[geo] + 1
                while count and i < len(order):
                    p, t = order[i]
                    i += 1
                    if t == 'tie-fragment':
                        verts += u16(data, p + 32)
                        tris += u16(data, p + 36)
                        count -= 1
            elif kinds[geo] in ('prototype-shrubbery', 'prototype-trans-shrubbery'):
                count = struct.unpack_from('<h', data, geo + 2)[0]
                i = index[geo] + 1
                while count and i < len(order):
                    p, t = order[i]
                    i += 1
                    if t == 'shrubbery':
                        verts += 4 * data[p + 11]  # stq-qwc (type offset 15)
                        count -= 1
                tris = verts
            elif kinds[geo] == 'prototype-generic-shrub':
                count = struct.unpack_from('<h', data, geo + 2)[0]
                i = index[geo] + 1
                while count and i < len(order):
                    p, t = order[i]
                    i += 1
                    if t == 'generic-shrub-fragment':
                        verts += u32(data, p + 4)  # vtx-cnt (type offset 8)
                        count -= 1
                tris = verts
            out.append({'verts': verts, 'tris': tris})
        return out

    prototypes, by_bucket, shrub_buckets = [], {}, []
    for p, t in order:
        if t in ('prototype-bucket-tie', 'prototype-bucket-shrub'):
            kind = 'tie' if t == 'prototype-bucket-tie' else 'shrub'
            by_bucket[p] = len(prototypes)
            if kind == 'shrub':
                shrub_buckets.append(len(prototypes))
            prototypes.append({'name': string_at(data, u32(data, p)), 'kind': kind, 'lods': lods(p, kind),
                               'instances': 0})
    instances = []
    for p, t in order:
        if t == 'instance-tie':
            proto = by_bucket.get(u32(data, p + 8))
        elif t == 'instance-shrubbery':
            i = u16(data, p + 2)
            proto = shrub_buckets[i] if i < len(shrub_buckets) else None
        else:
            continue
        if proto is None:
            raise ValueError(f'{t} at {p:#x} names no prototype')
        x, y, z, r = struct.unpack_from('<4f', data, p + 12)
        prototypes[proto]['instances'] += 1
        instances.append({'proto': proto, 'x': x / UNITS_PER_M, 'y': y / UNITS_PER_M, 'z': z / UNITS_PER_M,
                          'r': r / UNITS_PER_M})
    return prototypes, instances


def density(prototypes, instances, cell):
    cells = collections.defaultdict(lambda: [0, 0])  # records, LOD-0 tris
    for inst in instances:
        key = (math.floor(inst['x'] / cell), math.floor(inst['z'] / cell))
        cells[key][0] += 1
        cells[key][1] += prototypes[inst['proto']]['lods'][0]['tris']
    window = {k: sum(cells.get((k[0] + i, k[1] + j), (0, 0))[1] for i in (-1, 0, 1) for j in (-1, 0, 1))
              for k in cells}
    records = sorted(v[0] for v in cells.values())
    tris = sorted(v[1] for v in cells.values())
    return {'cells': len(cells), 'records_max': records[-1], 'records_median': records[len(records) // 2],
            'record_bytes_max': INSTANCE_BYTES * records[-1], 'lod0_tris_max': tris[-1],
            'lod0_tris_window_3x3_max': max(window.values())}


def summary(prototypes, instances, cell):
    out = {}
    for kind in ('tie', 'shrub'):
        ps = [p for p in prototypes if p['kind'] == kind]
        out[kind] = {
            'prototypes': len(ps),
            'instances': sum(p['instances'] for p in ps),
            'prototype_verts_all_lods': sum(l['verts'] for p in ps for l in p['lods']),
            'prototype_tris_all_lods': sum(l['tris'] for p in ps for l in p['lods']),
            'prototype_tris_lod0': sum(p['lods'][0]['tris'] for p in ps),
            'expanded_tris_lod0': sum(p['lods'][0]['tris'] * p['instances'] for p in ps),
        }
    out['density'] = density(prototypes, instances, cell)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dgo', type=Path)
    parser.add_argument('--level-object', help="the BSP object's name (default: the DGO's *-vis object)")
    parser.add_argument('--cell-size', type=float, default=16.0, help='XZ cell size in meters')
    parser.add_argument('--json', type=Path, help='also write the prototypes and instances here')
    args = parser.parse_args()
    name = args.level_object or next(n for n, _, kind in level_contents.objects(args.dgo) if kind == 'level')
    prototypes, instances = model(level_contents.object_bytes(args.dgo, name))
    if args.json:
        args.json.write_text(json.dumps({'prototypes': prototypes, 'instances': instances}))
    print(json.dumps(summary(prototypes, instances, args.cell_size), indent=1))


if __name__ == '__main__':
    main()

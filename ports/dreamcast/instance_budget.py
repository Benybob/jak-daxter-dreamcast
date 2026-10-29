"""Draw-distance and LOD triangle budgets for a DCI1 package, per cell (AI-assisted), ISC.

    python instance_budget.py PACKAGE [--dgo DGO] [--cell 16] [--distances 20,30,40,60,80]
                              [--lod-switch bsp|15,25,40] [--json OUT]

The camera target is put at the centre of every XZ cell that holds an instance,
and the instance pass's draw rule is applied there (make_instance_fixture.expected:
horizontal distance from the target to the instance's translation, in single
precision). For each draw distance the report gives the instances and triangles
drawn at the worst cell, the median and the 90th percentile.

With the level's DGO the same is done with LOD: instanced_level.model gives each
prototype's triangle count per LOD from the BSP, and an instance beyond the n-th
switch distance draws LOD n (or its last non-empty LOD). The package holds LOD 0
only, so a lower LOD is estimated as the package's LOD 0 triangles scaled by the
BSP's ratio LOD n / LOD 0. TIE only: a shrub's geometry slots are the same plant
in the generic, shrubbery and transparent shrub formats (their padded vertex counts
even grow), not reductions, so shrubs stay at LOD 0 and are reported as unmatched.

The switch distances default to each TIE prototype's own, from its bucket's
`dists` (prototype-h.gc: near-plane, near-stiff, mid-plane, far-plane; meters):
geometry 0 (the tie-near renderer's) within the near plane, 1 to the mid plane,
2 to the far plane, 3 beyond. That reading follows the field names and
tie-methods.gc's per-geometry stats (geometry 0 counted as tie-near, 1 to 3 as
normal TIE); the comparisons in the instance-TIE asm are not checked against it.
"""
import argparse
import json
import math
from pathlib import Path

import make_instance_fixture as mf


def cells(instances, size):
    """The centres of the XZ cells that hold an instance, sorted."""
    keys = {(math.floor(m[9] / size), math.floor(m[11] / size)) for _, _, *m in instances}
    return [((kx + 0.5) * size, (kz + 0.5) * size) for kx, kz in sorted(keys)]


def distance2(m, tx, tz):
    """The instance pass's squared horizontal distance, in single precision."""
    f32 = mf.f32
    dx, dz = f32(f32(m[9]) - f32(tx)), f32(f32(m[11]) - f32(tz))
    return f32(f32(dx * dx) + f32(dz * dz))


def lod_scales(names, lod_table):
    """Per package prototype, the triangle scale of LODs 0..3 from the BSP table
    {name: [tris lod0, lod1, ...]}; prototypes not in it (or with an empty LOD 0)
    keep LOD 0 at every distance. Returns (scales, unmatched names)."""
    scales, unmatched = [], []
    for name in names:
        tris = lod_table.get(name)
        if not tris or not tris[0]:
            unmatched.append(name)
            scales.append([1.0])
            continue
        row, last = [], 1.0
        for t in tris:
            if t:
                last = t / tris[0]
            row.append(last)
        scales.append(row)
    return scales, unmatched


def drawn(protos, instances, tx, tz, distance, switches=(), scales=None):
    """(instances, triangles) the pass draws with the target at (tx, tz). With
    switches, an instance past switches[n-1] draws LOD n, scaled by scales;
    switches is one tuple for every prototype or a list of one per prototype."""
    limit = mf.f32(mf.f32(distance) * mf.f32(distance))
    count = tris = 0
    for proto, _, *m in instances:
        d2 = distance2(m, tx, tz)
        if d2 > limit:
            continue
        count += 1
        full = protos[proto][1] // 3
        if not switches:
            tris += full
            continue
        own = switches[proto] if isinstance(switches, list) else switches
        lod = sum(1 for s in own if d2 > s * s)
        row = scales[proto]
        tris += round(full * row[min(lod, len(row) - 1)])
    return count, tris


def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(q * (len(ordered) - 1) + 0.5))]


def budget(protos, instances, centres, distance, switches=(), scales=None):
    rows = [(drawn(protos, instances, x, z, distance, switches, scales), (x, z)) for x, z in centres]
    (worst_n, worst_t), worst_at = max(rows, key=lambda r: (r[0][1], r[0][0]))
    return {'draw_distance_m': distance, 'worst_tris': worst_t, 'worst_instances': worst_n,
            'worst_cell_centre_m': list(worst_at),
            'median_tris': percentile([r[0][1] for r in rows], 0.5),
            'p90_tris': percentile([r[0][1] for r in rows], 0.9),
            'median_instances': percentile([r[0][0] for r in rows], 0.5)}


def bsp_lod_table(dgo):
    """({TIE prototype name: [LOD 0..3 triangle counts]},
    {TIE prototype name: (near, mid, far) plane in meters}) from a DGO's BSP."""
    import struct
    import instanced_level
    import level_contents
    dgo = Path(dgo)
    name = next(n for n, _, kind in level_contents.objects(dgo) if kind == 'level')
    prototypes, _ = instanced_level.model(level_contents.object_bytes(dgo, name))
    tris = {p['name'][:31]: [l['tris'] for l in p['lods']] for p in prototypes if p['kind'] == 'tie'}
    obj = level_contents.object_bytes(dgo, name)
    code_size, found = level_contents.basics(obj)
    data = obj[16:16 + code_size]
    planes = {}
    for at, type_name in found:
        if type_name == 'prototype-bucket-tie':
            p = at + 4  # dists is at type offset 32
            near, _, mid, far = struct.unpack_from('<4f', data, p + 28)
            planes[instanced_level.string_at(data, instanced_level.u32(data, p))[:31]] = tuple(
                x / instanced_level.UNITS_PER_M for x in (near, mid, far))
    return tris, planes


def report(blob, cell=16.0, distances=(20, 30, 40, 60, 80), switches=(15, 25, 40), lod_table=None, planes=None):
    """planes ({name: (near, mid, far)}), when given, replaces switches per prototype."""
    protos, instances = mf.read(blob)
    names = [blob[12 + 52 * i + 20:12 + 52 * i + 52].split(b'\0')[0].decode('ascii', 'replace')
             for i in range(len(protos))]
    centres = cells(instances, cell)
    out = {'ai_assisted': True, 'prototypes': len(protos), 'instances': len(instances), 'cell_m': cell,
           'cells': len(centres), 'lod0': [budget(protos, instances, centres, d) for d in distances]}
    if lod_table is None:
        out['lod'] = 'skipped: no --dgo'
        return out
    scales, unmatched = lod_scales(names, lod_table)
    out['lod_unmatched_prototypes'] = len(unmatched)
    if planes is not None:
        per = [tuple(planes.get(n, ())) for n in names]
        out['lod_switch_m'] = 'per prototype (BSP near, mid, far planes)'
        out['lod_planes_m'] = sorted({p for p in per if p})
        out['lod'] = [budget(protos, instances, centres, d, per, scales) for d in distances]
    else:
        out['lod_switch_m'] = list(switches)
        out['lod'] = [budget(protos, instances, centres, d, tuple(switches), scales) for d in distances]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('package', type=Path)
    ap.add_argument('--dgo', type=Path)
    ap.add_argument('--cell', type=float, default=16.0)
    ap.add_argument('--distances', default='20,30,40,60,80')
    ap.add_argument('--lod-switch', default='bsp', help="'bsp' (each prototype's planes) or meters, comma separated")
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    distances = [float(x) for x in a.distances.split(',')]
    switches = () if a.lod_switch == 'bsp' else tuple(float(x) for x in a.lod_switch.split(','))
    table, planes = bsp_lod_table(a.dgo) if a.dgo else (None, None)
    out = report(a.package.read_bytes(), a.cell, distances, switches, table,
                 planes if a.lod_switch == 'bsp' else None)
    text = json.dumps(out, indent=1)
    if a.json:
        a.json.write_text(text, newline='\n')
    print(text)


if __name__ == '__main__':
    main()

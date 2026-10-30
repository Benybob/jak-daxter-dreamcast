"""Measure cell-streamed DCI2/DCI3 instance records and prototype eviction.

    python -B instance_streaming.py build/village1-lods.dci3 --cell 16,32,64 \
        --draw 80,40 --json build/instance-streaming.json

Each record belongs to floor(translation / cell) in XZ. Load every occupied
cell whose closed bounds can pass the draw rule, then draw-test its records
with make_instance_fixture's float32 arithmetic. JSON cell catalogs contain
original, zero-based record IDs; a sample's resident_cells selects those lists.
Geometry eviction retains every LOD/material geometry of prototypes with a
record within D, counting shared geometries once. Tables and padding stay
resident. This estimates a future layout; the C loader still needs the entire
contiguous package.

The path is make_flythrough_fixture.path(mf.camera(...), 16, 30). The level
sweep samples the centres of ALL 16 m cells in the instance-translation bounds,
including empty cells, independently of streaming cell size. Empty-residency
samples are implicit in JSON. Sparse candidate generation avoids iterating the
large empty rectangles between distant background instances. Sampled peaks
are not continuous worst-case bounds. No prefetch, overlap during replacement,
I/O, allocator, catalog, texture, terrain, scratch or gameplay costs are included.
(AI-assisted), ISC.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import instance_budget
import make_flythrough_fixture as fly
import make_instance_fixture as mf
import tie_lods

INSTANCE_BYTES = struct.calcsize('<2H12f')
SWEEP_CELL = 16.0


def positive(value, name):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f'{name} must be finite and positive')
    return value


class Package:
    """Read-only package accounting; geometry bytes mean vertices and indices."""

    def __init__(self, blob):
        if len(blob) < 16 or blob[:4] not in (b'DCI2', b'DCI3'):
            raise ValueError('expected a DCI2 or DCI3 package')
        magic, np_, ng, ni = struct.unpack_from('<4s3I', blob)
        if not 0 < np_ <= 65535 or not 0 < ng <= 65535 or ni > 1000000:
            raise ValueError('invalid package counts')
        stride = 16 if magic == b'DCI3' else 24
        vstart = 16 + 52 * ng + 24 * np_ + (16 * ng if stride == 16 else 0)
        if len(blob) < vstart:
            raise ValueError('truncated package tables')
        nv = nx = 0
        for i in range(ng):
            vc, ic, fv, fi = struct.unpack_from('<4I', blob, 16 + 52 * i)
            if not 0 < vc <= 65536 or not ic or ic % 3 or fv != nv or fi != nx:
                raise ValueError('invalid geometry ranges')
            nv += vc
            nx += ic
        index_end = vstart + stride * nv + 2 * nx
        instance_start = (index_end + 3) & ~3
        if instance_start + INSTANCE_BYTES * ni != len(blob):
            raise ValueError('package size does not match tables')
        geoms, table, _, _, instances = tie_lods.read_dci2(blob)
        self.prototype_geometries = []
        for row in table:
            if row[0] >= ng:
                raise ValueError('invalid LOD 0 geometry')
            used = set()
            for lod, first in enumerate(row[:4]):
                extra = row[7 + lod]
                if first == tie_lods.NONE:
                    if extra:
                        raise ValueError('extra geometries for absent LOD')
                elif first + extra >= ng:
                    raise ValueError('invalid LOD geometry run')
                else:
                    used.update(range(first, first + extra + 1))
            self.prototype_geometries.append(used)
        self.instances = instances
        self.matrices = [row[2:] for row in instances]
        if any(row[0] >= np_ or any(not math.isfinite(x) or abs(x) > 1e6 for x in row[2:])
               for row in instances):
            raise ValueError('invalid instance reference or matrix')
        self.geometry_bytes = [stride * g[0] + 2 * g[1] for g in geoms]
        self.accounting = dict(
            format=magic.decode('ascii'), package_bytes=len(blob), prototypes=np_, geometries=ng,
            instances=ni, instance_stride=INSTANCE_BYTES, header_bytes=16,
            geometry_table_bytes=52 * ng, prototype_table_bytes=24 * np_,
            quantization_bytes=16 * ng if stride == 16 else 0,
            vertex_stride=stride, vertices=nv, vertex_bytes=stride * nv,
            indices=nx, index_bytes=2 * nx, index_padding_bytes=instance_start - index_end,
            geometry_bytes=sum(self.geometry_bytes), instance_bytes=INSTANCE_BYTES * ni,
            fixed_bytes=vstart + instance_start - index_end,
            sha256=hashlib.sha256(blob).hexdigest())


class Cells:
    """Whole-cell residency, with a catalog that resolves each sample to records."""

    def __init__(self, package, cell):
        self.package = package
        self.cell = positive(cell, 'cell')
        self.records = {}
        for i, m in enumerate(package.matrices):
            key = (math.floor(m[9] / self.cell), math.floor(m[11] / self.cell))
            self.records.setdefault(key, []).append(i)
        self.records = dict(sorted(self.records.items()))
        # Rounded closed endpoints are conservative for float32 translations:
        # the nearest representable point cannot be farther than a record in it.
        self.bounds = {k: tuple(mf.f32(v) for v in
                               (k[0] * self.cell, (k[0] + 1) * self.cell,
                                k[1] * self.cell, (k[1] + 1) * self.cell))
                       for k in self.records}

    def catalog(self):
        return [dict(cell=list(k), instance_ids=ids) for k, ids in self.records.items()]

    def query(self, target, draw=mf.DRAW_DISTANCE):
        draw = mf.f32(positive(draw, 'draw'))
        if not math.isfinite(draw) or not draw:
            raise ValueError('draw must be representable as a positive float32')
        limit = mf.f32(draw * draw)
        tx, ty, tz = (mf.f32(float(v)) for v in target)
        if not all(math.isfinite(v) for v in (tx, ty, tz, limit)):
            raise ValueError('target and squared draw distance must be finite float32')
        cells, active = [], set()
        resident = drawn = 0
        xs, zs = {}, {}
        for key, ids in self.records.items():
            x0, x1, z0, z1 = self.bounds[key]
            if key[0] not in xs:
                dx = mf.f32(min(max(tx, x0), x1) - tx)
                xs[key[0]] = mf.f32(dx * dx)
            if key[1] not in zs:
                dz = mf.f32(min(max(tz, z0), z1) - tz)
                zs[key[1]] = mf.f32(dz * dz)
            if mf.f32(xs[key[0]] + zs[key[1]]) > limit:
                continue
            cells.append(list(key))
            resident += len(ids)
            for i in ids:
                if instance_budget.distance2(self.package.matrices[i], tx, tz) <= limit:
                    drawn += 1
                    active.add(self.package.instances[i][0])
        geometries = set()
        for proto in active:
            geometries.update(self.package.prototype_geometries[proto])
        geometry = sum(self.package.geometry_bytes[g] for g in geometries)
        records = resident * INSTANCE_BYTES
        full = self.package.accounting
        return dict(target_m=[tx, ty, tz], resident_cells=cells,
                    resident_instances=resident, drawn_instances=drawn,
                    overfetch_instances=resident - drawn, record_bytes=records,
                    record_freed_bytes=full['instance_bytes'] - records,
                    active_prototypes=sorted(active), geometry_resident_count=len(geometries),
                    geometry_bytes=geometry, geometry_freed_bytes=full['geometry_bytes'] - geometry,
                    resident_bytes=full['fixed_bytes'] + records + geometry,
                    freed_bytes=full['package_bytes'] - full['fixed_bytes'] - records - geometry)


def summary(rows, samples=None):
    """Independent extrema, each with its own target; maxima need not coincide."""
    out = dict(samples=len(rows) if samples is None else samples)
    for key in ('resident_instances', 'drawn_instances', 'record_bytes', 'record_freed_bytes',
                'geometry_bytes', 'geometry_freed_bytes', 'resident_bytes', 'freed_bytes'):
        lo = min(rows, key=lambda r: r[key])
        hi = max(rows, key=lambda r: r[key])
        out[key] = dict(min=lo[key], max=hi[key], min_target_m=lo['target_m'], max_target_m=hi['target_m'])
    return out


def sweep(index, draw):
    """Full rectangular 16 m centre grid; only nonempty residency rows are stored."""
    if not index.records:
        return dict(grid_m=SWEEP_CELL, samples=0, rows=[], summary=None)
    m = index.package.matrices
    ix0, ix1 = (math.floor(f(v[9] for v in m) / SWEEP_CELL) for f in (min, max))
    iz0, iz1 = (math.floor(f(v[11] for v in m) / SWEEP_CELL) for f in (min, max))
    # A resident cell must be within D in each axis. One extra sample each side
    # protects the candidate enumeration from float32 boundary rounding; query()
    # applies the actual rounded squared-distance test.
    candidates = set()
    radius = mf.f32(positive(draw, 'draw'))
    for x0, x1, z0, z1 in index.bounds.values():
        ax = max(ix0, math.floor((x0 - radius) / SWEEP_CELL - 0.5) - 1)
        bx = min(ix1, math.ceil((x1 + radius) / SWEEP_CELL - 0.5) + 1)
        az = max(iz0, math.floor((z0 - radius) / SWEEP_CELL - 0.5) - 1)
        bz = min(iz1, math.ceil((z1 + radius) / SWEEP_CELL - 0.5) + 1)
        candidates.update((ix, iz) for ix in range(ax, bx + 1) for iz in range(az, bz + 1))
    rows = []
    occupied = set()
    empty = None
    for ix, iz in sorted(candidates):
        row = index.query(((ix + 0.5) * SWEEP_CELL, 0.0, (iz + 0.5) * SWEEP_CELL), draw)
        if row['resident_instances']:
            rows.append(row)
            occupied.add((ix, iz))
        elif empty is None:
            empty = row
    total = (ix1 - ix0 + 1) * (iz1 - iz0 + 1)
    if empty is None and len(rows) < total:
        key = next((ix, iz) for ix in range(ix0, ix1 + 1) for iz in range(iz0, iz1 + 1)
                   if (ix, iz) not in occupied)
        empty = index.query(((key[0] + 0.5) * SWEEP_CELL, 0.0, (key[1] + 0.5) * SWEEP_CELL), draw)
    return dict(grid_m=SWEEP_CELL, grid_index_bounds=[ix0, ix1, iz0, iz1], samples=total,
                nonempty_samples=len(rows), omitted_empty_samples=total - len(rows),
                summary=summary(rows + ([empty] if empty else []), total), rows=rows)


def report(blob, cells=(16.0,), draws=(mf.DRAW_DISTANCE,)):
    package = Package(blob)
    cells = [positive(c, 'cell') for c in cells]
    draws = [positive(d, 'draw') for d in draws]
    if not cells or not draws:
        raise ValueError('cell and draw lists must be nonempty')
    if not package.instances:
        raise ValueError('a camera path needs at least one instance')
    start = mf.camera(*mf.read(blob))
    targets = fly.path(start, 16, 30)
    runs = []
    for cell in cells:
        index = Cells(package, cell)
        for draw in draws:
            rows = [index.query(t, draw) for t in targets]
            runs.append(dict(cell_m=cell, draw_m=draw, occupied_cells=len(index.records),
                             cell_catalog=index.catalog(),
                             path=dict(frames=16, length_m=30, rows=rows, summary=summary(rows)),
                             sweep=sweep(index, draw)))
    return dict(ai_assisted=True, package=package.accounting,
                policy='Whole occupied XZ cells intersecting the float32 draw disk; all LODs/materials '
                       'of prototypes with instances within D; shared geometry counted once.',
                record_ids='Zero-based original instance-table indices, resolved via cell_catalog and resident_cells.',
                sweep_policy='All 16 m cell centres in translation bounds, including empty cells; '
                             'identical grid for every run. Empty residency rows are implicit. '
                             'Sampled extrema are not bounds between samples.',
                exclusions=['prefetch and hysteresis', 'old/new replacement overlap', 'I/O cost',
                            'cell catalog and runtime indirection', 'allocator overhead and fragmentation',
                            'textures and VRAM', 'terrain', 'view/bounds scratch', 'gameplay memory'],
                runtime='Sizing model only: instance_pass.c still opens an entirely resident contiguous package.',
                runs=runs)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('package', type=Path)
    parser.add_argument('--cell', default='16', help='cell sizes in meters, comma separated')
    parser.add_argument('--draw', default=str(mf.DRAW_DISTANCE), help='draw distances in meters, comma separated')
    parser.add_argument('--json', type=Path, help='save full report (real outputs belong under build/)')
    args = parser.parse_args(argv)
    try:
        cells = [positive(c, 'cell') for c in args.cell.split(',')]
        draws = [positive(d, 'draw') for d in args.draw.split(',')]
        if args.json and args.json.resolve() == args.package.resolve():
            raise ValueError('JSON output must not overwrite the input package')
        result = report(args.package.read_bytes(), cells, draws)
        text = json.dumps(result, indent=1, allow_nan=False) + '\n'
        if args.json:
            args.json.write_text(text, encoding='utf-8', newline='\n')
        print(text, end='')
    except (OSError, ValueError, OverflowError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

"""Measure streamed TIE placement lighting against GLB corners (AI-assisted), ISC.

    python instance_lighting.py --levels-root LEVELS --dgo-dir DGO --dump-dir DUMPS \
        --json build/instance-lighting.json

A is prototype_geometry's mean before material scaling/clamping. B fits one
nonnegative least-squares RGB multiplier to that packed mean, stored as three
Q4.12 u16s (8 bytes with alignment). C stores exact ARGB per lighting vertex,
deduplicating whole placement tables per prototype. D uses the original
prototype palette at COLOR_0/time 0; equal RGB entries merge and unused entries
are removed for this static snapshot. Corners with identical raw RGB trajectories over ALL placements share
one slot. A placement stores palette indices per slot, deduplicated like C.
This recovers observable sharing, not the VU's color_index_index provenance.
Missing palette colours get explicit extra entries and are counted.

Errors are absolute byte-channel errors after tie_lods.colour, counted once per
matched triangle corner per placement, never once per prototype or unique
vertex. P95 is nearest rank, separately for R/G/B and for pooled channels.
Unmatched corners are excluded and reported; their storage uses donor fallback.
Only TIE LOD 0 is scored: the GLB has no lower LOD truth, and debug OBJs have no
colours. Native DGO index sizes for all four LODs are measured separately.

Storage is an explicit proposed layout, not an implemented DCI extension:
4-byte table offset per record (the current pad holds its original index),
4-byte-aligned tables, deduplicated within each cell, 8-byte resident descriptor
per prototype, and for D 4-byte palette offset per material plus ARGB entries
(identical baked material palettes within a prototype share one table).
The DCI3 vertex's existing zero u16 pad holds a slot, so its stride stays 16.
Vertices merged by today's mean but having different lighting trajectories must
split; their extra 16-byte records are included. Resident/global table costs and
cell-local duplication are both reported. No whole-level residency is assumed.
CPU figures are explicit illustrative cycle budgets, NOT SH-4 measurements.
Analysis tool (AI-assisted), ISC.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import struct
import sys

import instance_cells
import instanced_level
import level_contents
import prototype_geometry as pg
import tie_lods


LEVEL_DGOS = {'village1': 'VI1', 'village3': 'VI3', 'misty': 'MIS', 'beach': 'BEA'}
OPTIONS = ('A_mean', 'B_tint', 'C_argb', 'D_palette_slots')
INSTANCE_BYTES = instance_cells.INSTANCE.size


def align4(n):
    return (n + 3) & ~3


class Errors:
    """Exact bounded histograms; merge counts rather than prototype averages."""
    def __init__(self):
        self.hist = [[0] * 256 for _ in range(3)]

    def add(self, predicted, truth):
        for k in range(3):
            self.hist[k][abs(predicted[k] - truth[k])] += 1

    def merge(self, other):
        for a, b in zip(self.hist, other.hist):
            for i, count in enumerate(b):
                a[i] += count

    @staticmethod
    def stats(hist):
        count = sum(hist)
        if not count:
            return None, None
        rank, cumulative = math.ceil(count * 0.95), 0
        for p95, n in enumerate(hist):
            cumulative += n
            if cumulative >= rank:
                break
        return sum(i * n for i, n in enumerate(hist)) / count, p95

    def report(self):
        stats = [self.stats(h) for h in self.hist]
        pooled = [sum(h[i] for h in self.hist) for i in range(256)]
        mean, p95 = self.stats(pooled)
        return {'corners': sum(self.hist[0]), 'mean_rgb': [s[0] for s in stats],
                'p95_rgb': [s[1] for s in stats], 'mean_channel': mean, 'p95_channel': p95}


def rgb(argb):
    return ((argb >> 16) & 255, (argb >> 8) & 255, argb & 255)


def pack_rgb(c, factor):
    return rgb(tie_lods.colour((*c, 1.0), factor))


def glb_rgb(c):
    """Exporter stores each palette byte / 255 as float32, before scaling."""
    return tuple(struct.unpack('<f', struct.pack('<f', v / 255))[0] for v in c)


def fit_tint(mean, truth, valid):
    """Corner-weighted LS, then Q4.12 quantization; zero mean implies gain 1."""
    gains = []
    for k in range(3):
        numerator = sum(m[k] * t[k] for m, t, ok in zip(mean, truth, valid) if ok)
        denominator = sum(m[k] ** 2 for m, ok in zip(mean, valid) if ok)
        gain = numerator / denominator if denominator else 1.0
        gains.append(max(0, min(65535, round(gain * 4096))))
    return tuple(gains)


def tint(mean, gains):
    """Integer multiply, half-up rounding, saturate; alpha is left untouched."""
    return tuple(min(255, (m * g + 2048) >> 12) for m, g in zip(mean, gains))


class CutSource:
    """Centroid XZ grid accelerates pg.cut without changing its sphere test."""
    def __init__(self, triangles, cell=16.0):
        self.triangles, self.cell, self.grid = triangles, cell, defaultdict(list)
        for i, (pts, _, _, _) in enumerate(triangles):
            key = tuple(math.floor(sum(p[k] for p in pts) / (3 * cell)) for k in (0, 2))
            self.grid[key].append(i)

    def cut(self, inst):
        reach = inst['r'] * 1.01 + pg.TOLERANCE_M
        x0, x1 = (math.floor((inst['x'] + s * reach) / self.cell) for s in (-1, 1))
        z0, z1 = (math.floor((inst['z'] + s * reach) / self.cell) for s in (-1, 1))
        if (x1 - x0 + 1) * (z1 - z0 + 1) > 3 * len(self.grid):
            candidates = self.triangles
        else:
            ids = sorted(i for x in range(x0, x1 + 1) for z in range(z0, z1 + 1)
                         for i in self.grid.get((x, z), ()))
            candidates = [self.triangles[i] for i in ids]
        return pg.cut(candidates, inst)


def select_base(cuts, expected):
    """Same first-eight selection and expected triangle count as pg.build (TIE)."""
    nonempty = [c for c in cuts if c]
    for i, a in enumerate(nonempty):
        for b in nonempty[i + 1:]:
            common = pg.shared(a, b)
            if common and len(common) == expected:
                return common
    if len(nonempty) == 1 and len(nonempty[0]) == expected:
        return nonempty[0]
    return None


def collect(base, cuts):
    """Mean exactly as pg.mean_vertex_colours; strict material matches for truth.

    Raw byte arrays save memory. Keep float sums for the flag's exact rounding.
    Position-only matches with a different material affect today's mean but are
    not trusted as placement ground truth. They are counted, not silently scored.
    """
    nc = 3 * len(base)
    donor = bytes(max(0, min(255, round(c[k] * 255))) for tri in base for c in tri[2] for k in range(3))
    sums, counts = [[0.0] * 3 for _ in range(nc)], [0] * nc
    rows, masks, mismatches = [], [], 0
    for other in cuts:
        raw, valid = bytearray(donor), bytearray(nc)
        for i, j in pg.shared_matches(base, other):
            for corner, c in enumerate(other[j][2]):
                v = 3 * i + corner
                counts[v] += 1
                for k in range(3):
                    sums[v][k] += c[k]
                if base[i][3] == other[j][3]:
                    raw[3 * v:3 * v + 3] = bytes(max(0, min(255, round(c[k] * 255))) for k in range(3))
                    valid[v] = 1
            mismatches += int(base[i][3] != other[j][3])
        rows.append(bytes(raw))
        masks.append(bytes(valid))
    means = [tuple(s / counts[i] for s in sums[i]) if counts[i] else tuple(base[i // 3][2][i % 3][:3])
             for i in range(nc)]
    return rows, masks, means, mismatches


def native_data(obj):
    """Jak 1 unlinked pointers; offsets are relative to the basic pointer (+4).

    See prototype-h.gc, BspHeader.cpp and extract_tie.cpp: palette width 8,
    frag-count at +108, index-start +112, base-qw +116, tie-colors +140,
    inline color-index-qwc +144; instance color-indices at +4, bucket at +8.
    """
    size, basics = level_contents.basics(obj)
    data = obj[16:16 + size]

    def read(fmt, at):
        if at < 0 or at + struct.calcsize(fmt) > len(data):
            raise ValueError('native lighting range')
        return struct.unpack_from(fmt, data, at)

    buckets = {}
    for at, kind in basics:
        if kind != 'prototype-bucket-tie':
            continue
        p = at + 4
        palette_at, = read('<I', p + 140)
        width, height, pad = read('<3I', palette_at)
        if width != 8 or not 0 < height <= 256 or pad:
            raise ValueError('native TIE palette dimensions')
        samples = [tuple(read('<4B', palette_at + 12 + (i * 8 + t) * 4) for i in range(height))
                   for t in range(8)]
        counts, starts, bases = read('<4B', p + 108), read('<4B', p + 112), read('<4H', p + 116)
        lengths = [16 * sum(read('<B', p + 144 + starts[lod] + i)[0] for i in range(counts[lod]))
                   for lod in range(4)]
        buckets[p] = {'samples': samples, 'lengths': lengths, 'bases': bases}
    instances = []
    for at, kind in basics:
        if kind == 'instance-shrubbery':
            instances.append(None)
        elif kind == 'instance-tie':
            p = at + 4
            pointer, bucket = read('<2I', p + 4)
            b = buckets[bucket]
            streams = []
            for base, length in zip(b['bases'], b['lengths']):
                start = pointer + base * 16
                if start < 0 or start + length > len(data):
                    raise ValueError('native colour index range')
                streams.append(data[start:start + length])
            instances.append({'palette': b['samples'][0], 'streams': streams,
                              'palette_entries': len(b['samples'][0])})
    return instances


def table_cost(payloads, cells, resident=0):
    """All byte counts include alignment, offsets and per-cell duplication."""
    if len(payloads) != len(cells):
        raise ValueError('one table and cell per placement required')
    grouped = defaultdict(set)
    for payload, cell in zip(payloads, cells):
        grouped[cell].add(payload)
    globally = sum(align4(len(p)) for p in set(payloads))
    by_cell = {cell: sum(align4(len(p)) for p in tables) for cell, tables in grouped.items()}
    counts = Counter(cells)
    streamed = 4 * len(payloads) + sum(by_cell.values())
    return {'placements': len(payloads), 'reference_bytes': 4 * len(payloads),
            'unique_tables': len(set(payloads)), 'global_payload_bytes': globally,
            'cell_payload_bytes': sum(by_cell.values()), 'resident_bytes': resident,
            'streamed_added_bytes': streamed, 'added_bytes': resident + streamed,
            'added_bytes_per_placement': (resident + streamed) / len(payloads) if payloads else 0,
            'streamed_bytes_per_placement': streamed / len(payloads) if payloads else 0,
            'cells': [{'x': cell[0], 'z': cell[1], 'added_bytes': 4 * counts[cell] + by_cell[cell]}
                      for cell in sorted(grouped)]}


def analyse_prototype(base, raw, masks, means, factors, cells, native_palette):
    """Score one prototype and encode the proposed C/D tables for exact sizes."""
    nc = len(means)
    corner_factors = [factors.get(tri[3], (1.0,) * 4) for tri in base for _ in range(3)]
    mean = [pack_rgb(c, f) for c, f in zip(means, corner_factors)]
    truth = [[pack_rgb(glb_rgb(row[3 * i:3 * i + 3]), corner_factors[i])
              for i in range(nc)] for row in raw]
    errors = {o: Errors() for o in OPTIONS}
    gains = [fit_tint(mean, row, mask) for row, mask in zip(truth, masks)]
    # A trajectory is raw RGB for this corner across every placement. This also
    # stops mean-colour vertex merging from discarding placement discontinuities.
    trajectories = [b''.join(row[3 * i:3 * i + 3] for row in raw) for i in range(nc)]
    vertex_lookup, vertices, slot_lookup, slots, corner_vertices = {}, [], {}, [], []
    baseline_vertices = set()
    material_vertices = Counter()
    for i in range(nc):
        pts, uvs, cols, mat = base[i // 3]
        corner = i % 3
        spatial = (mat, tuple(round(x * 1000) for x in pts[corner]), tuple(uvs[corner]))
        alpha = tie_lods.colour((*means[i], *cols[corner][3:]), corner_factors[i]) & 0xff000000
        baseline_vertices.add((spatial, mean[i], alpha))
        key = (spatial, trajectories[i], alpha, mean[i])
        if key not in vertex_lookup:
            vertex_lookup[key] = len(vertices)
            vertices.append((i, alpha))
            material_vertices[mat] += 1
        corner_vertices.append(vertex_lookup[key])
        if trajectories[i] not in slot_lookup:
            slot_lookup[trajectories[i]] = len(slots)
            slots.append(i)
    if len(slots) > 65536 or max(material_vertices.values(), default=0) > 65535:
        raise ValueError('lighting exceeds DCI3 u16 vertex/slot indexing')
    # Deduplicate the original time-0 palette, adding and counting unmatched RGB.
    palette = list(dict.fromkeys(tuple(c[:3]) for c in native_palette))
    palette_lookup = {c: i for i, c in enumerate(palette)}
    native_count, missing = len(palette), set()
    for row in raw:
        for i in slots:
            c = tuple(row[3 * i:3 * i + 3])
            if c not in palette_lookup:
                missing.add(c)
                palette_lookup[c] = len(palette)
                palette.append(c)
    observed = {tuple(row[3 * i:3 * i + 3]) for row in raw for i in slots}
    palette = [c for c in palette if c in observed]
    palette_lookup = {c: i for i, c in enumerate(palette)}
    width = 1 if len(palette) <= 256 else 2
    if len(palette) > 65536:
        raise ValueError('palette exceeds u16')
    c_tables, d_tables = [], []
    # The material chooses a pre-baked palette; no material multiply at runtime.
    material_ids = sorted(material_vertices)
    baked = {mat: [pack_rgb(glb_rgb(c), factors.get(mat, (1.0,) * 4))
                   for c in palette] for mat in material_ids}
    for placement, (row, mask) in enumerate(zip(truth, masks)):
        indices = [palette_lookup[tuple(raw[placement][3 * i:3 * i + 3])] for i in slots]
        d_tables.append(struct.pack('<' + ('B' if width == 1 else 'H') * len(indices), *indices))
        c_tables.append(b''.join(struct.pack('<I', alpha | (row[i][0] << 16) | (row[i][1] << 8) | row[i][2])
                                  for i, alpha in vertices))
        for i, ok in enumerate(mask):
            if not ok:
                continue
            errors['A_mean'].add(mean[i], row[i])
            errors['B_tint'].add(tint(mean[i], gains[placement]), row[i])
            # Decode the actual representations, rather than assuming zero error.
            col, = struct.unpack_from('<I', c_tables[-1], 4 * corner_vertices[i])
            errors['C_argb'].add(rgb(col), row[i])
            slot = slot_lookup[trajectories[i]]
            index, = struct.unpack_from('<B' if width == 1 else '<H', d_tables[-1], width * slot)
            errors['D_palette_slots'].add(baked[base[i // 3][3]][index], row[i])
    split_bytes = 16 * (len(vertices) - len(baseline_vertices))
    c_cost = table_cost(c_tables, cells, 8 + split_bytes)
    palette_tables = len({tuple(p) for p in baked.values()})
    d_cost = table_cost(d_tables, cells, 8 + split_bytes + 4 * len(material_ids) + 4 * len(palette) * palette_tables)
    costs = {'A_mean': {'resident_bytes': 0, 'streamed_added_bytes': 0, 'added_bytes': 0},
             'B_tint': {'resident_bytes': 0, 'streamed_added_bytes': 8 * len(raw), 'added_bytes': 8 * len(raw)},
             'C_argb': c_cost, 'D_palette_slots': d_cost}
    counts = Counter(cells)
    for option, stride in (('A_mean', 0), ('B_tint', 8)):
        costs[option]['cells'] = [{'x': c[0], 'z': c[1], 'added_bytes': stride * counts[c]}
                                  for c in sorted(counts)]
    return {'placements': len(raw), 'expected_corners': nc * len(raw),
            'matched_corners': sum(sum(m) for m in masks),
            'complete_placements': sum(all(m) for m in masks),
            'placements_with_matches': sum(any(m) for m in masks),
            'baseline_vertices': len(baseline_vertices), 'lighting_vertices': len(vertices),
            'extra_vertex_bytes': split_bytes, 'lighting_slots': len(slots),
            'palette_index_bytes': width, 'native_palette_unique_rgb': native_count,
            'palette_extra_rgb': len(missing), 'palette_entries': len(palette),
            'baked_palette_tables': palette_tables, 'materials': len(material_ids),
            'colour_scratch_bytes_max_material': 4 * max(material_vertices.values(), default=0),
            'tint_saturated_channels': sum(g == 65535 for row in gains for g in row),
            'costs': costs}, errors


def native_summary(records):
    out = []
    for lod in range(4):
        streams = [r['streams'][lod] for r in records]
        out.append({'lod': lod, 'raw_index_bytes': sum(map(len, streams)),
                    'deduplicated_index_bytes': sum(map(len, set(streams))),
                    'unique_streams': len(set(streams))})
    return out


def sampled_working_set(cells, draw=30.0):
    """Added streamed bytes at occupied cell centres, using instance_cells rules.

    This is a measured grid sample, not a continuous-space bound or a replay of
    the existing fixture path. Resident palette/topology and scratch are separate.
    """
    if not cells:
        return {'draw_metres': draw, 'samples': 0, 'max_added_bytes': 0, 'target_xz': None}
    maximum, target = -1, None
    for ix, iz in sorted(cells):
        x, z = (ix + 0.5) * instance_cells.CELL, (iz + 0.5) * instance_cells.CELL
        total = sum(size for (cx, cz), size in cells.items() if instance_cells.touches(cx, cz, x, z, draw))
        if total > maximum:
            maximum, target = total, [x, z]
    return {'draw_metres': draw, 'samples': len(cells), 'max_added_bytes': maximum, 'target_xz': target}


def analyse_level(level, dgo, glb, dump_dir=None):
    name = next(n for n, _, kind in level_contents.objects(dgo) if kind == 'level')
    obj = level_contents.object_bytes(dgo, name)
    prototypes, instances = instanced_level.model(obj)
    native = native_data(obj)
    if len(native) != len(instances):
        raise ValueError('native and model instance order disagree')
    doc, binary = pg.rs.read_glb(glb)
    factors = {i: tuple(m.get('pbrMetallicRoughness', {}).get('baseColorFactor', (1.0,) * 4))
               for i, m in enumerate(doc.get('materials', []))}
    world = pg.world_triangles(doc, binary)
    del doc, binary
    wind = CutSource([t for n, group in world.items() if n.startswith('wind-group') for t in group])
    by_proto = defaultdict(list)
    for i, inst in enumerate(instances):
        by_proto[inst['proto']].append(i)
    totals = {o: Errors() for o in OPTIONS}
    rows, skipped = [], []
    for pi, proto in enumerate(prototypes):
        if proto['kind'] != 'tie' or not by_proto[pi]:
            continue
        own = [instances[i] for i in by_proto[pi]]
        source = CutSource(world[proto['name']]) if world.get(proto['name']) else wind
        cuts = [source.cut(inst) for inst in own[:8]]
        base = select_base(cuts, proto['lods'][0]['tris'])
        if base is None:
            skipped.append({'name': proto['name'], 'placements': len(own),
                            'bsp_triangles': proto['lods'][0]['tris'],
                            'cut_triangles': [len(c) for c in cuts]})
            continue

        def all_cuts():
            yield from cuts
            for inst in own[8:]:
                yield source.cut(inst)

        raw, masks, means, mismatches = collect(base, all_cuts())
        cells = []
        for inst in own:
            m = instanced_level.instance_matrix(inst)
            # Exactly the float32 translation serialized into instance_cells.
            x, z = (struct.unpack('<f', struct.pack('<f', m[3][k] / instanced_level.UNITS_PER_M))[0]
                    for k in (0, 2))
            cells.append((math.floor(x / instance_cells.CELL), math.floor(z / instance_cells.CELL)))
        records = [native[i] for i in by_proto[pi]]
        row, errors = analyse_prototype(base, raw, masks, means, factors, cells, records[0]['palette'])
        row.update(name=proto['name'], material_mismatched_triangles=mismatches,
                   native=native_summary(records), errors={o: e.report() for o, e in errors.items()})
        rows.append(row)
        for o in OPTIONS:
            totals[o].merge(errors[o])
        if len(rows) % 20 == 0:
            print(f'{level}: {len(rows)} prototypes measured (AI-assisted)', file=sys.stderr, flush=True)
    measured = sum(r['placements'] for r in rows)
    options = {}
    for o in OPTIONS:
        resident = sum(r['costs'][o]['resident_bytes'] for r in rows)
        streamed = sum(r['costs'][o]['streamed_added_bytes'] for r in rows)
        options[o] = {'error': totals[o].report(), 'resident_added_bytes': resident,
                      'streamed_added_bytes': streamed, 'added_bytes': resident + streamed,
                      'added_bytes_per_placement': (resident + streamed) / measured if measured else 0,
                      'streamed_bytes_per_placement': streamed / measured if measured else 0,
                      'record_bytes': 52 + (0 if o == 'A_mean' else 8 if o == 'B_tint' else 4)}
        cell_bytes = defaultdict(int)
        for row in rows:
            for c in row['costs'][o]['cells']:
                cell_bytes[c['x'], c['z']] += c['added_bytes']
        working = sampled_working_set(cell_bytes)
        working['resident_plus_sampled_streamed_bytes'] = resident + working['max_added_bytes']
        options[o]['sampled_working_set'] = working
        if o in ('C_argb', 'D_palette_slots'):
            for key in ('global_payload_bytes', 'cell_payload_bytes', 'unique_tables'):
                options[o][key] = sum(r['costs'][o][key] for r in rows)
    tie_instances = sum(p['instances'] for p in prototypes if p['kind'] == 'tie')
    matched = sum(r['matched_corners'] for r in rows)
    expected = sum(r['expected_corners'] for r in rows)
    bsp_corners = sum(3 * p['lods'][0]['tris'] * p['instances'] for p in prototypes if p['kind'] == 'tie')
    dump_files = {str(lod): len(list(Path(dump_dir).glob(f'lod{lod}-tie-{dgo.name}-*/*.obj')))
                  for lod in range(4)} if dump_dir else None
    return {'level': level, 'dgo': str(dgo), 'glb': str(glb), 'dump_obj_files_including_ALL': dump_files,
            'tie_placements': tie_instances, 'measured_placements': measured,
            'complete_placements': sum(r['complete_placements'] for r in rows),
            'placements_with_matches': sum(r['placements_with_matches'] for r in rows),
            'bsp_expected_corners': bsp_corners, 'selected_expected_corners': expected,
            'matched_corners': matched, 'unmatched_selected_corners': expected - matched,
            'matched_fraction_of_bsp_corners': matched / bsp_corners if bsp_corners else None,
            'expanded_lighting_vertices_lod0': sum(r['lighting_vertices'] * r['placements'] for r in rows),
            'colour_scratch_bytes_max_material': max((r['colour_scratch_bytes_max_material'] for r in rows), default=0),
            'options': options, 'skipped_prototypes': skipped, 'prototypes': rows}


def cpu_model():
    return {'status': 'inferred operation counts and illustrative budgets; no SH-4 timing run',
            'assumed_clock_hz': 200000000, 'vertices_in_example': 10000,
            'A_mean': {'added_work': 'none', 'assumed_cycles_per_vertex': 0, 'example_ms': 0.0},
            'B_tint': {'added_work': '3 channel extracts, 3 integer multiplies/MACL reads, '
                       '3 rounded shifts, 3 saturations, RGB repack; cache once with transform',
                       'assumed_cycles_per_vertex': 40, 'example_ms': 2.0},
            'C_argb': {'added_work': 'one table ARGB load replacing prototype ARGB load; '
                       'one table base per placement', 'assumed_cycles_per_vertex': 4, 'example_ms': 0.2},
            'D_palette_slots': {'added_work': 'u16 vertex pad slot, u8/u16 placement index, '
                                'dependent u32 material palette load; no colour arithmetic',
                                'assumed_cycles_per_vertex': 12, 'example_ms': 0.6},
            'limits': 'Not latency measurements or bounds; cache misses, memory bandwidth, '
                      'instruction scheduling and visible vertex count change frame cost. '
                      'Current draw reads ARGB per triangle corner; either pay per corner '
                      'or fill a 4-byte-per-vertex scratch array beside the transform cache.'}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--levels-root', type=Path, required=True)
    ap.add_argument('--dgo-dir', type=Path, required=True)
    ap.add_argument('--dump-dir', type=Path)
    ap.add_argument('--levels', nargs='+', choices=tuple(LEVEL_DGOS), default=list(LEVEL_DGOS))
    ap.add_argument('--json', type=Path, default=Path('build/instance-lighting.json'))
    args = ap.parse_args(argv)
    # Real analysis output is always inside this port's ignored build directory.
    build = Path(__file__).resolve().parent / 'build'
    destination = args.json.resolve()
    if not destination.is_relative_to(build.resolve()):
        ap.error('--json must be under ports/dreamcast/build/')
    if destination.exists():
        ap.error('--json already exists; choose a new build path (existing files are read only)')
    levels = []
    for level in args.levels:
        print(f'{level}: measuring TIE LOD 0 (AI-assisted)', file=sys.stderr, flush=True)
        levels.append(analyse_level(level, args.dgo_dir / (LEVEL_DGOS[level] + '.DGO'),
                                    args.levels_root / level / (level + '-background.glb'), args.dump_dir))
    report = {'schema': 1, 'attribution': '(AI-assisted), ISC.',
              'verified': 'Host analysis of user inputs and encoded candidate table round trips. '
                          'Static COLOR_0, TIE LOD 0, matched corners only; alpha not scored. '
                          'A matches mean-colours before material scaling. Native streams include QW padding. '
                          'Byte costs describe the layout in this module docstring, not a changed renderer. '
                          'Working sets sample occupied TIE cell centres at 30 m, excluding unmeasured '
                          'prototypes and shrubs; they are not continuous bounds. Colour scratch is separate.',
              'inferred': 'SH-4 costs and recommendation; no hardware or emulator executed. '
                          'Observed trajectory sharing is not original VU slot provenance and '
                          'does not establish correctness at other times of day or lower LODs.',
              'recommendation': 'D palette slots for static lighting if the measured cell working set fits; '
                                'stream table payloads with the 56-byte offset records. It preserves '
                                'observed placement gradients with no tint arithmetic. C trades more '
                                'streamed bytes for one direct ARGB load. B costs only 8 bytes per '
                                'placement but cannot restore spatially varying light. Validate D on SH-4 '
                                'before choosing a frame budget.',
              'instance_pass_application': 'Keep DCI3 16-byte vertices; use the existing zero u16 '
                       'pad as lighting slot. Add a u32 lighting-table offset after each 52-byte '
                       'instance_cells record, keeping its original-index pad. Resolve the table '
                       'once per instance; each geometry already selects its material palette. '
                       'At the transform loop fill scratch_argb[k] from '
                       'palette[table[cvertex.pad]], then use scratch_argb[ix[j]] where the '
                       'triangle loop currently reads s->argb. Keep donor alpha per vertex, '
                       'OR it into RGB; clip/submission stays unchanged. C indexes by vertex '
                       'ordinal instead; B multiplies donor-mean RGB once in that transform loop. '
                       'This requires a versioned loader/cell format and validation; none is implemented here.',
              'cpu_model': cpu_model(), 'levels': levels}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(report, f, indent=1, allow_nan=False)
        f.write('\n')
    print(json.dumps({'json': str(destination), 'levels': [
        {k: r[k] for k in ('level', 'matched_corners', 'measured_placements', 'options')} for r in levels]}, indent=1))


if __name__ == '__main__':
    main()

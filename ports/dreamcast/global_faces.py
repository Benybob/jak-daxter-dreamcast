"""Explain global faces and measure hypothetical XZ midpoint splits.

    python global_faces.py LEVEL-background.glb --cell 16 [--split 8] [--json OUT]

The active scene is walked exactly as prepare_render_sectors.extract_scene walks
it: no tfrag, LOD, material or instance filtering is implicit. Dimensions are
world metres after the packer's float32 conversion. Histograms count closed XZ
triangle/cell overlaps, including faces rejected first by the 64 m XYZ span rule.
The candidate-axis guard is reported as unknown, never as zero touched cells.
Size bins use the largest XYZ extent. Material IDs belong to the packer's
deduplicated catalog; each material row includes that catalog record.
For exported backgrounds, --roots normal,lowres explicitly selects the terrain
roots used by tfrag_glb.py before extraction. Unselected roots remain untouched.

--split recursively bisects the longest projected XZ edge of faces touching more
than N cells (also splitting unknown counts). Position and UV are float32
midpoints; packed ARGB channels use round-to-even midpoints; normals are averaged
and normalized. Winding and material survive; each child gets a fresh source ID.
A point on a grid corner touches four closed cells, so small N may be
unattainable. Stalls at float32 precision or depth 32 are counted; exceeding the
source cap aborts analysis. This models geometry only; cracks at adjacent
unsplit edges remain possible.

DRX2 bytes include every local block and the global block, with header and CRC,
and exclude manifest/textures. Oversized blocks still have an exact projected
byte count, but cannot be encoded when their vertex count exceeds 65535. DRM2
package bytes use the packer's current zlib level and include manifest/textures.
No cap is overridden. Passing candidates are linked and parsed with the original
packer. WINDOW_UNIQUE_CAP is a query-time limit, not a build_package check; no
unspecified draw window or native memory fit is certified here. Nothing writes
geometry or packages. JSON output must be a new file below this tool's build/.
(AI-assisted), ISC.
"""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import zlib

import indexed_sectors as indexed
import prepare_render_sectors as drm
import tfrag_glb


SIZE_BINS = (1, 4, 8, 16, 32, 64, 128, 256, 512, 1024)
MAX_SPLIT_DEPTH = 32


def overlapping_cells(xs, zs, cell):
    """Packer SAT with triangle projections prepared once, rather than per cell.

    Rectangle extrema select the same corners as min/max of its four projected
    corners. Products, sums, padding, slack, guard and closed boundaries retain
    the packer's arithmetic; the synthetic tests compare complete ordered lists.
    """
    xlo = drm._i32(math.ceil((min(xs) - drm.RANGE_PAD_M) / cell) - 1)
    xhi = drm._i32(math.floor((max(xs) + drm.RANGE_PAD_M) / cell))
    zlo = drm._i32(math.ceil((min(zs) - drm.RANGE_PAD_M) / cell) - 1)
    zhi = drm._i32(math.floor((max(zs) + drm.RANGE_PAD_M) / cell))
    if xhi < xlo or zhi < zlo:
        return []
    if xhi - xlo > drm.CANDIDATE_AXIS_GUARD or zhi - zlo > drm.CANDIDATE_AXIS_GUARD:
        return None
    axes = []
    for i in range(3):
        ex, ez = xs[(i + 1) % 3] - xs[i], zs[(i + 1) % 3] - zs[i]
        length = math.hypot(-ez, ex)
        if ex * ex + ez * ez <= 1e-24 or length <= 1e-12:
            continue
        ax, az = -ez / length, ex / length
        projected = [x * ax + z * az for x, z in zip(xs, zs)]
        axes.append((ax, az, min(projected), max(projected)))
    xmin, xmax, zmin, zmax = min(xs), max(xs), min(zs), max(zs)
    slack = drm.SEPARATION_SLACK_M
    hits = []
    for ix in range(xlo, xhi + 1):
        for iz in range(zlo, zhi + 1):
            x0, x1, z0, z1 = drm.cell_bounds(ix, iz, cell)
            if xmax < x0 - slack or xmin > x1 + slack or zmax < z0 - slack or zmin > z1 + slack:
                continue
            for ax, az, low, high in axes:
                rect_low = (x1 if ax < 0 else x0) * ax + (z1 if az < 0 else z0) * az
                rect_high = (x0 if ax < 0 else x1) * ax + (z0 if az < 0 else z1) * az
                if high < rect_low - slack or rect_high < low - slack:
                    break
            else:
                hits.append((ix, iz))
    return hits


def inspect_face(vertices, cell):
    """Return all causes, plus the first cause used by classify_triangle()."""
    axes = tuple(tuple(v[i] for v in vertices) for i in range(3))
    spans = tuple(max(a) - min(a) for a in axes)
    cells = overlapping_cells(axes[0], axes[2], cell)
    reasons = []
    if max(spans) > drm.f32(drm.MAX_AXIS_SPAN_M):
        reasons.append('axis_span')
    if cells is None:
        reasons.append('candidate_axis_guard')
    elif not cells:
        reasons.append('no_overlapping_cells')
    elif len(cells) > drm.MAX_REPLICAS_PER_TRIANGLE:
        reasons.append('replica_limit')
    edges = [math.hypot(vertices[i][0] - vertices[(i + 1) % 3][0],
                        vertices[i][2] - vertices[(i + 1) % 3][2]) for i in range(3)]
    return dict(cells=cells, reasons=reasons, spans=spans, max_xz_edge=max(edges))


def midpoint(a, b):
    values = tuple(drm.f32((a[i] + b[i]) / 2) for i in range(5))
    normal = drm._pack_unit_normal(*(a[i] + b[i] for i in range(5, 8)))
    color = sum(round(((a[8] >> shift) & 255) / 2 + ((b[8] >> shift) & 255) / 2)
                << shift for shift in (0, 8, 16, 24))
    return values + normal + (color,)


def bisect(vertices):
    """Two wound children, splitting the longest edge in world XZ."""
    edge = max(range(3), key=lambda i: sum(
        (vertices[i][k] - vertices[(i + 1) % 3][k]) ** 2 for k in (0, 2)))
    a, b, c = (vertices[(edge + i) % 3] for i in range(3))
    m = midpoint(a, b)
    if m[:3] in (a[:3], b[:3]):
        return None
    return (a, m, c), (m, b, c)


def _threshold(value):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError('split threshold must be a positive integer')
    return value


def subdivide(faces, cell, threshold):
    """Yield material, child corners, diagnostics, and whether splitting stalled.

    Refuse more than MAX_SOURCE children rather than silently truncate analysis.
    Depth/float32 stalls remain whole and are explicitly counted in the report.
    """
    _threshold(threshold)
    splits = 0
    for material, vertices in faces:
        stack = [(vertices, 0)]
        while stack:
            vertices, depth = stack.pop()
            info = inspect_face(vertices, cell)
            needs = info['cells'] is None or len(info['cells']) > threshold
            children = bisect(vertices) if needs and depth < MAX_SPLIT_DEPTH else None
            if children is not None:
                splits += 1
                if len(faces) + splits > drm.MAX_SOURCE:
                    raise ValueError('hypothetical split exceeds source cap; analysis aborted')
                stack.extend((child, depth + 1) for child in reversed(children))
            else:
                yield material, vertices, info, needs


def _empty_stats():
    return dict(faces=0, global_faces=0, cells_touched_histogram=Counter(),
                global_cells_touched_histogram=Counter(), primary_global_reasons=Counter(),
                global_reason_flags=Counter(), max_axis_span_m_histogram=Counter(),
                global_max_axis_span_m_histogram=Counter(), max_span_xyz_m=[0, 0, 0],
                global_axis_span_flags=Counter(), max_xz_edge_m=0, split_stalled_faces=0)


def _record(stats, info, stalled):
    stats['faces'] += 1
    count = 'unknown_guard' if info['cells'] is None else str(len(info['cells']))
    stats['cells_touched_histogram'][count] += 1
    span = max(info['spans'])
    size = next((f'<= {bound}' for bound in SIZE_BINS if span <= bound), '> 1024')
    stats['max_axis_span_m_histogram'][size] += 1
    stats['max_span_xyz_m'] = [max(a, b) for a, b in zip(stats['max_span_xyz_m'], info['spans'])]
    stats['max_xz_edge_m'] = max(stats['max_xz_edge_m'], info['max_xz_edge'])
    stats['split_stalled_faces'] += int(stalled)
    if info['reasons']:
        stats['global_faces'] += 1
        stats['global_cells_touched_histogram'][count] += 1
        stats['global_max_axis_span_m_histogram'][size] += 1
        stats['primary_global_reasons'][info['reasons'][0]] += 1
        stats['global_reason_flags'].update(info['reasons'])
        stats['global_axis_span_flags'].update(
            axis for axis, span in zip('xyz', info['spans']) if span > drm.f32(drm.MAX_AXIS_SPAN_M))


def _parts(state, faces, cell, digest):
    low, high = drm._scene_bounds(faces)
    result = {key: state[key] for key in (
        'textures', 'materials', 'accessor_decodes', 'position_decodes',
        'position_transforms', 'normal_decodes', 'normal_transforms',
        'authored_normal_faces', 'flat_normal_faces')}
    result.update(source_count=len(faces), cell_size=cell, texture_size=drm.DEFAULT_TEXTURE_SIZE,
                  source_sha256=digest, bounds_min=low, bounds_max=high)
    return result


def measure(state, cell, split=None, digest=bytes(32)):
    """Unbounded diagnostic partition, then validate against unchanged build caps."""
    cell = drm.quantize_cell_size(cell)
    if split is not None:
        _threshold(split)
    original = state['faces']
    stream = (subdivide(original, cell, split) if split is not None else
              ((m, v, inspect_face(v, cell), False) for m, v in original))
    stats, materials, buckets, globals_, output = _empty_stats(), {}, {}, [], []
    for source_id, (material, vertices, info, stalled) in enumerate(stream):
        _record(stats, info, stalled)
        _record(materials.setdefault(material, _empty_stats()), info, stalled)
        output.append((material, vertices))
        raw = drm.pack_face(source_id, material, vertices)
        if info['reasons']:
            globals_.append(raw)
        else:
            for key in info['cells']:
                buckets.setdefault(key, []).append(raw)

    # Exact byte accounting remains useful even for partitions the packer refuses.
    # DRX2 sharing is bytewise within each block, including UV/colour seams.
    drx_bytes = drs_bytes = replicas = max_vertices = max_raw = max_compressed = 0
    compression_slack_ok = True
    for records in [globals_, *buckets.values()]:
        vertices = {raw[at:at + 20] + raw[at + 32:at + 36]
                    for raw in records for at in (8, 44, 80)}
        drx_bytes += indexed.HEADER.size + 4 + len(vertices) * indexed.DRAW_VERTEX.size + len(records) * indexed.FACE.size
        max_vertices = max(max_vertices, len(vertices))
        payload = b''.join(records)
        compressed = zlib.compress(payload, drm.ZLIB_LEVEL)
        drs_bytes += drm.SECTOR_HEADER_STRUCT.size + 8 + len(compressed)
        max_raw = max(max_raw, len(payload))
        max_compressed = max(max_compressed, len(compressed))
        compression_slack_ok &= len(compressed) <= len(payload) + drm.COMPRESS_SLACK
    replicas = sum(map(len, buckets.values()))
    tables, _, texture_bytes = drm._build_tables(state['textures'], state['materials'], [])
    package_bytes = drm.HEADER_SIZE + len(tables) + 4 + len(buckets) * drm.ENTRY_STRUCT.size + drs_bytes
    checks = {}

    def check(name, observed, cap):
        checks[name] = dict(observed=observed, cap=cap, passes=observed <= cap)

    check('source', len(output), drm.MAX_SOURCE)
    check('global', len(globals_), drm.GLOBAL_CAP)
    check('per_sector', max(map(len, buckets.values()), default=0), drm.PER_SECTOR_CAP)
    check('sectors', len(buckets), drm.MAX_SECTORS)
    check('replicas', replicas, drm.MAX_TOTAL_REPLICAS)
    check('materials', len(state['materials']), drm.MAX_MATERIALS)
    check('textures', len(state['textures']), drm.MAX_TEXTURES)
    check('texture_bytes', texture_bytes, drm.MAX_TEXTURE_BYTES)
    check('package_bytes', package_bytes, drm.MAX_PACKAGE_BYTES)
    check('raw_sector_bytes', max_raw, drm.RAW_SECTOR_LIMIT)
    check('compressed_sector_bytes', max_compressed, drm.COMPRESSED_INPUT_LIMIT)
    check('drx2_vertices_per_block', max_vertices, indexed.MAX_VERTICES)
    passes = all(c['passes'] for c in checks.values()) and compression_slack_ok
    validated = False
    if passes:
        # Same partition -> encode -> link sequence as build_package. No mocked
        # globals or changed caps: its parser rechecks the manifest contract.
        sectors = {key: drm.encode_sector(records, *key) for key, records in buckets.items()}
        package = drm.link_package(_parts(state, output, cell, digest), sectors,
                                   drm.encode_sector(globals_, is_global=True))
        if package.summary['package_bytes'] != package_bytes:
            raise ValueError('DRM2 byte accounting mismatch')
        # Prove DRX2 accounting too, once for every passing candidate.
        actual = sum(len(indexed.encode_draw_sector(r)) for r in [globals_, *buckets.values()])
        if actual != drx_bytes:
            raise ValueError('DRX2 byte accounting mismatch')
        validated = True
    return dict(stats, cell_m=cell, split_threshold=split, added_faces=len(output) - len(original),
                by_material=[dict(material_id=m, catalog=state['materials'][m], **s)
                             for m, s in sorted(materials.items())],
                local_sectors=len(buckets), local_replicas=replicas, drx2_bytes=drx_bytes,
                drm2_package_bytes=package_bytes, zlib_level=drm.ZLIB_LEVEL,
                cap_checks=checks, compression_slack_passes=compression_slack_ok,
                package_caps_pass=passes, packer_link_validated=validated,
                window_unique_cap_checked=False)


def load_scene(path, roots=None):
    blob = Path(path).read_bytes()
    selected = tfrag_glb.cut(blob, roots=roots)[0] if roots is not None else blob
    doc, binary = drm.parse_glb_bytes(selected)
    state = drm.extract_scene(doc, binary, drm.DEFAULT_TEXTURE_SIZE)
    # Extraction caches retain large accessor arrays; measurements only need these.
    state = {key: value for key, value in state.items() if key in (
        'faces', 'textures', 'materials', 'accessor_decodes', 'position_decodes',
        'position_transforms', 'normal_decodes', 'normal_transforms',
        'authored_normal_faces', 'flat_normal_faces')}
    roots = [doc['nodes'][i].get('name', str(i))
             for i in doc['scenes'][doc.get('scene', 0)].get('nodes', [])]
    return state, hashlib.sha256(blob).digest(), roots


def analyze(path, cell, split=None, roots=None):
    state, digest, selected_roots = load_scene(path, roots)
    baseline = measure(state, cell, digest=digest)
    result = dict(input=str(path), source_sha256=digest.hex(), active_scene_roots=selected_roots,
                  scope=('complete active scene' if roots is None else 'explicit tfrag roots'),
                  root_filter=roots,
                  contract=drm.contract_constants(), baseline=baseline)
    if split is not None:
        candidate = measure(state, cell, split, digest)
        candidate['drx2_bytes_delta'] = candidate['drx2_bytes'] - baseline['drx2_bytes']
        candidate['drm2_package_bytes_delta'] = candidate['drm2_package_bytes'] - baseline['drm2_package_bytes']
        result['split'] = candidate
    return result


def write_report(path, report):
    """Create only a new JSON artifact under the git-ignored build directory."""
    path = Path(path).resolve()
    build = (Path(__file__).resolve().parent / 'build').resolve()
    if not path.is_relative_to(build) or path == build:
        raise ValueError('JSON output must be below ports/dreamcast/build/')
    with path.open('x', encoding='utf-8') as handle:
        handle.write(json.dumps(report, indent=2, sort_keys=True) + '\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('glb', type=Path)
    parser.add_argument('--cell', type=float, required=True)
    parser.add_argument('--split', type=int)
    parser.add_argument('--roots', help='explicit tfrag suffixes, e.g. normal,lowres; default: complete active scene')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args(argv)
    try:
        drm.quantize_cell_size(args.cell)
        if args.split is not None:
            _threshold(args.split)
        roots = tuple(args.roots.split(',')) if args.roots is not None else None
        result = analyze(args.glb, args.cell, args.split, roots)
        if args.json:
            write_report(args.json, result)
        print(json.dumps(result, indent=2, sort_keys=True))
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

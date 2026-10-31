"""Bisect wide background triangles, preserving shared edges and GLB structure.

    python wide_faces.py GLB OUT.glb [--limit 64]

Only world-XZ spans exceeding limit request a new edge midpoint. Neighbours
receive the same edge cuts for conformity, including across attribute/material
seams; this necessary stitching is counted separately. Narrow multi-cell faces
do not request cuts. Y-only and replica-limit globals are measured, not split.
UV and colour interpolation is linear before material baking; normal/tangent
vectors are interpolated linearly, then normalized only when written. UVs are
never wrapped, clamped or rescaled. Convex interpolation cannot enlarge their
range; an input exceeding the packer's UV bound is reported as an error.

Topology is matched by exact local positions and the complete set of active
instance transforms. Differently transformed meshes with coincident world edges
are refused rather than silently claiming conformity. Pre-existing partial-edge
T-junctions are outside the matching-full-edge proof. Nodes, scene roots,
materials, embedded images and inactive meshes are preserved. Static triangle
primitives only; no morphs, skinning or custom attributes.

The sidecar measures every build cap at 16 m, attempts unchanged build_package,
and samples window_caps at half 14 and 10, step 8, even if packaging is refused.
Those samples are not a bound between grid points or a hardware measurement.
Only new files under build/ may be written. (AI-assisted), ISC.
"""

import argparse
from collections import Counter
import copy
import hashlib
import json
import math
from pathlib import Path
import struct

import global_faces as gf
import indexed_sectors as indexed
import prepare_render_sectors as drm
import tfrag_glb
import window_caps


ATTRIBUTES = {'POSITION', 'TEXCOORD_0', 'COLOR_0', 'NORMAL', 'TANGENT'}
MAX_PASSES = 64


def encode_glb(doc, binary, extra=()):
    text = json.dumps(doc, separators=(',', ':'), allow_nan=False).encode()
    chunks = [(b'JSON', text + b' ' * (-len(text) % 4)),
              (b'BIN\0', bytes(binary) + b'\0' * (-len(binary) % 4)), *extra]
    body = b''.join(struct.pack('<I4s', len(data), kind) + data for kind, data in chunks)
    return struct.pack('<4sII', b'glTF', 2, len(body) + 12) + body


def mesh_transforms(doc):
    """All active instances, retaining their multiplicity for source-cap checks."""
    result = {}

    def walk(index, parent, ancestors):
        if index in ancestors:
            raise ValueError('cycle in node graph')
        node = doc['nodes'][index]
        if 'skin' in node:
            raise ValueError('skinned geometry is unsupported')
        matrix = tuple(drm.multiply(parent, drm.node_matrix(node)))
        if 'mesh' in node:
            result.setdefault(node['mesh'], []).append(matrix)
        for child in node.get('children', []):
            walk(child, matrix, ancestors | {index})

    for root in doc['scenes'][doc.get('scene', 0)].get('nodes', []):
        walk(root, drm.IDENTITY, set())
    return result


def edge(a, b):
    return (a, b) if a <= b else (b, a)


def unit(values):
    length = math.sqrt(sum(v * v for v in values))
    if length <= 1e-30:
        raise ValueError('interpolated normal/tangent has zero length')
    return tuple(drm.f32(v / length) for v in values)


class Subdivision:
    """Indexed local attributes plus a geometric midpoint tree shared by seams."""

    def __init__(self, doc, binary, limit):
        if isinstance(limit, bool) or not math.isfinite(limit) or limit <= 0:
            raise ValueError('limit must be finite and positive')
        self.limit = limit
        self.doc, self.binary = copy.deepcopy(doc), binary
        self.transforms = mesh_transforms(doc)
        self.points, self.lookup, self.midpoints = [], {}, {}
        self.primitives, self.faces, self.original_edges = [], [], {}
        self.original_wide = set()
        cache = {}

        def decoded(index):
            if index not in cache:
                cache[index] = drm.accessor(doc, binary, index)
            return cache[index]

        # An exact world edge across different local frames requires inverse
        # rounding decisions. Refuse it explicitly; same-frame seams are exact.
        world_edges = {}
        for mesh_id, matrices in self.transforms.items():
            domain = tuple(sorted(set(matrices)))
            mesh = self.doc['meshes'][mesh_id]
            for primitive in mesh['primitives']:
                if primitive.get('mode', 4) != 4 or 'targets' in primitive or primitive.get('extensions'):
                    raise ValueError('only static triangle primitives are supported')
                names = primitive['attributes']
                if set(names) - ATTRIBUTES:
                    raise ValueError('unsupported vertex attribute')
                attrs = {name: list(decoded(index)) for name, index in names.items()}
                positions = attrs['POSITION']
                if any(len(p) != 3 for p in positions) or any(len(a) != len(positions) for a in attrs.values()):
                    raise ValueError('invalid attribute count or POSITION width')
                for name, width in (('TEXCOORD_0', 2), ('NORMAL', 3), ('TANGENT', 4)):
                    if name in attrs and any(len(v) != width for v in attrs[name]):
                        raise ValueError(f'invalid {name} width')
                if 'COLOR_0' in attrs and any(len(v) not in (3, 4) for v in attrs['COLOR_0']):
                    raise ValueError('invalid COLOR_0 width')
                ids = []
                for position in positions:
                    key = (domain, tuple(position))
                    if key not in self.lookup:
                        self.lookup[key] = len(self.points)
                        self.points.append(tuple(position))
                    ids.append(self.lookup[key])
                p = dict(primitive=primitive, attrs=attrs, ids=ids, domain=domain,
                         multiplicity=len(matrices), vertex_midpoints={}, worlds={})
                pi = len(self.primitives)
                self.primitives.append(p)
                indices = ([v[0] for v in decoded(primitive['indices'])] if 'indices' in primitive
                           else list(range(len(positions))))
                if len(indices) % 3 or any(type(i) is not int or not 0 <= i < len(positions) for i in indices):
                    raise ValueError('invalid triangle indices')
                for at in range(0, len(indices), 3):
                    corners = tuple(indices[at:at + 3])
                    origin = len(self.faces)
                    self.faces.append((pi, corners, origin))
                    if self.wide(p, corners):
                        self.original_wide.add(origin)
                    gids = [ids[i] for i in corners]
                    for i in range(3):
                        e = edge(gids[i], gids[(i + 1) % 3])
                        self.original_edges.setdefault(e, []).append(origin)
                    for matrix in domain:
                        world = [self.world(p, i, matrix) for i in corners]
                        for i in range(3):
                            e = edge(world[i], world[(i + 1) % 3])
                            old = world_edges.setdefault(e, domain)
                            if old != domain:
                                raise ValueError('coincident world edge across different transform domains')
        self.original_count = len(self.faces)
        self.original_instance_faces = sum(self.primitives[pi]['multiplicity'] for pi, _, _ in self.faces)
        self.max_uv = max((abs(v) for p in self.primitives
                           for uv in p['attrs'].get('TEXCOORD_0', []) for v in uv), default=0)
        if self.max_uv > drm.UV_ABS_MAX:
            raise ValueError(f'UV absolute maximum {self.max_uv} exceeds {drm.UV_ABS_MAX} '
                             f'by {self.max_uv - drm.UV_ABS_MAX}; subdivision cannot fix endpoint UVs')

    def world(self, p, index, matrix):
        key = (matrix, index)
        if key not in p['worlds']:
            p['worlds'][key] = tuple(drm.f32(v) for v in drm.transform(matrix, p['attrs']['POSITION'][index]))
        return p['worlds'][key]

    def wide(self, p, corners):
        return any(max(values) - min(values) > self.limit for matrix in p['domain']
                   for axis in (0, 2)
                   for values in [[self.world(p, i, matrix)[axis] for i in corners]])

    def split_vertex(self, p, a, b):
        local = edge(a, b)
        if local in p['vertex_midpoints']:
            return p['vertex_midpoints'][local]
        ga, gb = p['ids'][a], p['ids'][b]
        geometric = edge(ga, gb)
        position = tuple(drm.f32((x + y) / 2) for x, y in zip(self.points[ga], self.points[gb]))
        if position in (self.points[ga], self.points[gb]):
            raise ValueError('midpoint stalled at float32 precision')
        if geometric not in self.midpoints:
            key = (p['domain'], position)
            if key not in self.lookup:
                self.lookup[key] = len(self.points)
                self.points.append(position)
            self.midpoints[geometric] = self.lookup[key]
        gid = self.midpoints[geometric]
        index = len(p['ids'])
        for name, values in p['attrs'].items():
            if name == 'POSITION':
                value = self.points[gid]
            else:
                value = tuple((x + y) / 2 for x, y in zip(values[a], values[b]))
                if name == 'TANGENT' and values[a][3] != values[b][3]:
                    raise ValueError('split edge crosses tangent handedness')
            values.append(value)
        p['ids'].append(gid)
        p['vertex_midpoints'][local] = index
        return index

    def run(self):
        # Finish width refinement before stitching. Interleaving forced short
        # edge cuts with longest-edge refinement creates thin children whose
        # long diagonals demand unnecessary extra cuts in adjacent triangles.
        width_faces = None
        for passes in range(1, MAX_PASSES + 1):
            changed = False
            output = []
            stack = list(reversed(self.faces))
            instance_count = sum(self.primitives[pi]['multiplicity'] for pi, _, _ in stack)
            while stack:
                pi, corners, origin = stack.pop()
                p = self.primitives[pi]
                gids = [p['ids'][i] for i in corners]
                requested = ([i for i in range(3) if edge(gids[i], gids[(i + 1) % 3]) in self.midpoints]
                             if width_faces is not None else [])
                if requested:
                    cut = requested[0]
                elif width_faces is None and self.wide(p, corners):
                    cut = max(range(3), key=lambda i: max(
                        sum((self.world(p, corners[i], m)[k] -
                             self.world(p, corners[(i + 1) % 3], m)[k]) ** 2 for k in (0, 2))
                        for m in p['domain']))
                else:
                    output.append((pi, corners, origin))
                    continue
                a, b, c = (corners[(cut + i) % 3] for i in range(3))
                mid = self.split_vertex(p, a, b)
                instance_count += p['multiplicity']
                if instance_count > drm.MAX_SOURCE:
                    raise ValueError(f'subdivision exceeds source cap {drm.MAX_SOURCE}')
                stack.extend(((pi, (mid, b, c), origin), (pi, (a, mid, c), origin)))
                changed = True
            self.faces = output
            if width_faces is None:
                width_faces = len(output)
                continue
            if not changed:
                report = self.proof(passes)
                report.update(width_faces_added=width_faces - self.original_count,
                              conformity_faces_added=len(output) - width_faces)
                return report
        raise ValueError(f'conformity did not converge in {MAX_PASSES} passes')

    def proof(self, passes):
        """Independently check final leaf edges against every shared source edge."""
        expected = {}

        def leaves(e):
            if e not in self.midpoints:
                return [e]
            a, b = e
            mid = self.midpoints[e]
            return leaves(edge(a, mid)) + leaves(edge(mid, b))

        shared = cuts = checks = 0
        for e, origins in self.original_edges.items():
            if len(set(origins)) < 2:
                continue
            shared += 1
            if e not in self.midpoints:
                continue
            cuts += 1
            for segment in leaves(e):
                expected.setdefault(segment, set()).update(origins)
        counts = Counter()
        for pi, corners, origin in self.faces:
            p = self.primitives[pi]
            if self.wide(p, corners):
                raise ValueError('conformity rounding produced an over-limit world XZ span')
            counts[origin] += 1
            gids = [p['ids'][i] for i in corners]
            for i in range(3):
                e = edge(gids[i], gids[(i + 1) % 3])
                if e in self.midpoints:
                    raise ValueError('unresolved split edge in output')
                if e in expected and origin in expected[e]:
                    expected[e].remove(origin)
                    checks += 1
        missing = sum(map(len, expected.values()))
        if missing:
            raise ValueError(f'{missing} shared boundary segments missing')
        # Check each material/UV seam's independently interpolated positions,
        # plus every attribute whose endpoints agree, after float32 encoding.
        samples = {}
        attr_checks = 0
        for p in self.primitives:
            for (a, b), mid in p['vertex_midpoints'].items():
                ga, gb = p['ids'][a], p['ids'][b]
                if p['attrs']['POSITION'][mid] != self.points[self.midpoints[edge(ga, gb)]]:
                    raise ValueError('inconsistent midpoint position')
                for name, values in p['attrs'].items():
                    va, vb = (values[a], values[b]) if ga <= gb else (values[b], values[a])
                    key = (edge(ga, gb), name, va, vb)
                    value = self.stored(name, values[mid])
                    packed = struct.pack('<' + 'f' * len(value), *value)
                    if samples.setdefault(key, packed) != packed:
                        raise ValueError('inconsistent shared-edge attribute')
                    attr_checks += 1
        return dict(passes=passes, original_faces=self.original_count,
                    original_instance_faces=self.original_instance_faces,
                    original_wide_faces=len(self.original_wide), output_faces=len(self.faces),
                    faces_added=len(self.faces) - self.original_count,
                    narrow_faces_stitched=sum(n > 1 and o not in self.original_wide for o, n in counts.items()),
                    narrow_stitch_faces_added=sum(n - 1 for o, n in counts.items() if o not in self.original_wide),
                    shared_original_edges=shared, split_shared_original_edges=cuts,
                    shared_boundary_segments_checked=checks, missing_shared_segments=missing,
                    unresolved_split_edges=0, attribute_midpoints_checked=attr_checks,
                    max_input_uv_abs=self.max_uv, uv_bound=drm.UV_ABS_MAX,
                    proof_scope='matching full edges in identical active transform domains')

    @staticmethod
    def stored(name, value):
        if name == 'NORMAL':
            return unit(value)
        if name == 'TANGENT':
            return unit(value[:3]) + (value[3],)
        return tuple(drm.f32(v) for v in value)

    def serialize(self, extra=()):
        doc = self.doc
        binary = bytearray(self.binary)

        def append(values, kind, component=5126):
            width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[kind]
            layout = struct.Struct('<' + ('f' if component == 5126 else 'I') * width)
            binary.extend(b'\0' * (-len(binary) % 4))
            start = len(binary)
            for value in values:
                binary.extend(layout.pack(*value))
            doc['bufferViews'].append(dict(buffer=0, byteOffset=start, byteLength=len(binary) - start))
            acc = dict(bufferView=len(doc['bufferViews']) - 1, componentType=component,
                       count=len(values), type=kind)
            if kind == 'VEC3' and component == 5126:
                acc.update(min=[min(v[i] for v in values) for i in range(3)],
                           max=[max(v[i] for v in values) for i in range(3)])
            doc['accessors'].append(acc)
            return len(doc['accessors']) - 1

        by_primitive = [[] for _ in self.primitives]
        for pi, corners, _ in self.faces:
            by_primitive[pi].extend((i,) for i in corners)
        for pi, p in enumerate(self.primitives):
            if not p['vertex_midpoints']:
                continue
            attrs = {}
            for name, values in p['attrs'].items():
                stored = [self.stored(name, v) for v in values]
                attrs[name] = append(stored, f'VEC{len(stored[0])}')
            p['primitive']['attributes'] = attrs
            p['primitive']['indices'] = append(by_primitive[pi], 'SCALAR', 5125)
        doc['buffers'][0]['byteLength'] = len(binary)
        return encode_glb(doc, binary, extra)


def split_glb(blob, limit=64):
    doc, binary = drm.parse_glb_bytes(blob)
    subdivision = Subdivision(doc, binary, limit)
    report = subdivision.run()
    chunks = tfrag_glb.read_chunks(blob)
    output = subdivision.serialize(chunks[2:])
    cooked, cooked_binary = drm.parse_glb_bytes(output)
    for name in ('nodes', 'scenes', 'scene', 'materials', 'images', 'textures', 'samplers'):
        if cooked.get(name) != doc.get(name):
            raise ValueError(f'{name} changed during subdivision')
    # Re-read serialized midpoint attributes; the proof must cover written bytes.
    for p in subdivision.primitives:
        if not p['vertex_midpoints']:
            continue
        for name, index in p['primitive']['attributes'].items():
            actual = drm.accessor(cooked, cooked_binary, index)
            for mid in p['vertex_midpoints'].values():
                expected = subdivision.stored(name, p['attrs'][name][mid])
                if actual[mid] != expected:
                    raise ValueError('serialized midpoint mismatch')
    report.update(input_bytes=len(blob), output_bytes=len(output), limit_m=limit,
                  output_sha256=hashlib.sha256(output).hexdigest(), serialized_midpoints_verified=True)
    return output, report


class DiagnosticPackage(window_caps.LoadedPackage):
    """Index global face bounds without changing block residency or vertices.

    Global source IDs never occur in locals. Omitting globals outside the query
    therefore cannot change first-replica ownership in window_needs(). Its
    vertex indices, full vertex_count and byte budget remain those of the actual
    encoded block. Local selection still uses LoadedPackage.touched unchanged.
    """

    def index_globals(self):
        self.global_cells, self.wide_globals = {}, []
        for face in self.global_block['faces']:
            _, _, _, _, x0, x1, z0, z1 = face
            ix0, ix1, iz0, iz1 = (math.floor(v / 64) for v in (x0, x1, z0, z1))
            if (ix1 - ix0 + 1) * (iz1 - iz0 + 1) > 64:
                self.wide_globals.append(face)
                continue
            for ix in range(ix0, ix1 + 1):
                for iz in range(iz0, iz1 + 1):
                    self.global_cells.setdefault((ix, iz), []).append(face)

    def touched(self, x0, x1, z0, z1):
        candidates = {f[0]: f for f in self.wide_globals}
        ix0, ix1, iz0, iz1 = (math.floor(v / 64) for v in (x0, x1, z0, z1))
        for ix in range(ix0, ix1 + 1):
            for iz in range(iz0, iz1 + 1):
                for face in self.global_cells.get((ix, iz), ()):
                    candidates[face[0]] = face
        yield dict(self.global_block, faces=[candidates[i] for i in sorted(candidates)])
        blocks = super().touched(x0, x1, z0, z1)
        next(blocks)  # Full global block already supplied above, at identical cost.
        yield from blocks


def measure(blob, cell=16, halves=(14, 10), step=8):
    """Measure an uncapped partition; always try the real unchanged builder."""
    doc, binary = drm.parse_glb_bytes(blob)
    state = drm.extract_scene(doc, binary, drm.DEFAULT_TEXTURE_SIZE)
    report = gf.measure(state, cell, digest=hashlib.sha256(blob).digest())
    try:
        package = drm.build_package(doc, binary, cell, source_sha256=hashlib.sha256(blob).digest())
        report['build_package'] = dict(passes=True, summary=package.summary)
    except drm.RenderSectorError as exc:
        report['build_package'] = dict(passes=False, error=str(exc))
    report['blocking_caps'] = {name: dict(value, excess=value['observed'] - value['cap'])
                               for name, value in report['cap_checks'].items() if not value['passes']}
    # Use window_caps' actual codec loader and query/sweep code without publishing
    # an invalid manifest or creating additional files. Only its container is built
    # here; source IDs, block order and bounds match the manifest's sorted order.
    loaded = DiagnosticPackage.__new__(DiagnosticPackage)
    low, high = drm._scene_bounds(state['faces'])
    loaded.header = dict(source_count=len(state['faces']), material_count=len(state['materials']),
                         bounds_min_x=low[0], bounds_min_z=low[2], bounds_max_x=high[0], bounds_max_z=high[2])
    loaded.cell, loaded.locals, loaded.cells = cell, [], {}
    buckets, globals_ = {}, []
    for source, (material, vertices) in enumerate(state['faces']):
        axes = tuple(tuple(v[i] for v in vertices) for i in range(3))
        cells = (None if max(max(a) - min(a) for a in axes) > drm.f32(drm.MAX_AXIS_SPAN_M)
                 else gf.overlapping_cells(axes[0], axes[2], cell))
        raw = drm.pack_face(source, material, vertices)
        if not cells or len(cells) > drm.MAX_REPLICAS_PER_TRIANGLE:
            globals_.append(raw)
        else:
            for key in cells:
                buckets.setdefault(key, []).append(raw)

    class Block:
        def __init__(self, data):
            self.data = data

        def read_bytes(self):
            return self.data

    total_bytes = 0
    for key, records in [(None, globals_), *sorted(buckets.items())]:
        data = indexed.encode_draw_sector(records)
        block = window_caps._load_block(Block(data), len(records), loaded.header)
        total_bytes += len(data)
        if key is None:
            loaded.global_block = block
        else:
            block['bounds'] = drm.cell_bounds(*key, cell)
            loaded.cells[key] = len(loaded.locals)
            loaded.locals.append(block)
    if total_bytes != report['drx2_bytes']:
        raise ValueError('DRX2 byte accounting mismatch')
    report['drx2_encoded_and_validated'] = True
    loaded.index_globals()
    report['window_caps'] = [window_caps.sweep(loaded, half, step) for half in halves]
    report['window_measurement_scope'] = 'actual DRX2 blocks; uncapped diagnostic partition if build refused'
    return report


def output_paths(output):
    output = Path(output).resolve()
    build = (Path(__file__).resolve().parent / 'build').resolve()
    if not output.is_relative_to(build) or output.suffix.lower() != '.glb':
        raise ValueError('output must be a new .glb below ports/dreamcast/build/')
    sidecar = output.with_suffix('.json')
    if output.exists() or sidecar.exists():
        raise ValueError('output GLB and JSON must both be new files')
    return output, sidecar


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('glb', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('--limit', type=float, default=64)
    args = parser.parse_args(argv)
    try:
        output, sidecar = output_paths(args.out)
        blob = args.glb.read_bytes()
        result, proof = split_glb(blob, args.limit)
        print(f'(AI-assisted) Subdivision complete: {proof["faces_added"]} faces added; measuring caps/windows.', flush=True)
        report = dict(input=str(args.glb), input_sha256=hashlib.sha256(blob).hexdigest(),
                      output=str(output), subdivision=proof, verified=measure(result),
                      inferred=[], disclosure='(AI-assisted)')
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('xb') as handle:
            handle.write(result)
        with sidecar.open('x', encoding='utf-8') as handle:
            handle.write(json.dumps(report, indent=2, sort_keys=True) + '\n')
        print(json.dumps(dict(output=str(output), json=str(sidecar), subdivision=proof,
                              verified={k: report['verified'][k] for k in (
                                  'faces', 'global_faces', 'drx2_bytes', 'local_sectors',
                                  'blocking_caps', 'build_package', 'window_caps')}), indent=2))
    except (ValueError, OSError, KeyError, IndexError, struct.error) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

"""Measure fixed draw-window buffers from published DRX2 blocks. (AI-assisted), ISC."""

import argparse
import json
import math
from pathlib import Path

import indexed_sectors as indexed
import prepare_render_sectors as drm


METRICS = ('faces', 'vertices', 'remap', 'budget')


def _load_block(path, expected_count, header):
    blob = path.read_bytes()
    # Validate the codec once, including CRC, indices, padding and finite floats.
    indexed.decode_draw_sector(blob)
    _, vertex_count, face_count = indexed.HEADER.unpack_from(blob)
    if face_count != expected_count:
        raise ValueError(f'{path}: face count does not match manifest')
    face_offset = indexed.HEADER.size + vertex_count * indexed.DRAW_VERTEX.size
    positions = [(v[0], v[2]) for v in indexed.DRAW_VERTEX.iter_unpack(
        blob[indexed.HEADER.size:face_offset])]
    faces = []
    previous = -1
    for source_id, material, i0, i1, i2, _ in indexed.FACE.iter_unpack(blob[face_offset:-4]):
        if (source_id <= previous or source_id >= header['source_count']
                or material >= header['material_count']):
            raise ValueError(f'{path}: face does not match manifest')
        previous = source_id
        corners = (positions[i0], positions[i1], positions[i2])
        xs, zs = zip(*corners)
        # unpacked float32 coordinates are Python doubles, just as in touches().
        faces.append((source_id, i0, i1, i2, min(xs), max(xs), min(zs), max(zs)))
    return dict(vertex_count=vertex_count, bytes=len(blob), faces=faces)


def _read_manifest(blob):
    """Flythrough fixtures pack at zlib level 0, which the manifest contract records."""
    saved = drm.ZLIB_LEVEL
    try:
        return drm.parse_manifest(blob)
    except drm.RenderSectorError:
        drm.ZLIB_LEVEL = 0
        try:
            return drm.parse_manifest(blob)
        finally:
            drm.ZLIB_LEVEL = saved


class LoadedPackage:
    """Cached block indices and face bounds; construct with load_package()."""

    def __init__(self, root):
        self.header = _read_manifest((root / 'manifest.drm').read_bytes())
        self.cell = self.header['cell_size']
        self.global_block = _load_block(root / 'global.drx', self.header['global_count'], self.header)
        self.locals = []
        self.cells = {}
        for entry in self.header['entries']:
            ix, iz = entry['ix'], entry['iz']
            block = _load_block(root / f'sectors/{ix}_{iz}.drx', entry['record_count'], self.header)
            block['bounds'] = drm.cell_bounds(ix, iz, self.cell)
            self.cells[(ix, iz)] = len(self.locals)
            self.locals.append(block)

    def touched(self, x0, x1, z0, z1):
        yield self.global_block
        # A cell on either side of an exact boundary can touch. The extra cell
        # also guards division rounding; the final test uses C's double bounds.
        ix0, ix1 = math.floor(x0 / self.cell) - 1, math.floor(x1 / self.cell) + 1
        iz0, iz1 = math.floor(z0 / self.cell) - 1, math.floor(z1 / self.cell) + 1
        if (ix1 - ix0 + 1) * (iz1 - iz0 + 1) < len(self.locals):
            indices = []
            for ix in range(ix0, ix1 + 1):
                for iz in range(iz0, iz1 + 1):
                    index = self.cells.get((ix, iz))
                    if index is not None:
                        indices.append(index)
            # Manifest order determines which replica owns the shared vertices.
            candidates = (self.locals[i] for i in sorted(indices))
        else:
            candidates = iter(self.locals)
        for block in candidates:
            bx0, bx1, bz0, bz1 = block['bounds']
            if bx0 <= x1 and bx1 >= x0 and bz0 <= z1 and bz1 >= z0:
                yield block


def load_package(package_dir):
    """Parse manifest.drm and each .drx once; no .drs files are needed."""
    return LoadedPackage(Path(package_dir))


def _nonnegative(value, name):
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise ValueError(f'{name} must be finite and nonnegative')
    return value


def window_needs(package_dir_or_loaded, x, z, half):
    """Return faces, vertices, remap entries and resident .drx budget bytes.

    Pass load_package()'s result to reuse parsed data across queries. All tests
    are closed XZ bounds tests. Only the first touched block holding a source ID
    contributes that face's vertices; equal vertices in different blocks stay
    distinct. Remap and budget include every touched block, even empty global.
    """
    half = _nonnegative(half, 'half')
    x, z = float(x), float(z)
    x0, x1, z0, z1 = x - half, x + half, z - half, z + half
    if not all(math.isfinite(v) for v in (x0, x1, z0, z1)):
        raise ValueError('query bounds must be finite')
    package = (package_dir_or_loaded if isinstance(package_dir_or_loaded, LoadedPackage)
               else load_package(package_dir_or_loaded))
    seen = set()
    faces = vertices = remap = budget = 0
    for block in package.touched(x0, x1, z0, z1):
        remap += block['vertex_count']
        budget += block['bytes']
        used = set()
        for source_id, i0, i1, i2, bx0, bx1, bz0, bz1 in block['faces']:
            if source_id in seen:
                continue
            seen.add(source_id)
            if bx0 <= x1 and bx1 >= x0 and bz0 <= z1 and bz1 >= z0:
                faces += 1
                used.update((i0, i1, i2))
        vertices += len(used)
    return dict(faces=faces, vertices=vertices, remap=remap, budget=budget)


def sweep(package_dir_or_loaded, half, step=8):
    """Report independent maxima and rounded caps on a grid over manifest bounds.

    Centres start at (bounds_min_x, bounds_min_z), spaced by step, and stop at
    the maxima (inclusive when on the grid). Ties keep the first centre, in X
    then Z order. These are sampled maxima, not a proof for unsampled centres.
    Suggested bytes include face order and the transformed vertex view buffer.
    """
    half = _nonnegative(half, 'half')
    step = _nonnegative(step, 'step')
    if not step:
        raise ValueError('step must be positive')
    package = (package_dir_or_loaded if isinstance(package_dir_or_loaded, LoadedPackage)
               else load_package(package_dir_or_loaded))
    header = package.header
    nx = math.floor((header['bounds_max_x'] - header['bounds_min_x']) / step) + 1
    nz = math.floor((header['bounds_max_z'] - header['bounds_min_z']) / step) + 1
    maxima = {key: dict(value=-1, centre=None) for key in METRICS}
    for ix in range(nx):
        x = header['bounds_min_x'] + ix * step
        for iz in range(nz):
            z = header['bounds_min_z'] + iz * step
            needs = window_needs(package, x, z, half)
            for key in METRICS:
                if needs[key] > maxima[key]['value']:
                    maxima[key] = dict(value=needs[key], centre=[x, z])
    caps = {cap: ((maxima[metric]['value'] + 63) // 64) * 64
            for cap, metric in (('face_cap', 'faces'), ('vertex_cap', 'vertices'),
                                ('remap_cap', 'remap'))}
    costs = dict(faces=caps['face_cap'] * 14, vertices=caps['vertex_cap'] * 36,
                 remap=caps['remap_cap'] * 2)
    costs['total'] = sum(costs.values())
    return dict(half=half, step=step, centres=nx * nz, maxima=maxima,
                suggested=dict(caps, bytes=costs))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package_dir', type=Path)
    parser.add_argument('--half', type=float, required=True, help='square half-size in metres')
    parser.add_argument('--step', type=float, default=8, help='centre grid spacing in metres')
    parser.add_argument('--json', type=Path, help='also save the JSON report')
    args = parser.parse_args(argv)
    try:
        report = json.dumps(sweep(args.package_dir, args.half, args.step),
                            indent=2, sort_keys=True) + '\n'
        if args.json:
            args.json.write_text(report, encoding='utf-8')
        print(report, end='')
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

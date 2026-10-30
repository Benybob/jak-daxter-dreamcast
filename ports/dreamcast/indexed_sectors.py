"""Lossless DRX1, draw-only DRX2 sectors and block-cache measurements. (AI-assisted), ISC.

DRX1 keeps all 36 vertex bytes. Flat per-face normals defeat its sharing: village1
stores 2.81 vertices a face. DRX2 stores the 24 bytes the terrain draw reads
(position, UV, ARGB) and decodes with zero normals, so it is byte-identical only
to DRM2 faces with their normals zeroed.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import zlib


HEADER = struct.Struct('<4sII')
VERTEX = struct.Struct('<8fI')
DRAW_VERTEX = struct.Struct('<5fI')
FACE = struct.Struct('<II4H')
FACE_BYTES = 116
MAX_VERTICES = 65535
ZERO_NORMAL = bytes(12)


def _bytes(value, reason):
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise ValueError(reason)
    return bytes(value)


def _finite(vertex):
    if not all(math.isfinite(v) for v in VERTEX.unpack(vertex)[:8]):
        raise ValueError('nonfinite vertex')


def _draw_vertex(vertex):
    return vertex[:20] + vertex[32:]


def encode_sector(records):
    """Encode raw faces (or parse_sector record dictionaries) in original order.

    Sharing uses all 36 vertex bytes, including UV, normal, colour and signed
    zero. No floating-point conversion is used to construct the output.
    """
    return _encode(records, b'DRX1', lambda vertex: vertex)


def encode_draw_sector(records):
    """encode_sector without normals: sharing uses position, UV and ARGB bytes."""
    return _encode(records, b'DRX2', _draw_vertex)


def _encode(records, magic, stored):
    vertices, lookup, faces = bytearray(), {}, bytearray()
    try:
        iterator = iter(records)
    except TypeError as exc:
        raise ValueError('malformed records') from exc
    count = 0
    for record in iterator:
        if isinstance(record, dict):
            record = record.get('raw')
        raw = _bytes(record, 'malformed record')
        if len(raw) != FACE_BYTES:
            raise ValueError('malformed record size')
        indices = []
        for offset in (8, 44, 80):
            vertex = raw[offset:offset + VERTEX.size]
            _finite(vertex)
            kept = stored(vertex)
            index = lookup.get(kept)
            if index is None:
                index = len(lookup)
                if index >= MAX_VERTICES:
                    raise ValueError('too many vertices')
                lookup[kept] = index
                vertices.extend(kept)
            indices.append(index)
        faces.extend(raw[:8])
        faces.extend(struct.pack('<4H', *indices, 0))
        count += 1
        if count > 0xFFFFFFFF:
            raise ValueError('too many faces')
    blob = HEADER.pack(magic, len(lookup), count) + vertices + faces
    return bytes(blob) + struct.pack('<I', zlib.crc32(blob) & 0xFFFFFFFF)


def decode_sector(blob):
    """Validate every field before returning byte-identical 116-byte faces."""
    return _decode(blob, b'DRX1', VERTEX.size, lambda stored: stored)


def decode_draw_sector(blob):
    """Validate a DRX2 block and return 116-byte faces with zero normals."""
    return _decode(blob, b'DRX2', DRAW_VERTEX.size,
                   lambda stored: stored[:20] + ZERO_NORMAL + stored[20:])


def strip_normals(record):
    """A DRM2 face as DRX2 decodes it: the oracle for its round trip."""
    raw = bytearray(_bytes(record, 'malformed record'))
    if len(raw) != FACE_BYTES:
        raise ValueError('malformed record size')
    for offset in (8, 44, 80):
        raw[offset + 20:offset + 32] = ZERO_NORMAL
    return bytes(raw)


def _decode(blob, wanted, vertex_size, expand):
    blob = _bytes(blob, 'malformed blob')
    if len(blob) < HEADER.size + 4:
        raise ValueError('truncated header')
    magic, vertex_count, face_count = HEADER.unpack_from(blob)
    if magic != wanted:
        raise ValueError('bad magic')
    if vertex_count > MAX_VERTICES:
        raise ValueError('too many vertices')
    face_offset = HEADER.size + vertex_count * vertex_size
    if len(blob) != face_offset + face_count * FACE.size + 4:
        raise ValueError('incorrect size')
    if zlib.crc32(blob[:-4]) & 0xFFFFFFFF != struct.unpack_from('<I', blob, len(blob) - 4)[0]:
        raise ValueError('CRC mismatch')
    vertices = [expand(blob[i:i + vertex_size])
                for i in range(HEADER.size, face_offset, vertex_size)]
    for vertex in vertices:
        _finite(vertex)
    result = []
    for offset in range(face_offset, len(blob) - 4, FACE.size):
        _, _, i0, i1, i2, pad = FACE.unpack_from(blob, offset)
        if pad:
            raise ValueError('nonzero pad')
        if max(i0, i1, i2) >= vertex_count:
            raise ValueError('index out of range')
        result.append(blob[offset:offset + 8] + vertices[i0] + vertices[i1] + vertices[i2])
    return result


def measure(package_dir, queries=()):
    """Return a JSON-compatible report, proving each block's round trip.

    DRM2 raw bytes are the decoded face payload (116 * face_count); DRX1 raw
    bytes include its header and CRC. DRM2 compressed file sizes are separate.
    Totals include global.drs, even when empty. Windows use the packer's closed
    rectangle overlap test and always read global.drs once; border replicas
    remain counted in every touched block. This measures indexed cache storage,
    not the memory for expanded draw faces or a fully integrated renderer.
    """
    # The existing DRM2 parser imports Pillow; the DRX1 codec itself is stdlib.
    import prepare_render_sectors as drm

    root = Path(package_dir)
    header = drm.parse_manifest((root / 'manifest.drm').read_bytes())
    rows = []
    entries = [(f"sectors/{e['ix']}_{e['iz']}.drs", e) for e in header['entries']]
    entries.append(('global.drs', None))
    for name, entry in entries:
        blob = (root / name).read_bytes()
        parsed = drm.parse_sector(blob)
        global_ = entry is None
        expected = (dict(record_count=header['global_count'],
                         uncompressed_bytes=header['global_uncompressed'],
                         file_bytes=header['global_file_bytes'],
                         payload_crc=header['global_payload_crc'],
                         compressed_crc=header['global_compressed_crc'])
                    if global_ else entry)
        if (parsed['is_global'] != global_
                or (not global_ and (parsed['ix'], parsed['iz']) != (entry['ix'], entry['iz']))
                or len(blob) != expected['file_bytes']
                or any(parsed[k] != expected[k] for k in
                       ('record_count', 'uncompressed_bytes', 'payload_crc', 'compressed_crc'))):
            raise ValueError('sector does not match manifest')
        for record in parsed['records']:
            if (record['source_id'] >= header['source_count']
                    or record['material_id'] >= header['material_count']):
                raise ValueError('record does not match manifest')
        raw = [r['raw'] for r in parsed['records']]
        indexed = encode_sector(raw)
        if decode_sector(indexed) != raw:
            raise ValueError('round trip mismatch')
        draw = encode_draw_sector(raw)
        if decode_draw_sector(draw) != [strip_normals(r) for r in raw]:
            raise ValueError('draw round trip mismatch')
        rows.append(dict(path=name, ix=None if global_ else entry['ix'],
                         iz=None if global_ else entry['iz'], is_global=global_,
                         face_count=len(raw), vertex_count=HEADER.unpack_from(indexed)[1],
                         draw_vertex_count=HEADER.unpack_from(draw)[1],
                         drm2_raw_bytes=len(raw) * FACE_BYTES,
                         drx1_raw_bytes=len(indexed), drx2_raw_bytes=len(draw),
                         drm2_file_bytes=len(blob)))
    totals = {k: sum(r[k] for r in rows) for k in
              ('face_count', 'vertex_count', 'draw_vertex_count', 'drm2_raw_bytes',
               'drx1_raw_bytes', 'drx2_raw_bytes', 'drm2_file_bytes')}
    totals['raw_bytes_saved'] = totals['drm2_raw_bytes'] - totals['drx1_raw_bytes']
    windows = []
    for query in queries:
        bounds = drm._query_bounds(query)
        touched = []
        for row in rows:
            if row['is_global']:
                touched.append(row)
                continue
            x0, x1, z0, z1 = drm.cell_bounds(row['ix'], row['iz'], header['cell_size'])
            if (drm._interval_overlap(x0, x1, bounds[0], bounds[1])
                    and drm._interval_overlap(z0, z1, bounds[2], bounds[3])):
                touched.append(row)
        windows.append(dict(query=list(bounds), sectors=[r['path'] for r in touched],
                            **{k: sum(r[k] for r in touched) for k in
                               ('drm2_raw_bytes', 'drx1_raw_bytes', 'drx2_raw_bytes',
                                'drm2_file_bytes')}))
    return dict(format='DRX1', round_trip_verified=True,
                measurement='decoded DRM2 face payload vs complete DRX1 and DRX2 blocks; global included',
                sectors=rows, totals=totals,
                biggest_sector={fmt: max(rows, key=lambda r: r[fmt + '_raw_bytes'])
                                for fmt in ('drm2', 'drx1', 'drx2')}, windows=windows)


def write_draw_files(package_dir, remove_sectors=False):
    """Write a .drx beside each published .drs. Removing the .drs needs a block
    cache budget of at least the biggest .drx, since nothing is left to fall back on."""
    # The existing DRM2 parser imports Pillow; the DRX1 codec itself is stdlib.
    import prepare_render_sectors as drm

    root = Path(package_dir)
    written = 0
    for path in [root / 'global.drs', *sorted((root / 'sectors').glob('*.drs'))]:
        records = [r['raw'] for r in drm.parse_sector(path.read_bytes())['records']]
        blob = encode_draw_sector(records)
        path.with_suffix('.drx').write_bytes(blob)
        written += len(blob)
        if remove_sectors:
            path.unlink()
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package_dir', type=Path)
    parser.add_argument('--query', nargs='+', action='append', default=[],
                        metavar='X0,X1,Z0,Z1', help='closed world-space XZ window(s); repeatable')
    parser.add_argument('--json', type=Path, help='also save the JSON report')
    args = parser.parse_args(argv)
    try:
        queries = [tuple(map(float, q.split(','))) for group in args.query for q in group]
        report = json.dumps(measure(args.package_dir, queries), indent=2, sort_keys=True) + '\n'
        if args.json:
            args.json.write_text(report, encoding='utf-8')
        print(report, end='')
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

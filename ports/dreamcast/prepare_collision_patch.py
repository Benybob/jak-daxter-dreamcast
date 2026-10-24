"""Extract a bounded local DCC1 collision patch from original FR3 (AI-assisted).

Uses an installed libzstd and the built export_collision_fr3 host helper, which
reads the exact upstream serializer with assertions and complete-consumption
checks. Generated payloads contain game assets and must stay private.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile


def extract(path, reader, output, origin, radius, zstd=None):
    if len(origin) != 3 or not all(math.isfinite(x) for x in origin) or not 1 <= radius <= 64:
        raise ValueError('invalid patch origin/radius')
    compressed = path.read_bytes()
    if len(compressed) < 8 or len(compressed) > 128*1024*1024: raise ValueError('FR3 size limit')
    size, = struct.unpack_from('<Q', compressed)
    if not 0 < size <= 256*1024*1024: raise ValueError('FR3 decompressed size limit')
    library = str(Path(zstd).resolve()) if zstd else ctypes.util.find_library('zstd')
    if not library: raise ValueError('supply --zstd path to installed libzstd')
    lib = ctypes.CDLL(str(library))
    lib.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    lib.ZSTD_decompress.restype = ctypes.c_size_t
    raw = ctypes.create_string_buffer(size)
    if lib.ZSTD_decompress(raw, size, compressed[8:], len(compressed)-8) != size:
        raise ValueError('FR3 decompression failed')
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory); (directory/'level.raw').write_bytes(raw.raw)
        result = subprocess.run([str(reader.resolve()), str(directory/'level.raw'), str(directory/'level.dcr')], capture_output=True, text=True, check=True)
        data = (directory/'level.dcr').read_bytes()
    magic, version, fr3_version, count = struct.unpack_from('<4sIII', data)
    if (magic, version, fr3_version) != (b'DCR1', 1, 43) or len(data) != 16+count*40:
        raise ValueError('invalid serializer interchange')
    selected = []
    for values in struct.iter_unpack('<9fI', data[16:]):
        points = [values[i:i+3] for i in (0, 3, 6)]
        if all(min(p[k] for p in points) <= origin[k]+(12 if k == 1 else radius) and
               max(p[k] for p in points) >= origin[k]-(12 if k == 1 else radius) for k in range(3)):
            selected.append(tuple(p[k]-origin[k] for p in points for k in range(3))+(values[9],))
    if not 0 < len(selected) <= 8192: raise ValueError('patch triangle budget exceeded')
    payload = bytearray(struct.pack('<4sII3f', b'DCC1', 1, len(selected), *origin))
    for triangle in selected: payload += struct.pack('<9fI', *triangle)
    output.parent.mkdir(parents=True, exist_ok=True); output.write_bytes(payload)
    report = dict(ai_assisted=True, source_sha256=hashlib.sha256(compressed).hexdigest(), source_fr3_version=fr3_version,
                  source_triangles=count, selected_triangles=len(selected), origin_meters=list(origin), selection_radius=radius,
                  payload_bytes=len(payload), source_units_per_meter=4096, whole_intersecting_triangles=True,
                  serializer=result.stdout.strip(), noentity_triangles=sum(bool(t[9]&1) for t in selected),
                  event_values=sorted(set((t[9]>>14)&63 for t in selected)))
    output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n'); return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('fr3', type=Path); p.add_argument('output', type=Path)
    p.add_argument('--reader', type=Path, required=True); p.add_argument('--zstd'); p.add_argument('--origin', type=float, nargs=3, required=True)
    p.add_argument('--radius', type=float, default=8); a = p.parse_args()
    print(json.dumps(extract(a.fr3, a.reader, a.output, a.origin, a.radius, a.zstd), indent=2))

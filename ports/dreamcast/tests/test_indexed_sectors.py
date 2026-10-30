"""Synthetic lossless DRX1, C refusal atomicity and cache accounting. (AI-assisted), ISC."""

import ctypes
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import indexed_sectors as indexed
import prepare_render_sectors as drm
from test_render_sectors import GlbBuilder


def synthetic_package(with_global=False):
    builder = GlbBuilder()
    positions = [(3, 0, 1), (5, 0, 1), (5, 0, 3), (3, 0, 3)]
    attributes = {'POSITION': builder.add_vec3(positions),
                  'NORMAL': builder.add_vec3([(0, 1, 0)] * 4),
                  'TEXCOORD_0': builder.add_vec2([(0, 0), (1, 0), (1, 1), (0, 1)])}
    primitives = [dict(attributes=attributes, indices=builder.add_indices([0, 1, 2, 0, 2, 3]))]
    if with_global:
        attributes = {'POSITION': builder.add_vec3([(1, 0, 1), (101, 0, 1), (1, 0, 101)])}
        primitives.append(dict(attributes=attributes, indices=builder.add_indices([0, 1, 2])))
    mesh = builder.add_mesh(primitives)
    builder.scenes[0]['nodes'].append(builder.add_node(mesh))
    doc, binary = drm.parse_glb_bytes(builder.to_bytes())
    return drm.build_package(doc, binary, cell_size=4, texture_size=8)


def quad_records():
    package = synthetic_package()
    parsed = drm.parse_sector(package.sectors[(0, 0)])
    return [r['raw'] for r in parsed['records']]


def resign(blob):
    raw = bytearray(blob)
    struct.pack_into('<I', raw, len(raw) - 4, zlib.crc32(raw[:-4]) & 0xFFFFFFFF)
    return bytes(raw)


def mutate(blob, offset, fmt, value):
    raw = bytearray(blob)
    struct.pack_into(fmt, raw, offset, value)
    return resign(raw)


def corruptions(blob):
    vertices = struct.unpack_from('<I', blob, 4)[0]
    face = 12 + vertices * 36
    bad_crc = bytearray(blob)
    bad_crc[-1] ^= 1
    result = [(b'BAD!' + blob[4:], 'bad magic'), (bytes(bad_crc), 'CRC mismatch'),
              (blob + b'X', 'incorrect size'),
              (mutate(blob, 4, '<I', 65536), 'too many vertices'),
              (mutate(blob, 8, '<I', 0xFFFFFFFF), 'incorrect size'),
              (mutate(blob, face + 14, '<H', 1), 'nonzero pad')]
    for offset in (8, 10, 12):
        result.append((mutate(blob, face + offset, '<H', vertices), 'index out of range'))
    for offset in range(0, 32, 4):
        for value in (math.nan, math.inf, -math.inf):
            result.append((mutate(blob, 12 + offset, '<f', value), 'nonfinite vertex'))
    # A failure in the final face/vertex must not leave partially written faces.
    result.append((mutate(blob, face + 16 + 14, '<H', 1), 'nonzero pad'))
    result.append((mutate(blob, 12 + (vertices - 1) * 36, '<f', math.nan), 'nonfinite vertex'))
    return result


class IndexedSectorTests(unittest.TestCase):
    def test_published_shared_and_border_crossing_round_trip(self):
        package = synthetic_package(with_global=True)
        with tempfile.TemporaryDirectory() as directory:
            root = drm.publish_package(Path(directory) / 'published', package)
            paths = sorted((root / 'sectors').glob('*.drs')) + [root / 'global.drs']
            sources = []
            for path in paths:
                parsed = drm.parse_sector(path.read_bytes())
                records = [r['raw'] for r in parsed['records']]
                self.assertEqual(indexed.decode_sector(indexed.encode_sector(records)), records)
                # Also accept the actual dictionary records parse_sector returns.
                self.assertEqual(indexed.encode_sector(parsed['records']), indexed.encode_sector(records))
                if not parsed['is_global']:
                    sources.append([r['source_id'] for r in parsed['records']])
            self.assertEqual(sources, [[0, 1], [0, 1]])
            self.assertEqual(len(paths), 3)

    def test_quad_deduplicates_exact_bytes_in_first_use_order(self):
        records = quad_records()
        blob = indexed.encode_sector(records)
        self.assertEqual(struct.unpack_from('<4sII', blob), (b'DRX1', 4, 2))
        self.assertEqual(len(blob), 16 + 4 * 36 + 2 * 16)
        self.assertEqual(zlib.crc32(blob[:-4]), struct.unpack_from('<I', blob, len(blob) - 4)[0])
        expected = records[0][8:] + records[1][80:116]
        self.assertEqual(blob[12:12 + 4 * 36], expected)
        self.assertEqual(struct.unpack_from('<4H', blob, 12 + 4 * 36 + 8), (0, 1, 2, 0))
        self.assertEqual(struct.unpack_from('<4H', blob, 12 + 4 * 36 + 24), (0, 2, 3, 0))
        self.assertEqual(indexed.decode_sector(blob), records)

    def test_sharing_preserves_signed_zero_uv_normal_and_colour(self):
        vertex = struct.pack('<8fI', 0., 0., 0., 0., 0., 0., 0., 0., 0xAABBCCDD)
        variants = [vertex]
        for offset, value in ((0, 0x80000000), (12, 0x3F800000),
                              (20, 0x3F800000), (32, 0xFFBBCCDD)):
            item = bytearray(vertex)
            struct.pack_into('<I', item, offset, value)
            variants.append(bytes(item))
        records = [struct.pack('<II', 0xFFFFFFFF - i, i) + vertex + v + vertex
                   for i, v in enumerate(variants)]
        blob = indexed.encode_sector(records)
        self.assertEqual(struct.unpack_from('<I', blob, 4)[0], 5)
        self.assertEqual(indexed.decode_sector(blob), records)

    def test_refuses_malformed_records_and_nonfinite_input(self):
        record = quad_records()[0]
        for malformed in (None, 'x' * 116, 116, b'', b'x' * 115, b'x' * 117, {}, {'raw': None}):
            with self.subTest(record=repr(malformed)[:40]):
                with self.assertRaisesRegex(ValueError, 'malformed record'):
                    indexed.encode_sector([malformed])
        with self.assertRaisesRegex(ValueError, 'malformed records'):
            indexed.encode_sector(None)
        for vertex in range(3):
            for field in range(8):
                raw = bytearray(record)
                struct.pack_into('<f', raw, 8 + vertex * 36 + field * 4, math.nan)
                with self.assertRaisesRegex(ValueError, 'nonfinite vertex'):
                    indexed.encode_sector([raw])

    def test_decode_refusals_and_every_truncation(self):
        blob = indexed.encode_sector(quad_records())
        for size in range(len(blob)):
            with self.subTest(size=size):
                with self.assertRaises(ValueError):
                    indexed.decode_sector(blob[:size])
        for invalid, reason in corruptions(blob):
            with self.subTest(reason=reason):
                with self.assertRaisesRegex(ValueError, '^' + reason + '$'):
                    indexed.decode_sector(invalid)
        with self.assertRaisesRegex(ValueError, 'malformed blob'):
            indexed.decode_sector(None)

    def test_empty_sector_and_unreferenced_nonfinite_vertex(self):
        blob = indexed.encode_sector([])
        self.assertEqual(len(blob), 16)
        self.assertEqual(indexed.decode_sector(blob), [])
        # Even unreferenced vertices must be finite.
        raw = struct.pack('<4sII8fI', b'DRX1', 1, 0, math.inf, 0, 0, 0, 0, 0, 0, 0, 0) + b'\0' * 4
        with self.assertRaisesRegex(ValueError, 'nonfinite vertex'):
            indexed.decode_sector(resign(raw))

    def test_vertex_limit_boundary(self):
        records = []
        for start in range(0, 65535, 3):
            vertices = b''.join(struct.pack('<8fI', float(i), 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF)
                                for i in range(start, start + 3))
            records.append(struct.pack('<II', start // 3, 0) + vertices)
        blob = indexed.encode_sector(records)
        self.assertEqual(struct.unpack_from('<I', blob, 4)[0], 65535)
        self.assertEqual(indexed.decode_sector(blob), records)
        extra = struct.pack('<8fI', 65535., 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValueError, 'too many vertices'):
            indexed.encode_sector(records + [struct.pack('<II', 99999, 0) + extra * 3])

    def test_measure_exact_totals_and_closed_boundary_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = drm.publish_package(Path(directory) / 'package', synthetic_package())
            queries = [(1, 2, 1, 2), (3, 5, 1, 2), (4, 4, 1, 2), (20, 21, 20, 21)]
            report = indexed.measure(root, queries)
            self.assertTrue(report['round_trip_verified'])
            self.assertEqual(len(report['sectors']), 3)
            self.assertEqual(report['totals']['drm2_raw_bytes'], 2 * 232)
            self.assertEqual(report['totals']['drx1_raw_bytes'], 2 * 192 + 16)
            self.assertEqual(report['totals']['raw_bytes_saved'], 64)
            self.assertEqual(report['totals']['drm2_file_bytes'],
                             sum(p.stat().st_size for p in root.rglob('*.drs')))
            self.assertEqual([(r['drm2_raw_bytes'], r['drx1_raw_bytes']) for r in report['windows']],
                             [(232, 208), (464, 400), (464, 400), (0, 16)])
            self.assertEqual(report['biggest_sector']['drm2']['drm2_raw_bytes'], 232)
            self.assertEqual(report['biggest_sector']['drx1']['drx1_raw_bytes'], 192)
            self.assertEqual(report['windows'][0]['sectors'], ['sectors/0_0.drs', 'global.drs'])
            json.dumps(report)
            for bad in ((1, 0, 0, 1), (0, 1, 0), (0, math.nan, 0, 1)):
                with self.assertRaises(ValueError):
                    indexed.measure(root, [bad])

    def test_measure_includes_nonempty_global_in_every_window(self):
        with tempfile.TemporaryDirectory() as directory:
            root = drm.publish_package(Path(directory) / 'package', synthetic_package(True))
            report = indexed.measure(root, [(1000, 1001, 1000, 1001)])
            self.assertEqual(report['totals']['drm2_raw_bytes'], 580)
            self.assertEqual(report['totals']['drx1_raw_bytes'], 524)
            self.assertEqual(report['windows'][0]['sectors'], ['global.drs'])
            self.assertEqual(report['windows'][0]['drm2_raw_bytes'], 116)
            self.assertEqual(report['windows'][0]['drx1_raw_bytes'], 140)

    def test_measure_rejects_valid_sector_with_wrong_manifest_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = drm.publish_package(Path(directory) / 'package', synthetic_package())
            # Same records but wrong cell identity; internal DRS2 CRCs remain valid.
            raw = quad_records()
            (root / 'sectors/0_0.drs').write_bytes(drm.encode_sector(raw, 10, 10))
            with self.assertRaisesRegex(ValueError, 'sector does not match manifest'):
                indexed.measure(root)

    def test_cli_json_and_multiple_queries(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = drm.publish_package(base / 'package', synthetic_package())
            out = base / 'measurement.json'
            result = subprocess.run([sys.executable, str(PORT / 'indexed_sectors.py'), str(root),
                                     '--query', '1,2,1,2', '3,5,1,2', '--query', '4,4,1,2',
                                     '--json', str(out)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report, json.loads(out.read_text(encoding='utf-8')))
            self.assertEqual(len(report['windows']), 3)
            self.assertEqual(report['totals']['raw_bytes_saved'], 64)


@unittest.skipUnless(shutil.which('gcc'), 'gcc is required for the C decoder oracle')
class IndexedSectorNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ('indexed.dll' if os.name == 'nt' else 'indexed.so')
        command = [shutil.which('gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                   '-O2', '-shared', '-fPIC', str(PORT / 'render_indexed.c'), '-o', str(library)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise AssertionError('C99 decoder build failed:\n' + result.stdout + result.stderr)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == 'nt':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.decode = cls.lib.dc_render_indexed_decode
        cls.decode.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p,
                               ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        cls.decode.restype = ctypes.c_char_p

    def call(self, blob, capacity=2):
        source = ctypes.create_string_buffer(b'X' + blob)
        # Unaligned input/output addresses and canaries guard the byte-copy API.
        output = ctypes.create_string_buffer(b'\xA5' * (capacity * 116 + 17))
        before = output.raw
        count = ctypes.c_uint32(0xDEADBEEF)
        error = self.decode(ctypes.byref(source, 1), len(blob), ctypes.byref(output, 1),
                            capacity, ctypes.byref(count))
        if error:
            self.assertEqual(output.raw, before)
            self.assertEqual(count.value, 0xDEADBEEF)
            return error.decode('ascii'), None
        self.assertEqual(output.raw[:1], before[:1])
        self.assertEqual(output.raw[1 + count.value * 116:], before[1 + count.value * 116:])
        return None, output.raw[1:1 + count.value * 116]

    def test_byte_identical_shared_border_and_global_sectors(self):
        package = synthetic_package(True)
        for sector in list(package.sectors.values()) + [package.global_sector]:
            records = [r['raw'] for r in drm.parse_sector(sector)['records']]
            blob = indexed.encode_sector(records)
            error, raw = self.call(blob)
            self.assertIsNone(error)
            self.assertEqual(raw, b''.join(indexed.decode_sector(blob)))
            self.assertEqual(raw, b''.join(records))

    def test_every_truncation_and_corruption_is_atomic(self):
        blob = indexed.encode_sector(quad_records())
        for size in range(len(blob)):
            with self.subTest(size=size):
                self.assertIsNotNone(self.call(blob[:size])[0])
        for invalid, reason in corruptions(blob):
            with self.subTest(reason=reason):
                self.assertEqual(self.call(invalid)[0], reason)
        self.assertEqual(self.call(blob, capacity=1)[0], 'output capacity')

    def test_empty_block_null_arguments_and_overlap_are_atomic(self):
        empty = indexed.encode_sector([])
        count = ctypes.c_uint32(73)
        self.assertIsNone(self.decode(empty, len(empty), None, 0, ctypes.byref(count)))
        self.assertEqual(count.value, 0)
        blob = indexed.encode_sector(quad_records())
        count.value = 73
        self.assertEqual(self.decode(None, len(blob), None, 2, ctypes.byref(count)), b'null blob')
        self.assertEqual(self.decode(blob, len(blob), None, 2, ctypes.byref(count)), b'null output')
        self.assertEqual(self.decode(blob, len(blob), None, 2, None), b'null count')
        self.assertEqual(count.value, 73)
        storage = ctypes.create_string_buffer(blob + b'\xA5' * 300)
        before = storage.raw
        self.assertEqual(self.decode(storage, len(blob), storage, 2, ctypes.byref(count)),
                         b'overlapping buffers')
        self.assertEqual(storage.raw, before)
        self.assertEqual(count.value, 73)
        output = ctypes.create_string_buffer(b'\xA5' * 232)
        self.assertEqual(self.decode(blob, len(blob), output, 2,
                         ctypes.cast(output, ctypes.POINTER(ctypes.c_uint32))), b'overlapping buffers')
        self.assertEqual(output.raw, b'\xA5' * 232 + b'\0')
        self.assertEqual(self.decode(storage, len(blob), output, 2,
                         ctypes.cast(storage, ctypes.POINTER(ctypes.c_uint32))), b'overlapping buffers')
        self.assertEqual(storage.raw, before)


if __name__ == '__main__':
    unittest.main()

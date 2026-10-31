"""instance_cells.c against instance_cells.py on synthetic packages. (AI-assisted), ISC."""
import ctypes
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import instance_cells as cells


def package(placements, geometry=b'G' * 40):
    records = b''.join(cells.INSTANCE.pack(p % 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, z)
                       for p, (x, z) in enumerate(placements))
    return b'DCI3' + struct.pack('<3I', 3, 3, len(placements)) + geometry + records


class SplitTests(unittest.TestCase):
    def test_split_keeps_order_and_geometry(self):
        with tempfile.TemporaryDirectory() as d:
            blob = package([(1, 1), (20, 1), (2, 2), (-1, -1)])
            trimmed = cells.split(blob, d)
            self.assertEqual(len(trimmed), 16 + 40)
            self.assertEqual(struct.unpack_from('<I', trimmed, 12)[0], 0)
            self.assertEqual(trimmed[16:], b'G' * 40)
            rows = (Path(d) / 'cells/0_0.dci').read_bytes()
            self.assertEqual([struct.unpack_from('<H', rows, 2 + 52 * i)[0] for i in range(2)], [0, 2])
            self.assertTrue((Path(d) / 'cells/-1_-1.dci').exists())

    def test_refuses_other_formats(self):
        with self.assertRaises(ValueError):
            cells.read(b'DCI1' + bytes(20))

    def test_a_cell_is_held_by_its_nearest_point(self):
        self.assertTrue(cells.touches(1, 0, 0, 8, 16))
        self.assertFalse(cells.touches(2, 0, 0, 8, 16))
        self.assertFalse(cells.touches(1, 1, 0, 0, 16))

    def test_the_renderers_rounding_cannot_lose_an_instance(self):
        # dx rounds to exactly 80 in float32, so the renderer draws this record,
        # though in exact arithmetic it is past the radius.
        x = -2.0 ** -20
        f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]  # noqa: E731
        self.assertEqual(f32(80.0 - x), 80.0)
        self.assertGreater((80.0 - x) ** 2, 6400.0)
        self.assertEqual(cells.resident(package([(80.0, 0.0)]), x, 0.0, 80.0), [0])

    def test_a_second_split_leaves_no_stale_cell(self):
        with tempfile.TemporaryDirectory() as d:
            cells.split(package([(1, 1)]), d)
            cells.split(package([(17, 1)]), d)
            self.assertEqual([f.name for f in (Path(d) / 'cells').iterdir()], ['1_0.dci'])


class Package(ctypes.Structure):
    _fields_ = [('protos', ctypes.c_void_p), ('verts', ctypes.c_void_p), ('indices', ctypes.c_void_p),
                ('instances', ctypes.c_void_p), ('proto_count', ctypes.c_uint32),
                ('instance_count', ctypes.c_uint32), ('vert_count', ctypes.c_uint32),
                ('index_count', ctypes.c_uint32), ('max_proto_verts', ctypes.c_uint32),
                ('lods', ctypes.c_void_p), ('lod_count', ctypes.c_uint32), ('bounds', ctypes.c_void_p),
                ('cverts', ctypes.c_void_p), ('quant', ctypes.c_void_p)]


class Owner(ctypes.Structure):
    _fields_ = [('records', ctypes.c_void_p), ('capacity', ctypes.c_uint32), ('count', ctypes.c_uint32),
                ('cell_count', ctypes.c_uint32), ('cell', ctypes.c_double), ('root', ctypes.c_char * 192),
                ('cells', (ctypes.c_int32 * 5) * 256), ('palette', ctypes.c_void_p),
                ('lighting', ctypes.c_void_p), ('lighting_capacity', ctypes.c_uint32),
                ('lighting_used', ctypes.c_uint32), ('tables', ctypes.c_void_p)]


@unittest.skipUnless(shutil.which('gcc'), 'gcc is required for the C oracle')
class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ('cells.dll' if os.name == 'nt' else 'cells.so')
        result = subprocess.run([shutil.which('gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2', '-shared',
                                 '-static', str(PORT / 'instance_cells.c'), str(PORT / 'instance_pass.c'), '-o', str(library), '-lm'],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.lib = ctypes.CDLL(str(library))
        if os.name == 'nt':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.dc_instance_cells_make.restype = ctypes.c_char_p
        cls.lib.dc_instance_cells_make.argtypes = [ctypes.POINTER(Owner), ctypes.c_char_p, ctypes.c_double, ctypes.c_uint32]
        cls.lib.dc_instance_cells_update.restype = ctypes.c_char_p
        cls.lib.dc_instance_cells_update.argtypes = [ctypes.POINTER(Owner), ctypes.POINTER(Package)] + [ctypes.c_double] * 3
        cls.lib.dc_instance_cells_free.argtypes = [ctypes.POINTER(Owner)]
        cls.lib.dc_instance_cells_free.restype = None

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = self.temp.name
        self.owner = Owner()
        self.addCleanup(self.lib.dc_instance_cells_free, ctypes.byref(self.owner))
        self.pkg = Package(proto_count=3)

    def make(self, capacity):
        self.assertIsNone(self.lib.dc_instance_cells_make(ctypes.byref(self.owner), self.root.encode(), 16.0, capacity))

    def update(self, x, z, draw):
        return self.lib.dc_instance_cells_update(ctypes.byref(self.owner), ctypes.byref(self.pkg), x, z, draw)

    def indices(self):
        raw = ctypes.string_at(self.pkg.instances, self.pkg.instance_count * 52) if self.pkg.instance_count else b''
        return [struct.unpack_from('<H', raw, 2 + 52 * i)[0] for i in range(self.pkg.instance_count)]

    def test_a_path_holds_exactly_the_python_records_in_order(self):
        rng = random.Random(1)
        places = [(rng.uniform(-200, 200), rng.uniform(-200, 200)) for _ in range(1500)]
        blob = package(places)
        cells.split(blob, self.root)
        targets = [(rng.uniform(-150, 150), rng.uniform(-150, 150)) for _ in range(5)]
        path = [(float(x), float(x) * .7) for x in range(-160, 160, 7)] + targets
        self.make(cells.peak(blob, path, 80.0))
        for x, z in path:
            self.assertIsNone(self.update(x, z, 80.0))
            self.assertEqual(self.indices(), cells.resident(blob, x, z, 80.0))
            for i, (px, pz) in enumerate(places):
                if (px - x) ** 2 + (pz - z) ** 2 <= 6400:
                    self.assertIn(i, self.indices())

    def test_capacity_failure_keeps_a_consistent_subset(self):
        blob = package([(1, 1), (2, 2), (20, 1), (21, 2)])
        cells.split(blob, self.root)
        self.make(3)
        self.assertEqual(self.update(0, 0, 40.0), b'instance cells capacity')
        self.assertEqual(self.indices(), [0, 1])

    def test_bad_cells_are_refused(self):
        blob = package([(1, 1), (2, 2)])
        for name, spoil in (('truncated', lambda r: r[:-1]), ('proto', lambda r: struct.pack('<H', 9) + r[2:]),
                            ('nan', lambda r: r[:4] + struct.pack('<f', math.nan) + r[8:]),
                            ('order', lambda r: r[52:] + r[:52]),
                            ('placed', lambda r: r[:40] + struct.pack('<f', 99.0) + r[44:])):
            with self.subTest(name=name):
                cells.split(blob, self.root)
                f = Path(self.root) / 'cells/0_0.dci'
                f.write_bytes(spoil(f.read_bytes()))
                owner = Owner()
                self.assertIsNone(self.lib.dc_instance_cells_make(ctypes.byref(owner), self.root.encode(), 16.0, 8))
                pkg = Package(proto_count=3)
                self.assertIsNotNone(self.lib.dc_instance_cells_update(ctypes.byref(owner), ctypes.byref(pkg), 0, 0, 8.0))
                self.assertEqual(pkg.instance_count, 0)
                self.lib.dc_instance_cells_free(ctypes.byref(owner))

    def test_a_cell_that_cannot_be_opened_is_an_error_and_retried(self):
        blob = package([(1, 1)])
        cells.split(blob, self.root)
        target = Path(self.root) / 'cells/0_0.dci'
        data = target.read_bytes()
        target.unlink()
        target.mkdir()  # exists, and cannot be read as a file
        self.make(4)
        first = self.update(8, 8, 1.0)
        target.rmdir()
        target.write_bytes(data)
        self.assertIsNotNone(first)
        self.assertIsNone(self.update(8, 8, 1.0))
        self.assertEqual(self.indices(), [0])

    def test_unconvertible_positions_are_refused(self):
        self.make(4)
        for x, z, draw in ((0, 0, math.inf), (0, 0, math.nan), (3.4e10, 0, 80), (0, -3.4e10, 80), (math.inf, 0, 80)):
            with self.subTest(x=x, z=z, draw=draw):
                self.assertEqual(self.update(x, z, draw), b'instance cells arguments')

    def test_arguments(self):
        self.assertIsNotNone(self.lib.dc_instance_cells_make(ctypes.byref(self.owner), self.root.encode(), 0.0, 8))
        self.assertIsNotNone(self.lib.dc_instance_cells_make(ctypes.byref(self.owner), self.root.encode(), 16.0, 0))


if __name__ == '__main__':
    unittest.main()

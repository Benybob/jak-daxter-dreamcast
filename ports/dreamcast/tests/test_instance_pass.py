"""instance_pass.c: the DCI1 loader and the shared world-view camera (AI-assisted), ISC.

Synthetic packages only, built by prototype_geometry.package.
"""
from pathlib import Path
import ctypes
import math
import struct
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import prototype_geometry as pg  # noqa: E402


class Package(ctypes.Structure):
    _fields_ = [(n, ctypes.c_void_p) for n in ('protos', 'verts', 'indices', 'instances')] + \
               [(n, ctypes.c_uint32) for n in ('proto_count', 'instance_count', 'vert_count', 'index_count',
                                                'max_proto_verts')]


class Camera(ctypes.Structure):
    _fields_ = [(n, ctypes.c_float) for n in ('tx', 'ty', 'tz', 'yaw', 'pitch', 'distance', 'draw_distance')]


def triangle(x):
    return [(x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 1.0, 0.0)], [(0.0, 0.0)] * 3, [(255, 128, 0, 255)] * 3, 7


def sample():
    """Two prototypes (one with an odd index count, so the indices pad) and three instances."""
    results = [{'name': 'post', 'geometry': [triangle(0.0)]},
               {'name': 'broken'},
               {'name': 'fence', 'geometry': [triangle(0.0), triangle(2.0)]}]
    origin = [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]
    instances = [{'proto': 0, 'origin': origin, 'x': 1.0, 'y': 2.0, 'z': 3.0},
                 {'proto': 1, 'origin': origin, 'x': 0.0, 'y': 0.0, 'z': 0.0},
                 {'proto': 2, 'origin': origin, 'x': -4.0, 'y': 0.0, 'z': 8.0},
                 {'proto': 0, 'origin': origin, 'x': 9.0, 'y': 0.0, 'z': 0.0}]
    return pg.package(results, instances)


class InstancePassTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dll = PORT / 'build/instance-pass-check.dll'
        if not dll.exists():
            dll = dll.with_suffix('.so')
        cls.lib = ctypes.CDLL(str(dll))
        cls.open = cls.lib.dc_instance_package_open
        cls.open.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Package)]
        cls.open.restype = ctypes.c_char_p
        cls.view = cls.lib.dc_instance_view
        cls.view.argtypes = [ctypes.POINTER(Camera), ctypes.c_float * 10]
        cls.to_view = cls.lib.dc_instance_to_view
        cls.to_view.argtypes = [ctypes.c_float * 10, ctypes.POINTER(Camera), ctypes.c_float * 3, ctypes.c_float * 3]

    def load(self, blob):
        buf = (ctypes.c_uint32 * ((len(blob) + 3) // 4 + 1))()
        ctypes.memmove(buf, blob, len(blob))
        out = Package()
        ctypes.memset(ctypes.byref(out), 0x5a, ctypes.sizeof(out))
        before = bytes(out)
        err = self.open(buf, len(blob), ctypes.byref(out))
        if err:
            self.assertEqual(bytes(out), before)  # out unchanged on a refusal
        return err, out, buf

    def test_writer_pads_and_loader_reads_it(self):
        blob, counts = sample()
        self.assertEqual(counts, {'prototypes': 2, 'instances': 3, 'vertices': 9, 'indices': 9})
        self.assertEqual(len(blob) % 4, 0)
        err, out, buf = self.load(blob)
        self.assertIsNone(err)
        self.assertEqual((out.proto_count, out.instance_count, out.vert_count, out.index_count, out.max_proto_verts),
                         (2, 3, 9, 9, 6))
        base = ctypes.addressof(buf)
        self.assertEqual(out.instances - base, 12 + 2 * 52 + 9 * 24 + 9 * 2 + 2)
        proto, _, *m = struct.unpack_from('<2H12f', blob, out.instances - base + 52)
        self.assertEqual(proto, 1)          # 'broken' dropped, 'fence' renumbered
        self.assertEqual(m[9:], [-4.0, 0.0, 8.0])

    def test_refusals(self):
        blob, _ = sample()
        np_ = 2
        vstart = 12 + 52 * np_
        istart = vstart + 24 * 9
        inst = istart + 20

        def patched(at, fmt, *values):
            b = bytearray(blob)
            struct.pack_into(fmt, b, at, *values)
            return bytes(b)
        cases = {
            'DCI1 magic': b'DCI2' + blob[4:],
            'DCI1 counts': patched(4, '<I', 0),
            'DCI1 size': blob[:-1],
            'DCI1 prototype ranges': patched(12 + 52 + 8, '<I', 2),        # second prototype's first vertex
            'DCI1 prototype name': patched(12 + 51, '<B', 65),
            'DCI1 vertex value': patched(vstart + 4, '<f', math.inf),
            'DCI1 index range': patched(istart, '<H', 3),                  # 'post' has three vertices
            'DCI1 instance prototype': patched(inst, '<H', 2),
            'DCI1 instance matrix': patched(inst + 4 + 9 * 4, '<f', math.nan),
        }
        for reason, bad in cases.items():
            with self.subTest(reason=reason):
                err, _, _ = self.load(bad)
                self.assertEqual(err.decode(), reason)
        self.assertEqual(self.load(patched(inst + 4 + 10 * 4, '<f', 2e6))[0].decode(), 'DCI1 instance matrix')
        out = Package()
        self.assertEqual(self.open(None, 0, ctypes.byref(out)).decode(), 'DCI1 arguments')

    def test_fixture_without_a_package_stages_the_synthetic_one(self):
        import tempfile
        import make_instance_fixture as mf
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            report = mf.make(tmp / 'assets', tmp / 'fixture.h')
            header = (tmp / 'fixture.h').read_text()
            blob = (tmp / 'assets/package.dci').read_bytes()
        self.assertFalse(report['original_assets'])
        self.assertIn('#define INSTANCE_SYNTHETIC 1', header)
        self.assertIn('instance_camera={', header)
        self.assertNotRegex(header, r'[{,]\d+f')    # every float literal has a decimal point
        err, out, _ = self.load(blob)
        self.assertIsNone(err)
        self.assertEqual((out.proto_count, out.instance_count), (1, 25))
        self.assertEqual(report['expected_triangles'], 12 * report['expected_drawn_instances'])

    def test_camera_matches_render_world_view(self):
        # render_world_view.c's columns, applied to (point - target), then z + distance, scaled 0.25.
        for yaw, pitch, dist in ((0.0, 0.0, 0.0), (0.7, 0.3, 12.0), (-2.5, -0.9, 40.0)):
            cam = Camera(10.0, -3.0, 5.0, yaw, pitch, dist, 100.0)
            basis = (ctypes.c_float * 10)()
            self.view(ctypes.byref(cam), basis)
            point = (13.0, 1.0, -2.0)
            got = (ctypes.c_float * 3)()
            self.to_view(basis, ctypes.byref(cam), (ctypes.c_float * 3)(*point), got)
            cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
            x, y, z = point[0] - 10.0, point[1] + 3.0, point[2] - 5.0
            want = (0.25 * (cy * x + sy * z),
                    0.25 * (sp * sy * x + cp * y - sp * cy * z),
                    0.25 * (-cp * sy * x + sp * y + cp * cy * z + dist))
            for a, b in zip(got, want):
                self.assertAlmostEqual(a, b, places=4)
        # the target itself sits straight ahead at the boom distance
        cam = Camera(1.0, 2.0, 3.0, 1.1, 0.4, 20.0, 1.0)
        self.view(ctypes.byref(cam), basis)
        self.to_view(basis, ctypes.byref(cam), (ctypes.c_float * 3)(1.0, 2.0, 3.0), got)
        self.assertEqual([round(v, 5) for v in got], [0.0, 0.0, 5.0])


if __name__ == '__main__':
    unittest.main()

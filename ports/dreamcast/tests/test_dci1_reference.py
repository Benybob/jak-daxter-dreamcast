"""Synthetic DCI1 validation, projection and rasterization oracles. (AI-assisted), ISC."""

import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dci1_reference as ref


IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0., 1., 0., 0., 0.)
CAMERA = (0., 0., 0., 0., 0., 0., 100.)


def package(vertices, indices=(0, 1, 2), matrices=(IDENTITY,)):
    """Pack synthetic records independently of the implementation's format code."""
    data = struct.pack('<4sII', b'DCI1', 1, len(matrices))
    data += struct.pack('<5I32s', len(vertices), len(indices), 0, 0, 7, b'test')
    data += b''.join(struct.pack('<5fI', *v[:3], 0., 0., v[3]) for v in vertices)
    data += struct.pack('<' + 'H' * len(indices), *indices)
    data += b'\0' * (-len(data) % 4)
    data += b''.join(struct.pack('<2H12f', 0, 0, *m) for m in matrices)
    return data


def screen_vertex(x, y, z=1., colour=0xFFFF0000):
    # Inverse projection and the zero-yaw/pitch camera's 0.25 scale.
    return ((x - 320) * z * 4 / 390, (260 - y) * z * 4 / 390, z * 4, colour)


def pixel(rgb, x, y):
    i = (y * 640 + x) * 3
    return tuple(rgb[i:i + 3])


class ParserTests(unittest.TestCase):
    def setUp(self):
        self.vertices = [(0., 0., 4., 0xFF123456), (1., 0., 4., 0xFFABCDEF),
                         (0., 1., 4., 0xFFFFFFFF)]
        self.data = package(self.vertices)

    def test_parse_round_trip_odd_index_padding(self):
        parsed = ref.parse(self.data)
        self.assertEqual(parsed['prototypes'][0], dict(vertex_count=3, index_count=3,
                         first_vertex=0, first_index=0, material=7, name='test'))
        self.assertEqual(parsed['vertices'][0], (0., 0., 4., 0., 0., 0xFF123456))
        self.assertEqual(parsed['indices'], [0, 1, 2])
        self.assertEqual(parsed['instances'], [(0, IDENTITY)])
        self.assertEqual(len(self.data), 12 + 52 + 72 + 8 + 52)
        self.assertEqual(self.data[142:144], b'\0\0')

    def test_multiple_prototypes_contiguous_offsets(self):
        header = struct.pack('<4sII', b'DCI1', 2, 0)
        table = (struct.pack('<5I32s', 3, 3, 0, 0, 0, b'one')
                 + struct.pack('<5I32s', 3, 3, 3, 3, 0, b'two'))
        vertices = b''.join(struct.pack('<5fI', *v[:3], 0., 0., v[3])
                            for v in self.vertices * 2)
        parsed = ref.parse(header + table + vertices + struct.pack('<6H', 0, 1, 2, 2, 1, 0))
        self.assertEqual([p['first_vertex'] for p in parsed['prototypes']], [0, 3])
        self.assertEqual(parsed['indices'], [0, 1, 2, 2, 1, 0])
        self.assertEqual(ref.parse(struct.pack('<4sII', b'DCI1', 0, 0))['instances'], [])

    def test_distinct_validation_reasons(self):
        cases = []

        def changed(offset, fmt, value):
            result = bytearray(self.data)
            struct.pack_into(fmt, result, offset, value)
            return result

        cases += [(b'DCI', 'truncated header'),
                  (b'NOPE' + self.data[4:], 'invalid magic'),
                  (self.data[:50], 'truncated prototype table'),
                  (changed(20, '<I', 1), 'noncontiguous first_vertex'),
                  (changed(24, '<I', 1), 'noncontiguous first_index'),
                  (changed(16, '<I', 2), 'index_count is not divisible by three'),
                  (changed(32, '<B', 255), 'non-ASCII prototype name'),
                  (self.data[:-1], 'incorrect total size'),
                  (self.data + b'\0', 'incorrect total size'),
                  (changed(64, '<f', math.nan), 'nonfinite vertex float'),
                  (changed(76, '<f', math.inf), 'nonfinite vertex float'),
                  (changed(136, '<H', 3), 'index outside prototype'),
                  (changed(142, '<B', 1), 'nonzero index padding'),
                  (changed(144, '<H', 1), 'instance prototype out of range'),
                  (changed(148, '<f', -math.inf), 'nonfinite instance float')]
        for data, reason in cases:
            with self.subTest(reason=reason):
                with self.assertRaisesRegex(ValueError, '^' + reason + '$'):
                    ref.parse(data)


class RasterTests(unittest.TestCase):
    def test_camera(self):
        camera = (7., 2., -3., 0.7, -0.2, 8., 100.)
        self.assertEqual(ref.view_point(camera[:3], camera), (0., 0., 2.))
        view = ref.view_point((7., 3., -3.), camera)
        self.assertLess(ref.project(view + (255, 0, 0))[1], 240)
        self.assertAlmostEqual(ref.view_point((1, 0, 0), (0, 0, 0, math.pi / 2, 0, 8, 9))[2], 1.75)

    def test_exact_flat_triangle_and_double_sided(self):
        # Binary-exact world coordinates: z=97.5 gives screen scale exactly one.
        vertices = [(0., 0., 390., 0xFFEA3255), (10., 0., 390., 0xFFEA3255),
                    (0., 10., 390., 0xFFEA3255)]
        data = package(vertices)
        _, _, rgb, stats = ref.render(data, CAMERA)
        # The 10 pixel centres on the descending right edge are excluded by
        # top-left ownership: rows contain 9 + 8 + ... + 1 = 45 samples.
        self.assertEqual(stats['pixels_covered'], 45)
        self.assertLessEqual(abs(stats['pixels_covered'] - 50), 10)
        self.assertEqual(sum(pixel(rgb, x, y) == (234, 50, 85)
                             for y in range(250, 260) for x in range(320, 330)), 45)
        reverse = ref.render(package(vertices, (2, 1, 0)), CAMERA)
        self.assertEqual(rgb, reverse[2])
        self.assertEqual(stats, dict(instances_visited=1, instances_drawn=1,
                         triangles_visited=1, triangles_near_clipped=0,
                         triangles_rasterized=1, pixels_covered=45))

    def test_shared_edge_top_left_no_gaps_or_double_coverage(self):
        vertices = [(0., 0., 390., 0xFFFFFFFF), (10., 0., 390., 0xFFFFFFFF),
                    (10., 10., 390., 0xFFFFFFFF), (0., 10., 390., 0xFFFFFFFF)]
        _, _, rgb, stats = ref.render(package(vertices, (0, 1, 2, 0, 2, 3)), CAMERA)
        self.assertEqual(stats['pixels_covered'], 100)
        self.assertTrue(all(pixel(rgb, x, y) == (255, 255, 255)
                            for y in range(250, 260) for x in range(320, 330)))

    def test_depth_order_and_ties(self):
        vertices = [screen_vertex(x, y, z, colour)
                    for z, colour in ((2., 0xFFFF0000), (1., 0xFF0000FF))
                    for x, y in ((310, 250), (330, 250), (320, 270))]
        for indices in ((0, 1, 2, 3, 4, 5), (3, 4, 5, 0, 1, 2)):
            rgb = ref.render(package(vertices, indices), CAMERA)[2]
            self.assertEqual(pixel(rgb, 320, 255), (0, 0, 255))
        for first, second in ((0xFFFF0000, 0xFF0000FF), (0xFF0000FF, 0xFFFF0000)):
            vertices = [screen_vertex(x, y, 1., colour) for colour in (first, second)
                        for x, y in ((310, 250), (330, 250), (320, 270))]
            rgb = ref.render(package(vertices, (0, 1, 2, 3, 4, 5)), CAMERA)[2]
            self.assertEqual(pixel(rgb, 320, 255), ((first >> 16) & 255, 0, first & 255))

    def test_clip_vertex_counts_and_colours(self):
        inside = (0., 0., 0.1, 255., 0., 0.)
        outside = (1., 0., 0., 0., 0., 255.)
        third = (0., 1., 0., 0., 0., 255.)
        clipped = ref.clip_near([inside, outside, third])
        self.assertEqual(len(clipped), 3)
        cuts = [v for v in clipped if v[2] == ref.NEAR]
        self.assertEqual(len(cuts), 2)
        self.assertTrue(all(v[3:] == (127.5, 0., 127.5) for v in cuts))
        self.assertEqual(len(ref.clip_near([inside, (1., 0., 0.1, 0, 255, 0), third])), 4)
        self.assertEqual(ref.clip_near([outside, third, outside]), [])

    def test_near_clipped_render_and_behind_camera(self):
        vertices = [(-0.01, 0., 0.4, 0xFFFFFFFF), (0.01, 0., 0.4, 0xFFFFFFFF),
                    (0., 0.01, 0., 0xFFFFFFFF)]
        stats = ref.render(package(vertices), CAMERA)[3]
        self.assertEqual(stats['triangles_near_clipped'], 1)
        self.assertEqual(stats['triangles_rasterized'], 2)
        self.assertGreater(stats['pixels_covered'], 0)
        vertices = [(x, y, -1., c) for x, y, _, c in vertices]
        _, _, rgb, stats = ref.render(package(vertices), CAMERA)
        self.assertEqual(stats['pixels_covered'], 0)
        self.assertEqual(rgb, bytearray(bytes((115, 158, 204)) * (640 * 480)))

    def test_draw_distance_float32_boundary(self):
        camera = CAMERA[:-1] + (5.,)
        m = IDENTITY[:9] + (3., 0., 4.)
        self.assertTrue(ref.within_draw_distance(m, camera))
        self.assertEqual(ref.render(package([], (), (m,)), camera)[3]['instances_drawn'], 1)
        self.assertFalse(ref.within_draw_distance(IDENTITY[:9] + (3.001, 0., 4.), camera))
        # This double-precision distance is outside; binary32 rounds it onto the boundary.
        m = IDENTITY[:9] + (3. + 1e-8, 0., 4.)
        self.assertGreater(m[9] ** 2 + m[11] ** 2, 25.)
        self.assertTrue(ref.within_draw_distance(m, camera))

    def test_gouraud_gradient_and_perspective(self):
        vertices = [screen_vertex(300, 250, 1., 0xFFFF0000),
                    screen_vertex(340, 250, 2., 0xFF0000FF),
                    screen_vertex(300, 290, 1., 0xFFFF0000)]
        rgb = ref.render(package(vertices), CAMERA)[2]
        colours = [pixel(rgb, x, 251) for x in range(302, 337)]
        self.assertTrue(all(a[0] >= b[0] and a[2] <= b[2]
                            for a, b in zip(colours, colours[1:])))
        # Screen barycentric weight of the blue vertex at x=320.5 is .5125;
        # reciprocal depths 1 and .5 make blue ~88, rather than affine ~131.
        blue_weight = 20.5 / 40
        expected = round(255 * (blue_weight / 2) / (1 - blue_weight / 2))
        self.assertLessEqual(abs(pixel(rgb, 320, 251)[2] - expected), 1)

    def test_instance_transform_and_offscreen_or_degenerate(self):
        vertices = [(0., 0., 390., 0xFFFFFFFF), (10., 0., 390., 0xFFFFFFFF),
                    (0., 10., 390., 0xFFFFFFFF)]
        shifted = IDENTITY[:9] + (10., 0., 0.)
        rgb = ref.render(package(vertices, matrices=(shifted,)), CAMERA)[2]
        self.assertEqual(pixel(rgb, 330, 259), (255, 255, 255))
        m = IDENTITY[:9] + (10000., 0., 0.)
        camera = CAMERA[:-1] + (20000.,)
        self.assertEqual(ref.render(package(vertices, matrices=(m,)), camera)[3]['triangles_rasterized'], 0)
        self.assertEqual(ref.render(package(vertices, (0, 0, 0)), CAMERA)[3]['pixels_covered'], 0)


class PpmAndCliTests(unittest.TestCase):
    def test_ppm_round_trip_comments_and_binary_whitespace(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frame.ppm'
            rgb = bytearray([10, 32, 35, 0, 255, 13])
            ref.write_ppm(path, 2, 1, rgb)
            self.assertEqual(ref.read_ppm(path), (2, 1, rgb))
            path.write_bytes(b'P6\t# first\n 2\v# second\n1\f255\r\n' + rgb)
            self.assertEqual(ref.read_ppm(path), (2, 1, rgb))
            path.write_bytes(b'P6\n2 1\n255\n \t# after maxval\n\r\n' + rgb)
            self.assertEqual(ref.read_ppm(path), (2, 1, rgb))
            for header in (b'P3\n2 1\n255\n', b'P6\n2 1\n256\n', b'P6\n-2 1\n255\n', b'P6'):
                path.write_bytes(header + rgb)
                with self.assertRaises(ValueError):
                    ref.read_ppm(path)

    def test_ppm_rejects_truncated_or_extra_raster(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frame.ppm'
            for data in (b'P6\n1 1\n255\n\xff\0',
                         b'P6\n1 1\n255\n\xff\0\0\0'):
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError, 'incorrect PPM pixel count'):
                    ref.read_ppm(path)

    def test_render_cli_synthetic_package(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            data = package([(0., 0., 390., 0xFFFFFFFF), (10., 0., 390., 0xFFFFFFFF),
                            (0., 10., 390., 0xFFFFFFFF)])
            (path / 'synthetic.dci').write_bytes(data)
            command = [sys.executable, ref.__file__, str(path / 'synthetic.dci'),
                       '--camera', '0,0,0,0,0,0,100', '--out', str(path / 'out.ppm'),
                       '--json', str(path / 'stats.json')]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(ref.read_ppm(path / 'out.ppm')[:2], (640, 480))
            self.assertEqual(json.loads((path / 'stats.json').read_text())['pixels_covered'], 45)


if __name__ == '__main__':
    unittest.main()

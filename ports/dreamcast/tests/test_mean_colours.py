"""Optional prototype mean RGB and unchanged donor output (AI-assisted), ISC.

Synthetic data only; package hashes were recorded before the mean-colour change.
"""
from contextlib import ExitStack, redirect_stderr, redirect_stdout
import hashlib
from io import StringIO
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prototype_geometry as pg  # noqa: E402
import tie_lods as tl  # noqa: E402


def triangle(x=0.0, colours=None, material=2):
    if colours is None:
        colours = ((10 / 255, 20 / 255, 30 / 255, 0.25),
                   (40 / 255, 50 / 255, 60 / 255, 0.5),
                   (70 / 255, 80 / 255, 90 / 255, 0.75))
    return (((x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 1.0, 0.0)),
            ((0.0, 0.0), (1.0, 0.0), (0.0, 1.0)), colours, material)


def fixture(count=10):
    origin = [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]
    instances = [{'proto': 0, 'origin': origin, 'x': float(10 * i), 'y': 0.0, 'z': 0.0, 'r': 2.0}
                 for i in range(count)]
    protos = [{'name': 'synthetic.mb', 'kind': 'tie', 'lods': [{'tris': 1}]}]
    source = [triangle(inst['x'], tuple(tuple(c[k] + (i / 255 if k < 3 else 0.0) for k in range(4))
                                       for c in triangle()[2])) for i, inst in enumerate(instances)]
    lower = ((0.05, 0.0, 0.0), (0.95, 0.0, 0.0), (0.0, 0.9, 0.0))
    uvs = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75))
    lods = {'synthetic.mb': [([triangle()[0]], [triangle()[1]]), ([lower], [uvs]), ([lower], [uvs]), None]}
    return protos, instances, source, lods


def synthetic_build(count=10, **kwargs):
    protos, instances, source, _ = fixture(count)
    with ExitStack() as stack:
        for target, value in (('level_contents.objects', [('level', None, 'level')]),
                              ('level_contents.object_bytes', b''),
                              ('instanced_level.model', (protos, instances)),
                              ('rs.read_glb', ({}, b'')),
                              ('world_triangles', {'synthetic.mb': source})):
            stack.enter_context(patch('prototype_geometry.' + target, return_value=value))
        return pg.build('synthetic.dgo', 'synthetic.glb', **kwargs)


def packages(results, instances):
    _, _, _, lods = fixture()
    planes = {'synthetic.mb': (10.0, 20.0, 30.0)}
    factors = {2: (2.0, 1.0, 0.5, 2.0)}
    return [pg.package(results, instances)[0]] + [
        tl.build_textured(results, instances, lods, planes, factors, compact)[0] for compact in (False, True)]


class MeanColoursTest(unittest.TestCase):
    def test_default_and_explicit_off_match_pre_change_package_bytes(self):
        # SHA-256 of the complete packages produced by the unmodified builders.
        expected = ['fa88f2eae5033eca4a17beabdb91219cd4d45ba04621dc4cea0b809edafdb42f',
                    '19e1bd335d2280c94acb033e6f0bd08bb55974cd7cf8b91fdf67a17a6d4f897c',
                    '9b0086252ea2a2b5b0f4e93c9a7bb22366ceccc2029e020339171c6c5fde57a5']
        for kwargs in ({}, {'mean_colours': False}):
            results, instances, _ = synthetic_build(**kwargs)
            blobs = packages(results, instances)
            self.assertEqual([hashlib.sha256(b).hexdigest() for b in blobs], expected)
            self.assertEqual(results[0]['geometry'], [triangle()])

    def test_build_averages_every_instance_including_after_eight(self):
        results, instances, _ = synthetic_build(mean_colours=True)
        self.assertEqual(len(instances), 10)
        pts, uvs, cols, mat = results[0]['geometry'][0]
        self.assertEqual((pts, uvs, mat), (triangle()[0], triangle()[1], triangle()[3]))
        for donor, mean in zip(triangle()[2], cols):
            for k in range(3):
                self.assertAlmostEqual(mean[k] * 255, donor[k] * 255 + 4.5)
            self.assertEqual(mean[3], donor[3])

    def test_matches_positions_across_grid_boundary_and_reordered_triangles(self):
        a = triangle(0.0039999)
        b = triangle(5.0)
        colours = ((0.2, 0.4, 0.6, 0.9), (0.8, 0.6, 0.4, 0.1), (0.1, 0.3, 0.5, 0.2))
        other_a = triangle(0.0040001, colours, 19)
        other_b = triangle(5.0005, colours, 23)
        other = [other_b, triangle(20.0), other_a]
        self.assertEqual(pg.shared([a, b], other), [a, b])
        mean = pg.mean_vertex_colours([a, b], [[a, b], other])
        for donor, averaged in zip([a, b], mean):
            self.assertEqual((averaged[0], averaged[1], averaged[3]), (donor[0], donor[1], donor[3]))
            for corner in range(3):
                for k in range(3):
                    self.assertAlmostEqual(averaged[2][corner][k], (donor[2][corner][k] + colours[corner][k]) / 2)
                self.assertEqual(averaged[2][corner][3], donor[2][corner][3])

    def test_missing_triangles_are_not_counted_and_unmatched_keep_donor(self):
        a, b, missing = triangle(), triangle(5.0), triangle(10.0)
        bright = ((1.0, 0.5, 0.25, 1.0),) * 3
        # Empty cuts and a triangle outside the 2 mm tolerance add no samples.
        cuts = [[a, b], [triangle(0.001, bright)], [], [triangle(0.0021, bright)]]
        mean = pg.mean_vertex_colours([a, b, missing], cuts)
        self.assertEqual(mean[1:], [b, missing])
        for c, donor in zip(mean[0][2], a[2]):
            for k in range(3):
                self.assertAlmostEqual(c[k], (donor[k] + bright[0][k]) / 2)
            self.assertEqual(c[3], donor[3])

    def test_duplicate_triangles_match_only_once_per_instance(self):
        a = triangle()
        bright = triangle(colours=((1.0, 1.0, 1.0, 1.0),) * 3)
        self.assertEqual(pg.shared([a, a], [bright]), [a])
        mean = pg.mean_vertex_colours([a, a], [[a, a], [bright]])
        self.assertEqual(mean[1], a)
        self.assertAlmostEqual(mean[0][2][0][0], (a[2][0][0] + 1.0) / 2)

    def test_single_instance_keeps_its_colours(self):
        results, _, _ = synthetic_build(count=1, mean_colours=True)
        self.assertEqual(results[0]['status'], 'ok (one instance only)')
        self.assertEqual(results[0]['geometry'], [triangle()])

    def test_rounding_material_factor_alpha_and_nearest_lower_lod(self):
        # Exact binary fractions make .5 ties unambiguous; packing uses Python round.
        donor = ((0.0, 0.125, 0.25, 0.125), (0.25, 0.5, 0.75, 0.25), (0.5, 0.75, 1.0, 0.5))
        other = tuple((c[0] + 0.125, c[1] - 0.125, c[2] - 0.125, 1.0) for c in donor)
        base = [triangle(colours=donor)]
        averaged = pg.mean_vertex_colours(base, [base, [triangle(colours=other)]])
        _, instances, _, lods = fixture(2)
        results = [{'name': 'synthetic.mb', 'geometry': averaged}]
        planes = {'synthetic.mb': (10.0, 20.0, 30.0)}
        # RGB is averaged before the existing material multiply/clamp; alpha is donor x2.
        factors = {2: (2.0, 1.0, 0.5, 2.0)}
        expected = [0x40201018, 0x809f7058, 0xffffaf78]
        for compact in (False, True):
            blob, _ = tl.build_textured(results, instances, lods, planes, factors, compact)
            geoms, table, _, _, _ = tl.read_dci2(blob)
            verts = tl.float_vertices(blob)
            self.assertEqual(table[0][:4], (0, 1, 1, tl.NONE))
            for g in geoms:
                colours = [struct.unpack_from('<5fI', verts, 24 * (g[2] + k))[5] for k in range(g[0])]
                self.assertEqual(colours, expected)
            self.assertEqual(struct.unpack_from('<5fI', verts, 24 * geoms[1][2])[3:5], (0.25, 0.25))
        lower = tl.lower_lod(*lods['synthetic.mb'][1], averaged)
        self.assertEqual(lower[0][2], list(averaged[0][2]))

    def test_cli_requires_textured(self):
        args = ['tie_lods.py', 'synthetic.dci1', 'synthetic.dgo', 'dump', '--mean-colours']
        with patch.object(sys, 'argv', args), redirect_stderr(StringIO()) as err:
            with self.assertRaises(SystemExit) as raised:
                tl.main()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn('--mean-colours requires --textured', err.getvalue())

    def test_cli_forwards_optional_flag(self):
        results, instances, _ = synthetic_build()
        for flags, enabled in (([], False), (['--mean-colours'], True)):
            args = ['tie_lods.py', '--textured', 'synthetic.glb', 'synthetic.dgo', 'dump'] + flags
            with ExitStack() as stack:
                stack.enter_context(patch.object(sys, 'argv', args))
                stack.enter_context(patch.object(tl.instance_budget, 'bsp_lod_table', return_value=(None, {})))
                build = stack.enter_context(patch.object(pg, 'build', return_value=(results, instances, [])))
                stack.enter_context(patch.object(pg.rs, 'read_glb', return_value=({}, b'')))
                stack.enter_context(patch.object(tl, 'dump_lods', return_value={}))
                stack.enter_context(redirect_stdout(StringIO()))
                tl.main()
            build.assert_called_once_with(Path('synthetic.dgo'), Path('synthetic.glb'), mean_colours=enabled)


if __name__ == '__main__':
    unittest.main()

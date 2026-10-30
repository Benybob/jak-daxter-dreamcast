"""Synthetic fixed draw-window capacity measurements. (AI-assisted), ISC."""

import json
import math
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


PORT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(PORT), str(PORT / 'tests')]
import indexed_sectors as indexed
import prepare_render_sectors as drm
import window_caps as caps
from test_render_sectors import _one, _package


class WindowCapsTest(unittest.TestCase):
    def setUp(self):
        # Keep all synthetic files inside the repository, including CLI output.
        self.temp = TemporaryDirectory(dir=PORT)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def packet(self, positions, name='packet'):
        package = _package(_one(positions, normals=[(0, 1, 0)] * len(positions)).to_bytes(),
                           cell_size=16)
        root = self.root / name
        drm.publish_package(root, package)
        indexed.write_draw_files(root, remove_sectors=True)
        self.assertEqual(list(root.rglob('*.drs')), [])
        return root

    def replicated(self):
        # Crossing face ABC, cell-zero face ADC, and cell-one face BEF.
        return self.packet([(14, 0, 1), (18, 0, 1), (14, 0, 3),
                            (14, 0, 1), (15, 0, 1), (14, 0, 3),
                            (18, 0, 1), (19, 0, 1), (18, 0, 3)])

    def test_single_local_block_shares_vertices(self):
        root = self.packet([(1, 0, 1), (3, 0, 1), (3, 0, 3),
                            (1, 0, 1), (3, 0, 3), (1, 0, 3)])
        self.assertEqual(caps.window_needs(root, 2, 2, 2),
                         dict(faces=2, vertices=4, remap=4, budget=160))
        # Selection is corner bounds overlap, including empty triangle space.
        self.assertEqual(caps.window_needs(root, 1, 3, 0)['faces'], 2)

    def test_replica_uses_vertices_from_first_touched_block(self):
        root = self.replicated()
        # Block zero has ABCD (4), block one ABCEF (5). ABC is emitted
        # from block zero, sharing A/C with ADC: 4 + 3 selected vertices.
        self.assertEqual(caps.window_needs(root, 16, 2, 4),
                         dict(faces=3, vertices=7, remap=9, budget=328))
        # When only cell one is touched its copy of ABC supplies the face.
        self.assertEqual(caps.window_needs(root, 17, 2, 0),
                         dict(faces=1, vertices=3, remap=5, budget=184))

    def test_closed_cell_and_face_boundaries(self):
        root = self.replicated()
        # The point at x=16 touches both cell rectangles; only ABC's bounds hit.
        self.assertEqual(caps.window_needs(root, 16, 2, 0),
                         dict(faces=1, vertices=3, remap=9, budget=328))
        root = self.packet([(1, 0, 1), (3, 0, 1), (1, 0, 3)], 'edge')
        for x, z in ((1, 1), (3, 3), (1, 3), (3, 1)):
            with self.subTest(x=x, z=z):
                self.assertEqual(caps.window_needs(root, x, z, 0)['faces'], 1)
        self.assertEqual(caps.window_needs(root, math.nextafter(3, math.inf), 2, 0)['faces'], 0)
        self.assertEqual(caps.window_needs(root, 2, math.nextafter(1, -math.inf), 0)['faces'], 0)

    def test_float32_positions_compared_to_double_query(self):
        root = self.packet([(0.1, 0, 1), (0.1, 1, 2), (0.1, 0, 3)])
        self.assertEqual(caps.window_needs(root, 0.1, 2, 0)['faces'], 0)
        self.assertEqual(caps.window_needs(root, drm.f32(0.1), 2, 0)['faces'], 1)

    def test_empty_area_still_counts_nonempty_global(self):
        root = self.packet([(1, 0, 1), (3, 100, 1), (1, 0, 3),
                            (33, 0, 1), (35, 0, 1), (33, 0, 3)])
        self.assertEqual(caps.window_needs(root, 1000, 1000, 1),
                         dict(faces=0, vertices=0, remap=3, budget=104))
        self.assertEqual(caps.window_needs(root, 2, 2, 1),
                         dict(faces=1, vertices=3, remap=3, budget=104))

    def test_global_is_the_only_block(self):
        root = self.packet([(1, 0, 1), (3, 100, 1), (1, 0, 3)])
        loaded = caps.load_package(root)
        self.assertEqual(loaded.locals, [])
        self.assertEqual(caps.window_needs(loaded, 2, 2, 1),
                         dict(faces=1, vertices=3, remap=3, budget=104))

    def test_cell_index_handles_negative_and_large_queries(self):
        positions = []
        for ix in range(-25, 25):
            x = ix * 16 + 1
            positions.extend([(x, 0, 1), (x + 1, 0, 1), (x, 0, 2)])
        loaded = caps.load_package(self.packet(positions))
        # The spatial lookup must include the negative cell at the boundary.
        self.assertEqual(caps.window_needs(loaded, 0, 1, 0),
                         dict(faces=0, vertices=0, remap=6, budget=224))
        self.assertEqual(caps.window_needs(loaded, -15, 1, 0),
                         dict(faces=1, vertices=3, remap=3, budget=120))
        self.assertEqual(caps.window_needs(loaded, 0, 0, 10000),
                         dict(faces=50, vertices=150, remap=150, budget=5216))

    def test_loaded_package_reuses_parsed_blocks_and_bounds(self):
        root = self.replicated()
        with patch.object(indexed, 'decode_draw_sector', wraps=indexed.decode_draw_sector) as decode:
            loaded = caps.load_package(root)
            self.assertEqual(decode.call_count, 3)
            with patch.object(Path, 'read_bytes', side_effect=AssertionError('unexpected re-read')):
                self.assertEqual(caps.window_needs(loaded, 16, 2, 4)['vertices'], 7)
                self.assertEqual(caps.window_needs(loaded, 17, 2, 0)['vertices'], 3)
                caps.sweep(loaded, 14)
            self.assertEqual(decode.call_count, 3)

    def test_rounding_and_buffer_costs(self):
        triangle = [(1, 0, 1), (3, 0, 1), (1, 0, 3)]
        for count, face_cap in ((1, 64), (64, 64), (65, 128)):
            with self.subTest(count=count):
                root = self.packet(triangle * count, str(count))
                report = caps.sweep(root, 14)
                self.assertEqual(report['centres'], 1)
                self.assertEqual(report['maxima']['faces'], dict(value=count, centre=[1, 1]))
                self.assertEqual(report['suggested'], dict(
                    face_cap=face_cap, vertex_cap=64, remap_cap=64,
                    bytes=dict(faces=face_cap * 14, vertices=2304, remap=128,
                               total=face_cap * 14 + 2432)))

    def test_grid_centre_count_and_independent_maxima(self):
        root = self.packet([(1, 0, 1), (2, 0, 1), (1, 0, 2),
                            (17, 0, 1), (18, 0, 1), (17, 0, 2)])
        report = caps.sweep(root, 0, step=5)
        self.assertEqual(report['centres'], 4)  # x = 1, 6, 11, 16; z = 1
        self.assertEqual(report['maxima']['faces'], dict(value=1, centre=[1, 1]))
        self.assertEqual(report['maxima']['vertices'], dict(value=3, centre=[1, 1]))
        self.assertEqual(report['maxima']['remap'], dict(value=6, centre=[16, 1]))
        self.assertEqual(report['maxima']['budget'], dict(value=224, centre=[16, 1]))
        self.assertEqual(caps.sweep(root, 0, step=1)['centres'], 36)

    def test_cli_json_stdout_and_file(self):
        root = self.replicated()
        output = self.root / 'caps.json'
        result = subprocess.run([sys.executable, '-B', str(PORT / 'window_caps.py'),
                                 str(root), '--half', '14', '--step', '2', '--json', str(output)],
                                cwd=PORT, check=True, capture_output=True, text=True)
        self.assertEqual(result.stderr, '')
        self.assertEqual(json.loads(result.stdout), caps.sweep(root, 14, 2))
        self.assertEqual(json.loads(output.read_text(encoding='utf-8')), json.loads(result.stdout))
        default = subprocess.run([sys.executable, '-B', str(PORT / 'window_caps.py'),
                                  str(root), '--half', '14'],
                                 cwd=PORT, check=True, capture_output=True, text=True)
        self.assertEqual(json.loads(default.stdout), caps.sweep(root, 14))

    def test_a_sweep_is_a_sample_not_a_bound(self):
        near = [(.1, 0, .1), (.9, 0, .1), (.1, 0, .9)] * 64
        far = [(x + 28, y, z) for x, y, z in near]
        root = self.packet(near + far, 'between')
        self.assertEqual(caps.sweep(root, 14, 8)['maxima']['faces']['value'], 64)
        self.assertEqual(caps.window_needs(root, 14.5, .5, 14)['faces'], 128)

    def test_invalid_query_and_step(self):
        root = self.replicated()
        loaded = caps.load_package(root)
        for x, z, half in ((math.nan, 0, 1), (0, math.inf, 1), (0, 0, -1), (0, 0, math.inf)):
            with self.subTest(x=x, z=z, half=half), self.assertRaises(ValueError):
                caps.window_needs(loaded, x, z, half)
        for step in (0, -1, math.nan, math.inf):
            with self.subTest(step=step), self.assertRaises(ValueError):
                caps.sweep(loaded, 14, step)


if __name__ == '__main__':
    unittest.main()

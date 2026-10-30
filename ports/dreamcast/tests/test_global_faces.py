"""Synthetic global-face diagnostics and midpoint split tests. (AI-assisted), ISC."""

from contextlib import redirect_stdout
from io import StringIO
import json
import math
from pathlib import Path
import random
import struct
import sys
import unittest
from unittest.mock import mock_open, patch


PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import global_faces as gf
import indexed_sectors as indexed
import prepare_render_sectors as drm


def vertex(x, y, z, u=0, v=0, color=0xFFFFFFFF):
    return (float(x), float(y), float(z), float(u), float(v), 0.0, 1.0, 0.0, color)


def scene(positions, material=0, translation=None):
    """An untextured, nonindexed GLB scene, entirely synthetic and in memory."""
    binary = b''.join(struct.pack('<3f', *p) for p in positions)
    doc = dict(asset=dict(version='2.0'), buffers=[dict(byteLength=len(binary))],
               bufferViews=[dict(buffer=0, byteLength=len(binary))],
               accessors=[dict(bufferView=0, componentType=5126, type='VEC3', count=len(positions))],
               materials=[dict(name=f'material-{i}') for i in range(material + 1)],
               meshes=[dict(primitives=[dict(attributes=dict(POSITION=0), material=material)])],
               nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0)
    if translation is not None:
        doc['nodes'][0]['translation'] = translation
    return doc, binary, drm.extract_scene(doc, binary, drm.DEFAULT_TEXTURE_SIZE)


def projected_area(vertices):
    a, b, c = vertices
    return ((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) / 2


class GlobalFacesTest(unittest.TestCase):
    def test_reasons_match_packer_and_span_precedes_replicas(self):
        examples = [((1, 0, 1), (2, 0, 1), (1, 0, 2)),
                    ((1, 0, 1), (2, 65, 1), (1, 0, 2)),
                    ((1, 0, 1), (61, 0, 1), (1, 0, 61)),
                    ((1, 0, 1), (81, 0, 1), (1, 0, 81)),
                    ((1, 0, 1), (1001, 0, 1), (1, 0, 1001))]
        expected = [[], ['axis_span'], ['replica_limit'],
                    ['axis_span', 'replica_limit'], ['axis_span', 'candidate_axis_guard']]
        for positions, reasons in zip(examples, expected):
            with self.subTest(positions=positions):
                vertices = tuple(vertex(*p) for p in positions)
                info = gf.inspect_face(vertices, 4)
                self.assertEqual(info['reasons'], reasons)
                axes = tuple(tuple(v[i] for v in vertices) for i in range(3))
                self.assertEqual(drm.classify_triangle(*axes, 4),
                                 None if reasons else info['cells'])

    def test_exact_span_boundary_and_negative_grid_corner(self):
        vertices = (vertex(-16, 0, -16), vertex(-16, 64, -16), vertex(-16, 0, -16))
        info = gf.inspect_face(vertices, 16)
        self.assertEqual(info['reasons'], [])
        self.assertEqual(set(info['cells']), {(-2, -2), (-2, -1), (-1, -2), (-1, -1)})

    def test_empty_overlap_has_explicit_cause(self):
        with patch.object(gf, 'overlapping_cells', return_value=[]):
            self.assertEqual(gf.inspect_face((vertex(1, 0, 1),) * 3, 16)['reasons'],
                             ['no_overlapping_cells'])

    def test_prepared_cell_sat_matches_packer_oracle(self):
        rng = random.Random(1046)
        triangles = [([rng.uniform(-80, 80) for _ in range(3)],
                      [rng.uniform(-80, 80) for _ in range(3)]) for _ in range(40)]
        for edge in (-64, -16, 0, 16, 64, 999984):
            for epsilon in (0, 1e-7, -1e-7, 1e-4, -1e-4):
                triangles.append(([edge + epsilon] * 3, [edge, edge + 1, edge + 2]))
        triangles.extend([([0, 0, 0], [0, 0, 0]),
                          ([0, 1e-13, 2e-13], [0, 0, 0]),
                          ([0, 2000, 0], [0, 0, 2000])])
        for cell in (4, 16, 64):
            for xs, zs in triangles:
                with self.subTest(cell=cell, xs=xs, zs=zs):
                    self.assertEqual(gf.overlapping_cells(xs, zs, cell),
                                     drm.overlapping_cells(xs, zs, cell))

    def test_midpoint_interpolates_position_uv_color_and_normal(self):
        a = vertex(1, 2, 3, 0, 2, 0x00020406)
        b = vertex(5, 8, 11, 2, 4, 0xFF050709)
        b = b[:5] + (1.0, 0.0, 0.0, b[8])
        m = gf.midpoint(a, b)
        self.assertEqual(m[:5], (3, 5, 7, 1, 3))
        self.assertEqual(m[8], 0x80040608)
        self.assertAlmostEqual(sum(n * n for n in m[5:8]), 1, places=6)
        self.assertEqual(m[5], m[6])
        self.assertEqual(struct.unpack('<5f', struct.pack('<5f', *m[:5])), m[:5])

    def test_split_longest_xz_edge_preserves_winding_and_area(self):
        # Large Y difference must not choose the short XZ edge AB.
        vertices = (vertex(0, 0, 0), vertex(1, 100, 0), vertex(0, 0, 8))
        children = gf.bisect(vertices)
        self.assertEqual(children[0][1][:3], (0.5, 50, 4))
        self.assertEqual(sum(projected_area(c) for c in children), projected_area(vertices))
        self.assertTrue(all(projected_area(c) > 0 for c in children))

    def test_recursive_split_count_threshold_material_and_area(self):
        original = (vertex(1, 0, 1), vertex(41, 0, 1), vertex(1, 0, 41))
        children = list(gf.subdivide([(7, original)], 4, 4))
        self.assertGreater(len(children), 2)
        self.assertEqual(sum(projected_area(c[1]) for c in children), projected_area(original))
        for material, vertices, info, stalled in children:
            self.assertEqual(material, 7)
            self.assertFalse(stalled)
            self.assertLessEqual(len(info['cells']), 4)
            drm.pack_face(0, material, vertices)

    def test_y_span_global_survives_cell_only_split(self):
        _, _, state = scene([(1, 0, 1), (2, 100, 1), (1, 0, 2)])
        report = gf.measure(state, 16, 4)
        self.assertEqual((report['faces'], report['global_faces'], report['added_faces']), (1, 1, 0))
        self.assertEqual(report['primary_global_reasons'], {'axis_span': 1})
        self.assertTrue(report['packer_link_validated'])

    def test_guard_is_unknown_in_histogram(self):
        _, _, state = scene([(1, 0, 1), (1001, 0, 1), (1, 0, 1001)])
        report = gf.measure(state, 4)
        self.assertEqual(report['cells_touched_histogram'], {'unknown_guard': 1})
        self.assertEqual(report['global_reason_flags'], {'axis_span': 1, 'candidate_axis_guard': 1})

    def test_drx2_and_drm2_byte_counts_equal_real_codecs(self):
        doc, binary, state = scene([(14, 0, 1), (18, 0, 1), (14, 0, 3),
                                    (14, 0, 1), (15, 0, 1), (14, 0, 3)])
        report = gf.measure(state, 16)
        package = drm.build_package(doc, binary, 16)
        blobs = [package.global_sector, *package.sectors.values()]
        actual = sum(len(indexed.encode_draw_sector(drm.parse_sector(b)['records'])) for b in blobs)
        self.assertEqual(report['drx2_bytes'], actual)
        self.assertEqual(report['drm2_package_bytes'], package.summary['package_bytes'])
        self.assertTrue(report['package_caps_pass'])
        self.assertTrue(report['packer_link_validated'])
        self.assertFalse(report['window_unique_cap_checked'])

    def test_drx2_sharing_respects_uv_and_colour_seams(self):
        _, _, state = scene([(1, 0, 1), (2, 0, 1), (1, 0, 2)])
        vertices = tuple(vertex(*p) for p in [(1, 0, 1), (2, 0, 1), (1, 0, 2)])
        state['faces'] = [(0, vertices), (0, vertices)]
        shared = gf.measure(state, 16)['drx2_bytes']
        changed = (vertex(1, 0, 1, u=1), vertex(2, 0, 1, color=0), vertices[2])
        state['faces'][1] = (0, changed)
        self.assertEqual(gf.measure(state, 16)['drx2_bytes'] - shared, 48)

    def test_global_overflow_reports_without_overriding_contract(self):
        _, _, state = scene([(1, 0, 1), (2, 100, 1), (1, 0, 2)])
        state['faces'] *= drm.GLOBAL_CAP + 1
        contract = drm.contract_constants()
        report = gf.measure(state, 16)
        self.assertEqual(report['global_faces'], 4097)
        self.assertEqual(report['cap_checks']['global'], dict(observed=4097, cap=4096, passes=False))
        self.assertFalse(report['package_caps_pass'])
        self.assertFalse(report['packer_link_validated'])
        self.assertEqual(drm.contract_constants(), contract)

    def test_histograms_materials_and_transformed_world_metres(self):
        _, _, state = scene([(1, 0, 1), (3, 0, 1), (1, 0, 3)], translation=[20, 0, -20])
        state['faces'].append((0, (vertex(1, 0, 1), vertex(2, 100, 1), vertex(1, 0, 2))))
        report = gf.measure(state, 16)
        self.assertEqual(sum(report['cells_touched_histogram'].values()), 2)
        self.assertEqual(sum(report['global_cells_touched_histogram'].values()), 1)
        self.assertEqual(sum(report['max_axis_span_m_histogram'].values()), 2)
        self.assertEqual(report['max_span_xyz_m'], [2, 100, 2])
        self.assertEqual(report['by_material'][0]['faces'], 2)
        self.assertEqual(report['by_material'][0]['global_faces'], 1)
        self.assertEqual(state['faces'][0][1][0][:3], (21, 0, -19))

    def test_invalid_cell_and_split_threshold(self):
        _, _, state = scene([(1, 0, 1), (2, 0, 1), (1, 0, 2)])
        for cell in (0, 3, 65, math.inf, math.nan):
            with self.subTest(cell=cell), self.assertRaises(ValueError):
                gf.measure(state, cell)
        for split in (True, 0, -1, 4.5):
            with self.subTest(split=split), self.assertRaises(ValueError):
                gf.measure(state, 16, split)

    def test_small_threshold_and_explicit_depth_stall(self):
        vertices = (vertex(1, 0, 1), vertex(2, 0, 1), vertex(1, 0, 2))
        self.assertEqual(len(list(gf.subdivide([(0, vertices)], 16, 1))), 1)
        corner = (vertex(0, 0, 0), vertex(1, 0, 0), vertex(0, 0, 1))
        with patch.object(gf, 'MAX_SPLIT_DEPTH', 2):
            children = list(gf.subdivide([(0, corner)], 16, 1))
        self.assertTrue(any(c[3] for c in children))
        self.assertLessEqual(len(children), 4)

    def test_cli_json_and_deltas_without_game_files(self):
        _, _, state = scene([(1, 0, 1), (61, 0, 1), (1, 0, 61)])
        with patch.object(gf, 'load_scene', return_value=(state, bytes(32), ['synthetic'])), \
                patch.object(gf, 'write_report') as write, redirect_stdout(StringIO()) as out:
            self.assertEqual(gf.main(['synthetic.glb', '--cell', '4', '--split', '8',
                                      '--json', str(PORT / 'build' / 'synthetic.json')]), 0)
        report = json.loads(out.getvalue())
        self.assertEqual(report['split']['global_faces'], 0)
        self.assertEqual(report['split']['drx2_bytes_delta'],
                         report['split']['drx2_bytes'] - report['baseline']['drx2_bytes'])
        self.assertEqual(report['active_scene_roots'], ['synthetic'])
        self.assertEqual(write.call_count, 1)

    def test_json_output_is_new_and_confined_to_build(self):
        with self.assertRaises(ValueError):
            gf.write_report(PORT / 'outside.json', {})
        with patch.object(Path, 'open', mock_open()) as opened:
            gf.write_report(PORT / 'build' / 'synthetic.json', {'faces': 1})
        opened.assert_called_once_with('x', encoding='utf-8')
        self.assertEqual(json.loads(opened().write.call_args.args[0]), {'faces': 1})


if __name__ == '__main__':
    unittest.main()

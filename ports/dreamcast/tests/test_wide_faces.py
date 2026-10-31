"""Synthetic GLB subdivision, interpolation and conformity tests. (AI-assisted), ISC."""

from contextlib import redirect_stdout
from io import StringIO
import copy
import json
import math
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch


PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import prepare_render_sectors as drm
import wide_faces as wf


def scene(groups, transforms=None, normal=False, tangent=False, normalized=False):
    """Each group is an independent primitive, including independent seam data."""
    binary = bytearray()
    doc = dict(asset=dict(version='2.0'), buffers=[dict(byteLength=0)], bufferViews=[],
               accessors=[], materials=[dict(name='synthetic', doubleSided=True)],
               meshes=[], nodes=[], scenes=[dict(nodes=[])], scene=0)

    def acc(values, kind, component=5126, norm=False):
        binary.extend(b'\0' * (-len(binary) % 4))
        offset = len(binary)
        code = {5126: 'f', 5125: 'I', 5121: 'B'}[component]
        for value in values:
            binary.extend(struct.pack('<' + code * len(value), *value))
        doc['bufferViews'].append(dict(buffer=0, byteOffset=offset, byteLength=len(binary) - offset))
        info = dict(bufferView=len(doc['bufferViews']) - 1, componentType=component,
                    count=len(values), type=kind)
        if norm:
            info['normalized'] = True
        doc['accessors'].append(info)
        return len(doc['accessors']) - 1

    for gi, points in enumerate(groups):
        attrs = dict(POSITION=acc(points, 'VEC3'),
                     TEXCOORD_0=acc([(p[0] / 32, p[2] / 32) for p in points], 'VEC2'))
        colors = [(0, 64, 128, 255), (255, 128, 64, 128), (128, 255, 0, 64)] * (len(points) // 3)
        attrs['COLOR_0'] = acc(colors if normalized else [tuple(v / 255 for v in c) for c in colors],
                              'VEC4', 5121 if normalized else 5126, normalized)
        if normal:
            attrs['NORMAL'] = acc([(0, 1, 0), (1, 0, 0), (0, 1, 0)] * (len(points) // 3), 'VEC3')
        if tangent:
            attrs['TANGENT'] = acc([(1, 0, 0, 1), (0, 0, 1, 1), (1, 0, 0, 1)] * (len(points) // 3), 'VEC4')
        primitive = dict(attributes=attrs, material=0, indices=acc([(i,) for i in range(len(points))], 'SCALAR', 5125))
        doc['meshes'].append(dict(name=f'mesh-{gi}', primitives=[primitive]))
        node = dict(mesh=gi, name=f'node-{gi}')
        if transforms:
            node.update(transforms[gi])
        doc['nodes'].append(node)
        doc['scenes'][0]['nodes'].append(gi)
    doc['buffers'][0]['byteLength'] = len(binary)
    return wf.encode_glb(doc, binary)


def triangles(blob):
    doc, binary = drm.parse_glb_bytes(blob)
    state = drm.extract_scene(doc, binary, drm.DEFAULT_TEXTURE_SIZE)
    return state['faces']


def area(vertices):
    a, b, c = vertices
    return ((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) / 2


class WideFacesTest(unittest.TestCase):
    def test_narrow_multicell_face_is_unchanged(self):
        blob = scene([[(1, 0, 1), (62, 0, 1), (1, 0, 62)]])
        result, report = wf.split_glb(blob)
        self.assertEqual(report['faces_added'], 0)
        self.assertEqual(triangles(blob), triangles(result))
        a, _ = drm.parse_glb_bytes(blob)
        b, _ = drm.parse_glb_bytes(result)
        self.assertEqual(a, b)

    def test_limit_inclusive_and_only_xz_requests_split(self):
        blob = scene([[(0, 0, 0), (64, 0, 0), (0, 200, 64)]])
        result, report = wf.split_glb(blob)
        self.assertEqual(report['original_wide_faces'], 0)
        self.assertEqual(len(triangles(result)), 1)
        faces = triangles(result)
        self.assertIsNone(drm.classify_triangle(*zip(*(v[:3] for v in faces[0][1])), 16))

    def test_recursive_split_preserves_area_winding_and_caps(self):
        blob = scene([[(1, 0, 1), (258, 0, 1), (1, 0, 130)]])
        result, proof = wf.split_glb(blob)
        faces = triangles(result)
        self.assertGreater(proof['faces_added'], 1)
        self.assertAlmostEqual(sum(area(v) for _, v in faces), area(triangles(blob)[0][1]))
        self.assertTrue(all(area(v) > 0 for _, v in faces))
        self.assertTrue(all(max(v[k] for v in vs) - min(v[k] for v in vs) <= 64
                            for _, vs in faces for k in (0, 2)))
        doc, binary = drm.parse_glb_bytes(result)
        package = drm.build_package(doc, binary, 16)
        self.assertEqual(package.summary['source_count'], len(faces))
        self.assertEqual(package.summary['global_count'], 0)

    def test_shared_reversed_edges_across_primitives_are_conforming(self):
        a, b = (0, 0, 0), (256, 0, 0)
        blob = scene([[a, b, (0, 0, 96)], [b, a, (256, 0, -96)]], normalized=True)
        result, proof = wf.split_glb(blob)
        self.assertEqual(proof['split_shared_original_edges'], 1)
        self.assertGreaterEqual(proof['shared_boundary_segments_checked'], 8)
        self.assertEqual(proof['missing_shared_segments'], 0)
        self.assertEqual(proof['unresolved_split_edges'], 0)
        doc, binary = drm.parse_glb_bytes(result)
        chains = []
        for mesh in doc['meshes']:
            attrs = mesh['primitives'][0]['attributes']
            pts = drm.accessor(doc, binary, attrs['POSITION'])
            chains.append({struct.pack('<3f', *p) for p in pts if p[2] == 0})
        self.assertEqual(chains[0], chains[1])

    def test_narrow_neighbour_gets_only_required_stitch(self):
        # AB's diagonal is the longest edge, though neither axis exceeds 64 m.
        a, b = (0, 0, 0), (64, 0, 64)
        blob = scene([[a, b, (0, 0, 65)], [b, a, (40, 0, 0)]])
        _, proof = wf.split_glb(blob)
        self.assertEqual(proof['narrow_faces_stitched'], 1)
        self.assertEqual(proof['narrow_stitch_faces_added'], 1)
        self.assertEqual(proof['missing_shared_segments'], 0)

    def test_later_split_propagates_back_to_an_earlier_narrow_face(self):
        a, b = (0, 0, 0), (64, 0, 64)
        blob = scene([[b, a, (40, 0, 0)], [a, b, (0, 0, 65)]])
        result, proof = wf.split_glb(blob)
        self.assertGreaterEqual(proof['passes'], 3)
        self.assertEqual(proof['narrow_faces_stitched'], 1)
        self.assertEqual(proof['missing_shared_segments'], 0)
        self.assertEqual(len(triangles(result)), proof['output_faces'])

    def test_a_disconnected_narrow_face_keeps_its_original_accessors(self):
        blob = scene([[(0, 0, 0), (256, 0, 0), (0, 0, 64)],
                      [(10, 0, 10), (20, 0, 10), (10, 0, 20)]])
        result, proof = wf.split_glb(blob)
        before, _ = drm.parse_glb_bytes(blob)
        after, _ = drm.parse_glb_bytes(result)
        self.assertEqual(before['meshes'][1], after['meshes'][1])
        self.assertEqual(proof['narrow_faces_stitched'], 0)

    def test_shared_edge_stitching_does_not_request_more_width_cuts(self):
        groups = [[(0, 0, 0), (256, 0, 0), (50, 0, 130)],
                  [(256, 0, 0), (0, 0, 0), (180, 0, -95)]]
        independent = sum(wf.split_glb(scene([group]))[1]['width_faces_added'] for group in groups)
        _, proof = wf.split_glb(scene(groups))
        self.assertEqual(proof['width_faces_added'], independent)
        self.assertEqual(proof['width_faces_added'] + proof['conformity_faces_added'], proof['faces_added'])
        self.assertEqual(proof['unresolved_split_edges'], 0)

    def test_uv_colour_are_linear_before_material_bake(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]], normalized=True)
        doc, binary = drm.parse_glb_bytes(blob)
        sub = wf.Subdivision(doc, binary, 64)
        p = sub.primitives[0]
        m = sub.split_vertex(p, 0, 1)
        self.assertEqual(p['attrs']['POSITION'][m], (64, 0, 0))
        self.assertEqual(p['attrs']['TEXCOORD_0'][m], (2, 0))
        self.assertEqual(p['attrs']['COLOR_0'][m], (0.5, 96 / 255, 96 / 255, 383 / 510))
        self.assertEqual(sub.split_vertex(p, 1, 0), m)

    def test_normals_and_tangents_normalize_only_at_serialization(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]], normal=True, tangent=True)
        result, proof = wf.split_glb(blob)
        self.assertTrue(proof['serialized_midpoints_verified'])
        doc, binary = drm.parse_glb_bytes(result)
        attrs = doc['meshes'][0]['primitives'][0]['attributes']
        for name in ('NORMAL', 'TANGENT'):
            values = drm.accessor(doc, binary, attrs[name])
            self.assertTrue(all(abs(sum(v * v for v in value[:3]) - 1) < 1e-6 for value in values))
        self.assertTrue(all(t[3] == 1 for t in drm.accessor(doc, binary, attrs['TANGENT'])))

    def test_transformed_world_limit_and_nodes_roots_preserved(self):
        blob = scene([[(0, 0, 0), (40, 0, 0), (0, 0, 40)]],
                     transforms=[dict(scale=[4, 1, 2], translation=[-100, 7, 50])])
        result, proof = wf.split_glb(blob)
        self.assertEqual(proof['original_wide_faces'], 1)
        a, _ = drm.parse_glb_bytes(blob)
        b, _ = drm.parse_glb_bytes(result)
        for key in ('nodes', 'scenes', 'scene', 'materials'):
            self.assertEqual(a[key], b[key])
        for _, vs in triangles(result):
            self.assertTrue(all(max(v[k] for v in vs) - min(v[k] for v in vs) <= 64 for k in (0, 2)))

    def test_instanced_mesh_accounts_for_every_instance(self):
        blob = scene([[(0, 0, 0), (40, 0, 0), (0, 0, 40)]])
        doc, binary = drm.parse_glb_bytes(blob)
        doc['nodes'].append(dict(mesh=0, scale=[4, 1, 4]))
        doc['scenes'][0]['nodes'].append(1)
        result, proof = wf.split_glb(wf.encode_glb(doc, binary))
        self.assertEqual(proof['original_instance_faces'], 2)
        self.assertEqual(len(triangles(result)), proof['output_faces'] * 2)

    def test_different_transform_domains_with_world_seam_refused(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 8)],
                      [(0, 0, 0), (64, 0, 0), (0, 0, -8)]],
                     transforms=[{}, dict(scale=[2, 1, 1])])
        with self.assertRaisesRegex(ValueError, 'different transform domains'):
            wf.split_glb(blob)

    def test_uv_bound_is_not_hidden_by_clamp_or_wrap(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        doc, binary = drm.parse_glb_bytes(blob)
        uv = doc['accessors'][doc['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']]
        at = doc['bufferViews'][uv['bufferView']]['byteOffset']
        binary = bytearray(binary)
        struct.pack_into('<f', binary, at, 65537)
        with self.assertRaisesRegex(ValueError, 'exceeds 65536.0 by 1.0'):
            wf.split_glb(wf.encode_glb(doc, binary))
        struct.pack_into('<f', binary, at, 65536)
        result, proof = wf.split_glb(wf.encode_glb(doc, binary))
        self.assertEqual(proof['max_input_uv_abs'], 65536)
        self.assertTrue(all(abs(v[k]) <= 65536 for _, vs in triangles(result) for v in vs for k in (3, 4)))

    def test_small_and_degenerate_faces_survive(self):
        blob = scene([[(0, 0, 0)] * 3 + [(1, 0, 1), (2, 0, 1), (1, 0, 2)]])
        result, proof = wf.split_glb(blob)
        self.assertEqual(proof['faces_added'], 0)
        self.assertEqual(triangles(result), triangles(blob))

    def test_invalid_limits(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        for limit in (0, -1, math.inf, math.nan, True):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                wf.split_glb(blob, limit)

    def test_source_cap_stops_subdivision(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        with patch.object(drm, 'MAX_SOURCE', 1), self.assertRaisesRegex(ValueError, 'source cap 1'):
            wf.split_glb(blob)

    def test_guarded_unsupported_attributes_modes_and_sparse(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        doc, binary = drm.parse_glb_bytes(blob)
        for extra in (dict(mode=5), dict(targets=[]), dict(extensions=dict(unknown={}))):
            changed = copy.deepcopy(doc)
            changed['meshes'][0]['primitives'][0].update(extra)
            with self.assertRaisesRegex(ValueError, 'static triangle'):
                wf.split_glb(wf.encode_glb(changed, binary))
        doc['meshes'][0]['primitives'][0]['attributes']['JOINTS_0'] = 0
        with self.assertRaisesRegex(ValueError, 'unsupported vertex attribute'):
            wf.split_glb(wf.encode_glb(doc, binary))

    def test_extra_chunks_and_embedded_bytes_preserved(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        doc, binary = drm.parse_glb_bytes(blob)
        blob = wf.encode_glb(doc, binary, [(b'TEST', b'abcd')])
        result, _ = wf.split_glb(blob)
        self.assertEqual(wf.tfrag_glb.read_chunks(result)[-1], (b'TEST', b'abcd'))
        _, out = drm.parse_glb_bytes(result)
        self.assertEqual(out[:len(binary)], binary)

    def test_report_matches_actual_builder_and_window_caps(self):
        blob = scene([[(1, 0, 1), (129, 0, 1), (1, 0, 17)]])
        result, _ = wf.split_glb(blob)
        report = wf.measure(result)
        self.assertTrue(report['build_package']['passes'])
        self.assertTrue(report['package_caps_pass'])
        self.assertTrue(report['drx2_encoded_and_validated'])
        self.assertEqual(report['global_faces'], 0)
        self.assertEqual(report['blocking_caps'], {})
        self.assertEqual([s['half'] for s in report['window_caps']], [14, 10])
        self.assertEqual([s['step'] for s in report['window_caps']], [8, 8])

    def test_cap_failure_reports_exact_excess_and_still_samples(self):
        blob = scene([[(1, 0, 1), (2, 0, 1), (1, 0, 2)] * 2])
        with patch.object(drm, 'PER_SECTOR_CAP', 1):
            report = wf.measure(blob)
        self.assertFalse(report['build_package']['passes'])
        self.assertEqual(report['blocking_caps']['per_sector'], dict(observed=2, cap=1, passes=False, excess=1))
        self.assertEqual(len(report['window_caps']), 2)

    def test_global_spatial_index_matches_original_full_grid_metrics(self):
        blob = scene([[(-64, 0, -64), (0, 200, 0), (-1, 0, -63)],
                      [(0, 0, 0), (64, 200, 64), (63, 0, 1)],
                      [(-1, 0, -1), (1, 0, -1), (-1, 0, 1)]])
        optimized = wf.measure(blob)['window_caps']
        with patch.object(wf.DiagnosticPackage, 'touched', wf.window_caps.LoadedPackage.touched):
            original = wf.measure(blob)['window_caps']
        self.assertEqual(optimized, original)

    def test_global_spatial_index_preserves_large_face_fallback_and_empty_block(self):
        for groups in ([[(-256, 0, -256), (256, 200, -256), (-256, 0, 256)]],
                       [[(1, 0, 1), (2, 0, 1), (1, 0, 2)]]):
            blob = scene(groups)
            optimized = wf.measure(blob, halves=(14,))['window_caps']
            with patch.object(wf.DiagnosticPackage, 'touched', wf.window_caps.LoadedPackage.touched):
                original = wf.measure(blob, halves=(14,))['window_caps']
            self.assertEqual(optimized, original)

    def test_outputs_must_be_new_and_under_build(self):
        with self.assertRaisesRegex(ValueError, 'below'):
            wf.output_paths(PORT / 'outside.glb')
        with patch.object(Path, 'exists', return_value=True), self.assertRaisesRegex(ValueError, 'new files'):
            wf.output_paths(PORT / 'build' / 'synthetic.glb')

    def test_cli_writes_two_exclusive_outputs(self):
        blob = scene([[(0, 0, 0), (128, 0, 0), (0, 0, 16)]])
        from unittest.mock import mock_open
        opened = mock_open()
        with patch.object(Path, 'exists', return_value=False), \
                patch.object(Path, 'read_bytes', return_value=blob), \
                patch.object(Path, 'mkdir'), patch.object(Path, 'open', opened), \
                redirect_stdout(StringIO()):
            self.assertEqual(wf.main(['synthetic.glb', str(PORT / 'build' / 'synthetic.glb')]), 0)
        self.assertEqual(opened.call_args_list[0].args, ('xb',))
        self.assertEqual(opened.call_args_list[1].args, ('x',))
        self.assertEqual(len(opened().write.call_args_list), 2)
        report = json.loads(opened().write.call_args_list[1].args[0])
        self.assertTrue(report['verified']['build_package']['passes'])
        self.assertEqual(report['disclosure'], '(AI-assisted)')


if __name__ == '__main__':
    unittest.main()

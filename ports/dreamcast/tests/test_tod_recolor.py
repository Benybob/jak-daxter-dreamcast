"""Synthetic native palettes and GLBs for TOD recoloring (AI-assisted), ISC."""

import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tod_recolor as tod


def palette(*pairs):
    return tuple(tuple((*a, 128) if t == 0 else (*b, 64) for t in range(8)) for a, b in pairs)


def native(roots):
    result = tod.Native.__new__(tod.Native)
    result.roots = roots
    result.wind = {}
    return result


class Glb:
    """Interleaved RGBAs with sentinel padding, explicit index offsets/strides."""

    def __init__(self, colors):
        self.binary = bytearray(b'HEAD')
        for color in colors:
            self.binary += struct.pack('<4f', *(v / 255 for v in color[:3]), color[3]) + b'PAD!'
        self.doc = {'asset': {'version': '2.0'}, 'buffers': [{}],
                    'bufferViews': [{'buffer': 0, 'byteOffset': 4,
                                     'byteLength': len(colors) * 20, 'byteStride': 20}],
                    'accessors': [{'bufferView': 0, 'componentType': 5126,
                                   'type': 'VEC4', 'count': len(colors)}],
                    'nodes': [], 'meshes': [], 'scene': 0, 'scenes': [{'nodes': []}]}

    def node(self, root, name, indices, component=5123, matrix=None):
        code = {5121: 'B', 5123: 'H', 5125: 'I'}[component]
        size = struct.calcsize(code)
        self.binary += b'\0' * (-len(self.binary) % 4)
        at = len(self.binary)
        self.binary += b'OFFS'
        for i in indices:
            self.binary += struct.pack('<' + code, i) + b'xx'[:size]
        stride = size + min(size, 2)
        view = len(self.doc['bufferViews'])
        self.doc['bufferViews'].append(dict(buffer=0, byteOffset=at,
                                            byteLength=len(self.binary) - at, byteStride=stride))
        acc = len(self.doc['accessors'])
        self.doc['accessors'].append(dict(bufferView=view, byteOffset=4, componentType=component,
                                          type='SCALAR', count=len(indices)))
        self.doc['meshes'].append({'primitives': [{'attributes': {'COLOR_0': 0}, 'indices': acc}]})
        child = len(self.doc['nodes'])
        node = {'name': name, 'mesh': len(self.doc['meshes']) - 1}
        if matrix is not None:
            node['matrix'] = matrix
        self.doc['nodes'].append(node)
        # Use one root per call; duplicate root names are legal scene nodes.
        self.doc['nodes'].append({'name': root, 'children': [child]})
        self.doc['scenes'][0]['nodes'].append(child + 1)
        return child

    def build(self):
        self.doc['buffers'][0]['byteLength'] = len(self.binary)
        js = json.dumps(self.doc, indent=1).encode()
        js += b' ' * (-len(js) % 4)
        binary = bytes(self.binary) + b'\0' * (-len(self.binary) % 4)
        body = b''.join(struct.pack('<I4s', len(payload), kind) + payload for kind, payload in
                        ((b'JSON', js), (b'BIN\0', binary), (b'TEST', b'keep')))
        return struct.pack('<4sII', b'glTF', 2, len(body) + 12) + body


class RecolorTests(unittest.TestCase):
    def test_corner_counts_ambiguity_and_preserved_bytes(self):
        p = palette(((10, 20, 30), (80, 90, 100)),
                    ((10, 20, 30), (150, 160, 170)),
                    ((40, 50, 60), (100, 110, 120)),
                    ((40, 50, 60), (100, 110, 120)))
        b = Glb([(10, 20, 30, 0.25), (40, 50, 60, 0.75), (3, 4, 5, 1), (10, 20, 30, 1)])
        b.node('level-tfrag-normal', 'terrain', [0, 0, 1, 1, 1, 2])
        original = b.build()
        output, report = tod.recolor(original, native({'tfrag-normal': p}), 2)
        r = report['groups']['terrain']
        self.assertEqual((r['corners'], r['unique'], r['ambiguous'], r['unmatched']), (6, 3, 2, 1))
        self.assertEqual(r['duplicate_rows_same_target_corners'], 3)
        self.assertAlmostEqual(r['mean_rgb_before'][0], (20 + 120 + 3) / 6, places=5)
        self.assertAlmostEqual(r['mean_rgb_after'][0], (160 + 300 + 3) / 6, places=5)
        self.assertEqual(len(original), len(output))
        parts = tod.chunks(original)
        bin_at = parts[1][1]
        permitted = {bin_at + 4 + 20 * i + j for i in (0, 1) for j in range(12)}
        self.assertTrue(all(i in permitted for i, (a, z) in enumerate(zip(original, output)) if a != z))
        self.assertEqual(output[bin_at + 4:bin_at + 16], tod.rgb_bytes((80, 90, 100)))
        self.assertEqual(report['referenced_color_elements'], 3)

    def test_own_palettes_shared_color_accessor_and_all_index_widths(self):
        a = palette(((10, 20, 30), (50, 60, 70)))
        b = palette(((10, 20, 30), (150, 160, 170)))
        glb = Glb([(10, 20, 30, 1)] * 3)
        glb.node('level-tfrag-normal', 'ground', [0] * 3, 5121)
        glb.node('level-tie-0', 'vil3-norm-wallmed-02.mb', [1] * 3, 5123)
        glb.node('level-shrub-0', 'leaf', [2] * 3, 5125)
        output, report = tod.recolor(glb.build(), native({
            'tfrag-normal': a, 'tie-0': {'vil3-norm-wallmed-02.mb': b}, 'shrub-0': a}), 1)
        at = tod.chunks(output)[1][1] + 4
        self.assertEqual(output[at:at + 12], tod.rgb_bytes((50, 60, 70)))
        self.assertEqual(output[at + 20:at + 32], tod.rgb_bytes((150, 160, 170)))
        self.assertEqual(report['total']['unique'], 9)
        self.assertEqual(report['vil3-norm-wallmed-02.mb']['corners'], 3)

    def test_reject_conflicting_shared_vertex(self):
        p = palette(((1, 2, 3), (4, 5, 6)))
        q = palette(((1, 2, 3), (7, 8, 9)))
        b = Glb([(1, 2, 3, 1)])
        b.node('l-tie-0', 'one', [0] * 3)
        b.node('l-tie-0', 'two', [0] * 3)
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            tod.recolor(b.build(), native({'tie-0': {'one': p, 'two': q}}), 1)

    def test_float_match_is_exact_and_slot_zero_is_identity(self):
        p = palette(((1, 2, 3), (4, 5, 6)), ((1, 2, 3), (7, 8, 9)))
        b = Glb([(1, 2, 3, 1)])
        b.node('l-shrub-0', 'leaf', [0] * 3)
        blob = b.build()
        output, report = tod.recolor(blob, native({'shrub-0': p}), 0)
        self.assertEqual(output, blob)
        self.assertEqual(report['total']['unique'], 3)
        changed = bytearray(blob)
        at = tod.chunks(blob)[1][1] + 4
        bits, = struct.unpack_from('<I', changed, at)
        struct.pack_into('<I', changed, at, bits + 1)
        output, report = tod.recolor(bytes(changed), native({'shrub-0': p}), 1)
        self.assertEqual(report['total']['unmatched'], 3)
        self.assertEqual(output, changed)

    def test_reject_bad_inputs(self):
        p = palette(((1, 2, 3), (4, 5, 6)))
        n = native({'shrub-0': p})
        for slot in (-1, 8):
            with self.assertRaises(ValueError):
                tod.recolor(b'', n, slot)
        with self.assertRaises(ValueError):
            tod.chunks(b'glTF')
        b = Glb([(1, 2, 3, 1)])
        b.node('l-shrub-0', 'leaf', [1] * 3)
        with self.assertRaisesRegex(ValueError, 'out of bounds'):
            tod.recolor(b.build(), n, 1)
        b.doc['accessors'][0]['sparse'] = {}
        with self.assertRaisesRegex(ValueError, 'unsupported accessor'):
            tod.recolor(b.build(), n, 1)
        del b.doc['accessors'][0]['sparse']
        b.doc['nodes'][1]['name'] = 'l-unknown'
        with self.assertRaisesRegex(ValueError, 'no native palette'):
            tod.recolor(b.build(), n, 1)

    def test_nonindexed_rgb_accessor(self):
        b = Glb([(1, 2, 3, 1)] * 3)
        b.node('l-shrub-0', 'leaf', [0] * 3)
        del b.doc['meshes'][0]['primitives'][0]['indices']
        b.doc['accessors'][0]['type'] = 'VEC3'
        _, report = tod.recolor(b.build(), native({'shrub-0': palette(((1, 2, 3), (4, 5, 6)))}), 7)
        self.assertEqual(report['total']['unique'], 3)

    def test_noon_and_cli(self):
        for level in ('village1', 'beach', 'jungle', 'training', 'maincave', 'darkcave', 'robocave'):
            self.assertEqual(tod.NOON_SOURCES[level][1], (2, 3, 0.0))
        for level in ('misty', 'village2', 'swamp', 'sunken', 'sunkenb'):
            self.assertEqual(tod.NOON_SOURCES[level][1], (1, 0, 0.99))
        for level in ('firecanyon', 'village3', 'snow'):
            self.assertEqual(tod.NOON_SOURCES[level][1], (1, 2, 0.0))
        self.assertEqual(tod.NOON_SOURCES['rolling'][1], (0, 1, 0.5))
        b = Glb([(1, 2, 3, 1)])
        b.node('l-shrub-0', 'leaf', [0] * 3)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, out, report = root / 'in.glb', root / 'out.glb', root / 'out.json'
            source.write_bytes(b.build())
            with patch.object(tod.level_contents, 'objects', return_value=[('l-vis', 1, 'level')]), \
                 patch.object(tod.level_contents, 'object_bytes', return_value=b''), \
                 patch.object(tod, 'Native', return_value=native({'shrub-0': palette(((1, 2, 3), (4, 5, 6)))})), \
                 contextlib.redirect_stdout(io.StringIO()):
                tod.main([str(source), 'fake.DGO', '--noon', 'village3', '--out', str(out), '--json', str(report)])
            self.assertEqual(json.loads(report.read_text())['slot'], 1)
            self.assertEqual(len(source.read_bytes()), len(out.read_bytes()))

    def test_noon_misty_weights_snapshot_two_and_rolling_blends(self):
        p = palette(((10, 20, 30), (110, 120, 130)))
        b = Glb([(10, 20, 30, 0.25)])
        b.node('l-shrub-0', 'leaf', [0] * 3)
        original = b.build()
        at = tod.chunks(original)[1][1] + 4
        for level, expected in (('misty', (11, 21, 31)), ('rolling', (60, 70, 80))):
            output, report = tod.recolor(original, native({'shrub-0': p}),
                                         tod.NOON_SOURCES[level][1])
            self.assertEqual(output[at:at + 12], tod.rgb_bytes(expected))
            self.assertNotEqual(output[at:at + 12], tod.rgb_bytes((110, 120, 130)))
            self.assertEqual(output[at + 12:at + 16], original[at + 12:at + 16])
            self.assertIsNone(report['slot'])


class NativeTests(unittest.TestCase):
    def fixture(self):
        data = bytearray(2048)
        found = [(0, 'drawable-tree-array'), (64, 'drawable-tree-tfrag'),
                 (128, 'drawable-tree-instance-tie'), (192, 'drawable-tree-instance-shrub'),
                 (256, 'proxy-prototype-array-tie'), (288, 'prototype-array-tie'),
                 (320, 'prototype-bucket-tie'), (512, 'instance-tie')]
        def put(at, fmt, *args):
            struct.pack_into(fmt, data, at, *args)
        put(6, '<h', 3)
        put(32, '<3I', 68, 132, 196)
        put(76, '<I', 800)  # terrain palette: basic+8
        put(136, '<I', 260)  # TIE proxy
        put(260, '<I', 292)
        put(292, '<I', 1)
        put(304, '<I', 324)
        put(324, '<I', 700)
        put(700, '<I', 4)
        data[704:708] = b'rock'
        put(380, '<f', 1)  # bucket+56 stiffness
        put(464, '<I', 900)  # bucket+140 palette
        put(204, '<I', 1000)  # shrub palette
        put(524, '<I', 324)  # instance+8 bucket
        put(528, '<4f', 4096, 8192, 12288, 1)
        put(544, '<16h', 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0)
        for at, rgb in ((800, (1, 2, 3, 255)), (900, (4, 5, 6, 128)), (1000, (7, 8, 9, 64))):
            put(at, '<3I', 8, 1, 0)
            for t in range(8):
                put(at + 12 + t * 4, '<4B', *(v + t if k < 3 else v for k, v in enumerate(rgb)))
        obj = struct.pack('<4I', 0, 0, 4, len(data)) + data
        return obj, found

    def test_native_tree_links_eight_samples_padding_and_wind(self):
        obj, found = self.fixture()
        with patch.object(tod.level_contents, 'basics', return_value=(len(obj) - 16, found)):
            n = tod.Native(obj)
        self.assertEqual(n.roots['tfrag-normal'][0][7], (8, 9, 10, 255))
        self.assertEqual(len(n.roots['tfrag-normal']), 4)
        self.assertEqual(n.roots['tfrag-normal'][3][7], (0, 0, 0, 0))
        self.assertEqual(n.roots['tie-0']['rock'][0][2], (6, 7, 8, 128))
        self.assertEqual(n.roots['shrub-0'][0][3], (10, 11, 12, 64))
        matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 0]
        self.assertEqual(n.for_node('tie-0', {'name': 'wind-group-0-0', 'matrix': matrix}),
                         n.roots['tie-0']['rock'])
        matrix[12] += 1
        with self.assertRaisesRegex(ValueError, 'wind matrix'):
            n.for_node('tie-0', {'name': 'wind-group-0-0', 'matrix': matrix})

    def test_native_palette_validation(self):
        n = tod.Native.__new__(tod.Native)
        n.palettes = {}
        for header in ((7, 1, 0), (8, 0, 0), (8, 1, 1), (8, 9999, 0)):
            n.data = struct.pack('<3I', *header)
            with self.assertRaises(ValueError):
                n.palette(0)


if __name__ == '__main__':
    unittest.main()

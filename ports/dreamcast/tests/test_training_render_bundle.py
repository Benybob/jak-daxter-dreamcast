"""Bundle ownership/coverage tests, procedural inputs only (AI-assisted), ISC.

The subprocess validator is mocked here; native WLD validation has its own
suite. These cases exercise the bundle's independent source and coverage gates.
"""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import prepare_render_sectors as r
import prepare_training_render as training
from test_render_sectors import GlbBuilder, _package


class TrainingBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / 'training'
        self.assets.mkdir()
        b = GlbBuilder()
        p = b.add_vec3([(0, 0, 0), (1, 0, 0), (0, 1, 1)])
        i = b.add_indices([0, 1, 2])
        b.scenes[0]['nodes'] = [b.add_node(mesh=b.add_mesh([
            dict(attributes=dict(POSITION=p), indices=i)]))]
        packet = _package(b.to_bytes(), cell_size=16)
        self.render = self.root / 'render'
        r.publish_package(self.render, packet)
        self.sha = r.parse_manifest(packet.manifest)['source_sha256'].hex()
        (self.assets / 'report.json').write_text(json.dumps(dict(source_glb_sha256=self.sha)))
        actor = bytearray(32)
        actor[:8] = b'DCT1\x01\0\0\0'
        struct.pack_into('<I', actor, 12, 3)
        for name, data in [('player.dct', actor), ('player.dca', b'animation'),
                           ('world.dct', b'terrain'), ('world.dcc', b'collision')]:
            (self.assets / name).write_bytes(data)
        self.xfm = bytearray(96)
        self.xfm[:8] = b'WLD3\x03\0\0\0'
        struct.pack_into('<3f', self.xfm, 84, 18, 12, 18)
        self.output = self.root / 'bundle'
        self.header = self.root / 'binding.h'

    def prepare(self):
        (self.assets / 'world.xfm').write_bytes(self.xfm)
        with patch.object(training.subprocess, 'run') as validate:
            result = training.prepare(self.assets, self.render, self.output,
                                      self.header, checker=Path(__file__))
            validate.assert_called_once()
            return result

    def test_complete_selected_files_and_every_anchor_match(self):
        result = self.prepare()
        self.assertTrue(result['partial_bundle_windows_match_full_source'])
        self.assertGreater(len(result['source_window_oracles']), 1)
        self.assertEqual(result['actor_triangles'], 1)
        self.assertEqual((self.output / 'render/manifest.drm').read_bytes(),
                         (self.render / 'manifest.drm').read_bytes())
        for row in result['copied_sectors']:
            name = f'{row[0]}_{row[1]}.drs'
            self.assertEqual((self.output / 'render/sectors' / name).read_bytes(),
                             (self.render / 'sectors' / name).read_bytes())

    def test_missing_selected_file_never_becomes_empty(self):
        next((self.render / 'sectors').glob('*.drs')).unlink()
        with self.assertRaises(FileNotFoundError):
            self.prepare()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.header.exists())

    def test_rounding_bound_is_enforced_before_output(self):
        struct.pack_into('<f', self.xfm, 8, -32768)
        with self.assertRaisesRegex(ValueError, '4096m'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_different_collision_source_is_refused(self):
        (self.assets / 'report.json').write_text(json.dumps(dict(source_glb_sha256='0' * 64)))
        with self.assertRaisesRegex(ValueError, 'source GLB mismatch'):
            self.prepare()
        self.assertFalse(self.output.exists())

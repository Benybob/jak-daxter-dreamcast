"""mesh_case_data.py: where it looks for VI1.DGO and what it writes (AI-assisted), ISC.

Synthetic data only: the real meshes come from the user's disc and are never
committed.
"""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mesh_case_data  # noqa: E402


class MeshCaseDataTest(unittest.TestCase):
    def test_finds_the_dgo_from_the_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'VI1.DGO').write_bytes(b'')
            with mock.patch.dict(os.environ, {'DC_JAK_DGO_DIR': directory}):
                self.assertEqual(mesh_case_data.find_dgo(), Path(directory) / 'VI1.DGO')

    def test_header_layout(self):
        mesh = {'polys': 2, 'verts': 4, 'strip_len': 5, 'base': (-16, 0, 32), 'vertex_qwc': 1, 'total_qwc': 2,
                'packed': bytes(range(32))}
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(mesh_case_data, 'meshes', return_value=([mesh], bytes(8))):
            path = Path(directory) / 'mesh_case.h'
            mesh_case_data.write_header(Path('unused'), path)
            text = path.read_text()
        self.assertIn('#define DC_M2C_MESH_COUNT 1u', text)
        self.assertIn('dc_m2c_mesh_pats[8]', text)
        self.assertIn('  {2, 5, 4, 1, 2, {-16, 0, 32}, dc_m2c_mesh0_packed},', text)


if __name__ == '__main__':
    unittest.main()

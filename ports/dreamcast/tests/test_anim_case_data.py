"""anim_case_data.py: where it looks for GAME.CGO and what it writes (AI-assisted), ISC.

Synthetic data only: the real animations come from the user's disc and are
never committed.
"""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import anim_case_data  # noqa: E402


class AnimCaseDataTest(unittest.TestCase):
    def test_finds_the_cgo_from_the_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'GAME.CGO').write_bytes(b'')
            with mock.patch.dict(os.environ, {'DC_JAK_CGO_DIR': directory}):
                self.assertEqual(anim_case_data.find_cgo(), Path(directory) / 'GAME.CGO')

    def test_header_layout(self):
        anims = {name: (2, 1, 1, bytes(range(16)), bytes(32)) for name in anim_case_data.ANIMS}
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(anim_case_data, 'animations', return_value=anims):
            path = Path(directory) / 'anim_case.h'
            anim_case_data.write_header(Path('unused'), path)
            text = path.read_text()
        self.assertIn('#define DC_M2C_ANIM_COUNT %du' % len(anim_case_data.ANIMS), text)
        self.assertIn('dc_m2c_anim0_fixed[16] = {\n  0x00, 0x01, 0x02', text)
        self.assertIn('{"%s", 2u, 1u, 1u, dc_m2c_anim0_fixed, dc_m2c_anim0_frames},' % anim_case_data.ANIMS[0], text)


if __name__ == '__main__':
    unittest.main()

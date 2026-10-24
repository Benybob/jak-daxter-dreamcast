"""Original-free camera/rig executable oracle (AI-assisted)."""
from pathlib import Path
import subprocess
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]
class CameraBoom(unittest.TestCase):
    def test_plane_distances_two_segments_atomic_refusal_rig_and_coverage(self):
        exe=PORT/'build'/('camera-boom-check.exe' if sys.platform=='win32' else 'camera-boom-check')
        r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        self.assertIn('500/500 checks',r.stdout)

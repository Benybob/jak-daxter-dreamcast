"""Portable near-plane interpolation and winding oracle (AI-assisted)."""
from pathlib import Path
import subprocess
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]
class SceneClip(unittest.TestCase):
    def test_analytic_interpolation_patterns_winding_and_guards(self):
        exe=PORT/'build'/('scene-clip-check.exe' if sys.platform=='win32' else 'scene-clip-check')
        r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        self.assertRegex(r.stdout,r'scene clip (\d+)/\1 checks')

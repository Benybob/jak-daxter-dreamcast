"""Execute portable movement and asset validation fixtures (AI-assisted)."""
from pathlib import Path
import subprocess
import unittest
import tempfile
import sys
PORT = Path(__file__).resolve().parents[1]
SUFFIX = '.exe' if sys.platform == 'win32' else ''

class PlayerNative(unittest.TestCase):
    def test_movement(self):
        r = subprocess.run([str(PORT/'build'/('player-motion-check'+SUFFIX))], capture_output=True, text=True, check=True)
        self.assertIn('318/318 checks', r.stdout)

    def test_animation_loader(self):
        with tempfile.TemporaryDirectory() as folder:
            r = subprocess.run([str(PORT/'build'/('player-anim-check'+SUFFIX)), str(Path(folder)/'fixture.dca')], capture_output=True, text=True, check=True)
        self.assertIn('208/208 checks', r.stdout)

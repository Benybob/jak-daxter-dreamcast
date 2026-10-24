"""Run original-free native collision fixtures (AI-assisted)."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
PORT = Path(__file__).resolve().parents[1]
SUFFIX = '.exe' if sys.platform == 'win32' else ''

class WorldCollision(unittest.TestCase):
    def test_capsule_controller_and_surface_rules(self):
        r = subprocess.run([str(PORT/'build'/('world-collision-check'+SUFFIX))], capture_output=True, text=True, check=True)
        self.assertIn('789/789 checks', r.stdout)

    def test_bounded_loader_failure_preserves_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            r = subprocess.run([str(PORT/'build'/('world-load-check'+SUFFIX)), str(Path(directory)/'fixture.dcc')], capture_output=True, text=True, check=True)
        self.assertIn('147/147 checks', r.stdout)

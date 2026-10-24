"""Own GPU-layer resource and submission instrumentation (AI-assisted)."""
from pathlib import Path
import re
import subprocess
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]

class TrainingLayer(unittest.TestCase):
    def test_allocation_failures_idempotent_cleanup_and_bounded_submission(self):
        exe=PORT/'build'/('training-layer-check.exe' if sys.platform=='win32' else 'training-layer-check')
        result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        self.assertRegex(result.stdout,r'training layer (\d+)/\1 checks')

"""Host validates real fiber C state/ownership, never native execution (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('fiber-check.exe' if sys.platform=='win32' else 'fiber-check')
@unittest.skipUnless(EXE.is_file(),'build actual fiber lifecycle checker first')
class FiberBoundaryTest(unittest.TestCase):
 def test_real_lifecycle_and_host_execution_refusal(self):
  p=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  report=json.loads(p.stdout)
  self.assertGreaterEqual(report['tests'],40)
  self.assertEqual(report['tests'],report['passed'])

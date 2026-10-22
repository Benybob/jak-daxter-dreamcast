"""Run the actual loader provenance implementation on host (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('code-entry-check.exe' if sys.platform=='win32' else 'code-entry-check')
@unittest.skipUnless(EXE.is_file(),'build code entry checker first')
class CodeEntryTest(unittest.TestCase):
 def test_exact_provenance_transaction_and_corruption_properties(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout)
  self.assertGreater(report['tests'],200)
  self.assertEqual(report['tests'],report['passed'])

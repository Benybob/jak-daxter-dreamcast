"""Host executes real validation, refusing target code (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('stack-call-check.exe' if sys.platform=='win32' else 'stack-call-check')
@unittest.skipUnless(EXE.is_file(),'build stack call boundary checker first')
class StackCallBoundaryTest(unittest.TestCase):
 def test_owned_span_entry_and_output_checks_never_execute_on_host(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout)
  self.assertGreaterEqual(report['tests'],25)
  self.assertEqual(report['tests'],report['passed'])

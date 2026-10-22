"""Execute shared actual C ABI acceptance; no native continuation claim (AI-assisted)."""
import json
from pathlib import Path
import subprocess,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('scalar-call-check.exe' if sys.platform=='win32' else 'scalar-call-check')
@unittest.skipUnless(EXE.is_file(),'build private scalar-call checker')
class ScalarCallTest(unittest.TestCase):
 def test_all_arities_full_words_and_rejection_without_call(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout)
  self.assertEqual(report['tests'],438);self.assertEqual(report['tests'],report['passed'])

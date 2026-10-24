"""Real C completion/import receipts; host does not execute SH4 (AI-assisted)."""
import json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('main-completion-check.exe' if sys.platform=='win32' else 'main-completion-check')
@unittest.skipUnless(EXE.is_file(),'build real completion oracle first')
class MainCompletionTest(unittest.TestCase):
 def test_receipt_identity_pid_header_and_readonly_release(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout);self.assertGreaterEqual(report['tests'],250)
  self.assertEqual(report['tests'],report['passed'])

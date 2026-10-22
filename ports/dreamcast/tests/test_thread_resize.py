"""Execute actual read-only resize preflight on C objects (AI-assisted)."""
import json
from pathlib import Path
import subprocess,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('thread-resize-check.exe' if sys.platform=='win32' else 'thread-resize-check')
@unittest.skipUnless(EXE.is_file(),'build read-only main-thread resize checker')
class ThreadResizeTest(unittest.TestCase):
 def test_owned_spans_main_thread_tail_headroom_and_readonly_behavior(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout)
  self.assertGreaterEqual(report['tests'],100);self.assertEqual(report['tests'],report['passed'])

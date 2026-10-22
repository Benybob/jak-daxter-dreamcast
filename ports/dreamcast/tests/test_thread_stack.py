"""Run independent native-C stack image properties on host (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('thread-stack-check.exe' if sys.platform=='win32' else 'thread-stack-check')
@unittest.skipUnless(EXE.is_file(),'build real thread stack checker first')
class ThreadStackTest(unittest.TestCase):
    def test_all_byte_depths_bounds_state_and_untouched_ranges(self):
        result=subprocess.run([str(EXE)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(result.stdout)
        self.assertGreater(report['tests'],4000)
        self.assertEqual(report['tests'],report['passed'])

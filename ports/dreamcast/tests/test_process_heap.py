"""Run the real portable process allocator guard suite (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
CHECK=ROOT/'build'/('process-heap-check.exe' if sys.platform=='win32' else 'process-heap-check')
class ProcessHeapTest(unittest.TestCase):
    def test_original_strict_boundary_zeroing_and_nonmutating_guards(self):
        result=subprocess.run([str(CHECK)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout),{'tests':102,'passed':102})

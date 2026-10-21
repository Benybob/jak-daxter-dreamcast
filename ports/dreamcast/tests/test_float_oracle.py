"""Check the pinned frontend's actual SSE instruction contract (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FloatOracleTest(unittest.TestCase):
    def test_sse_predicates_and_float_to_int_invalid_results(self):
        executable=ROOT/'build'/('float-check.exe' if os.name=='nt' else 'float-check')
        result=subprocess.run([str(executable)],text=True,capture_output=True)
        if result.returncode==77: self.skipTest('x86 SSE oracle is unavailable on this host')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout),{'tests':20,'passed':20})

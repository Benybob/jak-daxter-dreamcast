"""Allocator authority, generations, lifetime and output oracles (AI-assisted), ISC."""
from pathlib import Path
import os
import re
import subprocess
import unittest
EXE=Path(__file__).resolve().parents[1]/('build/process-spans-check'+('.exe' if os.name=='nt' else ''))
@unittest.skipUnless(EXE.is_file(),'build portable process span checker first')
class ProcessSpansTest(unittest.TestCase):
    def check_group(self,group):
        result=subprocess.run([str(EXE),str(group)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        match=re.search(r'process spans (\d+)/(\d+) checks',result.stdout)
        self.assertIsNotNone(match)
        self.assertGreater(int(match[1]),0)
        self.assertEqual(match[1],match[2])
    def test_sorted_extents_not_process_fields_and_failure_atomicity(self):self.check_group(0)
    def test_capacity_retirement_quiescence_and_reuse_generations(self):self.check_group(1)
    def test_metadata_alias_foreign_runtime_and_owner_guards(self):self.check_group(2)
    def test_temporary_output_requires_live_current_working_stack(self):self.check_group(3)

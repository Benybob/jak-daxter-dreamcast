"""Independent real portable scope lifecycle checks (AI-assisted), ISC."""
from pathlib import Path
import os
import re
import subprocess
import unittest
EXE=Path(__file__).resolve().parents[1]/('build/pp-scope-check'+('.exe' if os.name=='nt' else ''))
@unittest.skipUnless(EXE.is_file(),'build portable scope checker first')
class PpScopeTest(unittest.TestCase):
    def check_group(self,group):
        r=subprocess.run([str(EXE),str(group)],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        match=re.search(r'pp scope (\d+)/(\d+) checks',r.stdout)
        self.assertIsNotNone(match);self.assertGreater(int(match[1]),0);self.assertEqual(match[1],match[2])
    def test_nested_domains_allocations_and_retained_errors(self):self.check_group(0)
    def test_stale_scheduler_ancestors_lexical_and_context_refuse_without_writes(self):self.check_group(1)
    def test_hook_alias_depth_bounds_ownership_and_atomic_rejection(self):self.check_group(2)
    def test_fiber_anchor_and_fixed_return_cleanup_model(self):self.check_group(3)

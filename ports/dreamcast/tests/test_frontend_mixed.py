"""Genuine fixed-signature mixed GOAL entry/call ABI and remaining guards (AI-assisted)."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lower_sh4_ir import compile_function
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for mixed ABI integration checks')
class FrontendMixedTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env=os.environ.copy()
        if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        output=Path(cls.directory.name)/'fixture.json'
        r=subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(ROOT/'mixed_fixture.gc'),str(output)],env=env,capture_output=True,text=True)
        if r.returncode: raise AssertionError(r.stdout+r.stderr)
        cls.functions={f['name']:f for f in json.loads(output.read_text())['functions']}

    def test_fixed_mixed_entries_calls_and_compact_leaves_lower(self):
        for name in ('left','right','f0','f1','sum','floats','call','float-call','eight-last','eight-int'):
            with self.subTest(name=name):
                data,report=compile_function(self.functions['dc-mixed-'+name],runtime=True)
                self.assertTrue(data)
                if name in ('left','right','eight-int'): self.assertEqual(len(data),6)
                if name in ('f0','f1','eight-last'): self.assertEqual(len(data),4)

    def test_mixed_integer_stack_arguments_lower_after_gcc_oracle_verification(self):
        self.assertTrue(compile_function(self.functions['dc-mixed-stack'],runtime=True)[0])

    def test_mixed_variadic_call_is_not_treated_as_a_fixed_signature(self):
        # Variadic calls pass every argument in an integer slot (floats as bit
        # patterns, the desktop GOAL convention), so they differ from the fixed
        # mixed signature, which uses FPU registers for floats.
        fn=copy.deepcopy(self.functions['dc-mixed-call'])
        fixed,_=compile_function(copy.deepcopy(fn),runtime=True)
        op=next(op for op in fn['instructions'] if op['kind']=='call')
        op['function_type']='(function object string _varargs_ object)'
        variadic,_=compile_function(fn,runtime=True)
        self.assertNotEqual(fixed,variadic)

"""Fixed mixed stack arguments through the original frontend (AI-assisted)."""
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
from make_mixed_stack_fixture import generate
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for mixed stack checks')
class FrontendMixedStackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        source=Path(cls.directory.name)/'mixed.gc'; output=source.with_suffix('.json')
        source.write_text(generate()[0],encoding='utf-8')
        env=os.environ.copy()
        if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        result=subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(source),str(output)],
                              env=env,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.functions={f['name']:f for f in json.loads(output.read_text())['functions']}

    def test_generated_entries_call_wrappers_and_live_loop_values_all_lower(self):
        names=generate()[2]
        self.assertEqual(len(names),226)
        for name in names:
            with self.subTest(name=name):
                blob,_=compile_function(self.functions[name],runtime=True)
                self.assertTrue(blob)
        self.assertTrue(any(op['kind']=='goto' for op in self.functions['dc-ms-repeat']['instructions']))

    def test_mixed_variadic_and_more_than_eight_arguments_still_reject(self):
        fn=copy.deepcopy(self.functions['dc-ms-p9-a7-call'])
        call=next(op for op in fn['instructions'] if op['kind']=='call')
        call['function_type']='(function _varargs_ int)'
        with self.assertRaisesRegex(ValueError,'mixed variadic call'): compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.functions['dc-ms-p9-a7'])
        fn['parameters'].append(copy.deepcopy(fn['parameters'][-1]))
        with self.assertRaisesRegex(ValueError,'more than eight arguments'): compile_function(fn,runtime=True)

    def test_copy_only_returns_use_six_byte_integer_or_four_byte_float_leaves(self):
        stacked=0
        for name in generate()[2]:
            if name.endswith('-call') or name in ('dc-ms-live','dc-ms-repeat'): continue
            fn=self.functions[name]; blob,report=compile_function(fn,runtime=True)
            self.assertEqual(len(blob),4 if fn['return_type']=='float' else 6)
            self.assertEqual(report['stack_bytes'],0)
            if report['optimization']=='coalesced_stack_argument_return':
                stacked+=1
                baseline,_=compile_function(fn,runtime=True,optimize=False)
                self.assertGreater(len(baseline),len(blob))
        self.assertGreater(stacked,0)

"""Shared original function exits keep typed dataflow and ABI checks (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build the original typed frontend for exit integration checks')
class FrontendEarlyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env=os.environ.copy()
        if os.name=='nt': cls.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+cls.env['PATH']
        cls.directory=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        output=Path(cls.directory.name)/'fixture.json'
        cls.functions={}
        for source,names in [(ROOT/'early_fixture.gc',[]),
                             (ROOT.parents[1]/'goal_src/jak1/kernel/gcommon.gc',
                              ['identity','basic-type?','type-type?','find-parent-method'])]:
            result=subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(source),str(output)]+names,
                                  env=cls.env,capture_output=True,text=True)
            if result.returncode: raise AssertionError(result.stdout+result.stderr)
            cls.functions.update({f['name']:f for f in json.loads(output.read_text())['functions']})

    def test_outer_exit_keeps_original_branch_indices_and_one_return(self):
        for name in ('int','loop','float','effect','object','nested','eight'):
            fn=self.functions['dc-early-'+name]
            with self.subTest(name=name):
                ops=fn['instructions']; exit=fn['shared_exit']
                self.assertEqual(exit['exit_ir'],len(ops)-1)
                self.assertEqual([i for i,op in enumerate(ops) if op['kind']=='return'],[len(ops)-1])
                self.assertEqual(ops[-1]['reads'],[exit['register']])
                self.assertTrue(any(op['kind']=='goto' and op['target']==exit['exit_ir'] for op in ops))
                self.assertTrue(compile_function(fn,runtime=True)[0])

    def test_float_exit_retains_raw_gpr_copy_and_float_return_abi(self):
        fn=self.functions['dc-early-float']
        reg=next(r for r in fn['registers'] if r['id']==fn['shared_exit']['register'])
        self.assertEqual((fn['return_type'],fn['return_abi_type']),('float','float'))
        self.assertEqual((reg['class'],reg['type']),('gpr64','float'))

    def test_original_kernel_type_and_method_functions_lower(self):
        for name in ('basic-type?','type-type?','find-parent-method'):
            with self.subTest(name=name):
                fn=self.functions[name]
                self.assertEqual(len(fn['parameters']),2)
                self.assertTrue(compile_function(fn,runtime=True)[0])
        self.assertEqual([p['name'] for p in self.functions['find-parent-method']['locals']],['current-method'])

    def test_partial_local_on_early_path_still_rejects(self):
        with self.assertRaisesRegex(ValueError,'uninitialized virtual'):
            compile_function(self.functions['dc-early-partial'],runtime=True)

    def test_missing_exit_assignment_is_not_hidden_by_shared_metadata(self):
        fn=copy.deepcopy(self.functions['dc-early-int'])
        result=fn['shared_exit']['register']
        assignment=next(op for op in fn['instructions'] if op['kind']=='move' and op['writes']==[result])
        assignment.update(kind='nop',reads=[],writes=[])
        with self.assertRaisesRegex(ValueError,'uninitialized virtual'):
            compile_function(fn,runtime=True)

    def test_normal_leaf_has_no_extra_shared_result_slot(self):
        fn=self.functions['identity']
        self.assertNotIn('shared_exit',fn)
        self.assertEqual(len(compile_function(fn,runtime=True)[0]),6)

    def test_stack_arguments_remain_eight_in_declared_order(self):
        fn=self.functions['dc-early-eight']
        self.assertEqual([p['name'] for p in fn['parameters']],list('abcdefgh'))
        self.assertEqual(fn['implicit_parameters'],[])


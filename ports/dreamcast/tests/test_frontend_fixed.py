"""Original rlet aliases and explicit native fixed-register roles (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for fixed-register integration checks')
class FrontendFixedTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env=os.environ.copy()
        if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        output=Path(cls.directory.name)/'fixture.json'
        result=subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(ROOT/'fixed_fixture.gc'),str(output)],
                              env=env,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.functions={f['name']:f for f in json.loads(output.read_text())['functions']}

    def test_known_symbol_table_role_lowers_only_in_runtime_profile(self):
        for name in ('read','low','high','alias'):
            fn=self.functions['dc-fixed-'+name]
            with self.subTest(name=name):
                binding=fn['fixed_registers'][0]
                self.assertEqual((binding['frontend_register'],binding['role']),('r14','symbol_table'))
                _,report=compile_function(fn,runtime=True)
                self.assertTrue(any(r['symbol']=='pointer:#f' for r in report['relocations']))
                with self.assertRaisesRegex(ValueError,'native runtime profile'): compile_function(fn)

    def test_identical_fixed_aliases_export_once_and_keep_their_count(self):
        fn=self.functions['dc-fixed-alias']
        self.assertEqual(len({r['id'] for r in fn['registers']}),len(fn['registers']))
        self.assertEqual(fn['register_aliases'],[{'register':fn['fixed_registers'][0]['register'],'count':2}])
        self.assertEqual(len(fn['parameters']),1)

    def test_unknown_desktop_register_and_symbol_table_writes_reject(self):
        for name,reason in [('unknown','role not implemented'),('mutation','mutation is unsupported')]:
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,reason):
                compile_function(self.functions['dc-fixed-'+name],runtime=True)

    def test_absent_reset_does_not_invent_an_initialized_runtime_value(self):
        with self.assertRaisesRegex(ValueError,'uninitialized virtual'):
            compile_function(self.functions['dc-fixed-uninitialized'],runtime=True)

    def test_reset_cannot_claim_a_different_binding(self):
        fn=copy.deepcopy(self.functions['dc-fixed-read'])
        reset=next(op for op in fn['instructions'] if op['kind']=='fixed_register_reset')
        reset['bindings'][0]['frontend_register']='r15'
        with self.assertRaisesRegex(ValueError,'does not match its declaration'): compile_function(fn,runtime=True)

    def test_duplicate_virtual_metadata_is_still_rejected_by_backend(self):
        fn=copy.deepcopy(self.functions['dc-fixed-read']); fn['registers'].append(copy.deepcopy(fn['registers'][0]))
        with self.assertRaisesRegex(ValueError,'duplicate virtual'): compile_function(fn,runtime=True)

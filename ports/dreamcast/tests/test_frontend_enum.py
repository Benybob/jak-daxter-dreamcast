"""Original enum declarations retain semantics and use declared scalar ABI (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for enum ABI checks')
class FrontendEnumTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env=os.environ.copy()
        if os.name=='nt': cls.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+cls.env['PATH']
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        cls.output=Path(cls.directory.name)/'enum.json'
        result=subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(ROOT/'enum_fixture.gc'),str(cls.output)],
                              env=cls.env,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.functions={f['name']:f for f in json.loads(cls.output.read_text())['functions']}

    def test_all_declared_integer_widths_keep_enum_names_and_underlying_signedness(self):
        for width in (8,16,32,64):
            for sign,label,abi in (('u','mask','uint'),('s','signed','int')):
                name=f'dc-enum-{sign}{width}'; fn=self.functions[name]
                with self.subTest(name=name):
                    self.assertEqual(fn['return_type'],f'dc-enum-{label}{width}')
                    self.assertEqual(fn['return_abi_type'],abi)
                    self.assertEqual(fn['parameters'][0]['abi_type'],abi)
                    self.assertEqual(fn['parameters'][0]['type'],fn['return_type'])
                    data,report=compile_function(fn,runtime=True)
                    self.assertEqual(len(data),6)
                    with self.assertRaisesRegex(ValueError,'numeric return type'): compile_function(fn)

    def test_enum_memory_load_width_and_extension_come_from_original_type_system(self):
        for width in (8,16,32,64):
            for sign in ('u','s'):
                fn=self.functions[f'dc-enum-load-{sign}{width}']
                op=next(op for op in fn['instructions'] if op['kind']=='load_memory')
                self.assertEqual((op['size'],op['signed']),(width//8,sign=='s'))
                self.assertTrue(compile_function(fn,runtime=True)[0])

    def test_enum_call_classifications_and_mixed_stack_guard(self):
        for name in ('call','mixed','mixed-call','bits32','bits64'):
            with self.subTest(name=name): self.assertTrue(compile_function(self.functions['dc-enum-'+name],runtime=True)[0])
        fn=self.functions['dc-enum-call']
        calls=[op for op in fn['instructions'] if op['kind']=='call']
        self.assertEqual([op['abi']['parameter_abi_types'] for op in calls],[['uint'],['int']])
        with self.assertRaisesRegex(ValueError,'mixed integer/float stack argument'):
            compile_function(self.functions['dc-enum-mixed-stack'],runtime=True)

    def test_unclassified_enum_and_wide_register_guards_remain_strict(self):
        fn=copy.deepcopy(self.functions['dc-enum-u32'])
        fn['return_abi_type']=fn['return_type']
        with self.assertRaisesRegex(ValueError,'numeric return type'): compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.functions['dc-enum-u64'])
        fn['registers'][0]['class']='int128'
        with self.assertRaisesRegex(ValueError,'128-bit/vector'): compile_function(fn,runtime=True)

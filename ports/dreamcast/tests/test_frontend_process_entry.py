"""Exact original implicit self entry and fail-closed ABI guards (AI-assisted)."""
import copy
import hashlib
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for process entry checks')
class FrontendProcessEntryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        env=os.environ.copy()
        if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cls.source=ROOT.parents[1]/'goal_src/jak1/kernel/gkernel.gc'
        cls.header=Path(cls.directory.name)/'entry.h'
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(cls.source),'--function','remove-exit','--runtime','--header',str(cls.header)]
        for dep in ('gcommon.gc',ROOT/'scalar_validation_context.gc','gkernel-h.gc','gstring-h.gc'):
            command+=['--preload',str(cls.source.parent/dep)]
        result=subprocess.run(command,env=env,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.doc=json.loads(cls.header.with_suffix('.ir.json').read_text())
        cls.fn=next(f for f in cls.doc['functions'] if f['name']=='remove-exit')

    def test_unchanged_behavior_has_exact_self_role_and_strict_function_import(self):
        report=json.loads(self.header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertEqual(self.fn['parameters'],[])
        self.assertEqual(len(self.fn['implicit_parameters']),1)
        self.assertEqual(self.fn['implicit_parameters'][0]['type'],'process')
        self.assertEqual(self.fn['fixed_registers'][0]['role'],'process_self')
        self.assertEqual(self.fn['instructions'][0]['kind'],'fixed_register_reset')
        self.assertEqual({r['symbol'] for r in report['functions'][0]['relocations']},
                         {'function:dc-c-current-process','pointer:#f'})
        self.assertIn(88,[o['offset'] for o in self.fn['instructions'] if o['kind']=='load_memory'])

    def test_numeric_profile_cannot_enable_implicit_entry(self):
        with self.assertRaisesRegex(ValueError,'implicit process/self'): compile_function(self.fn)

    def test_explicit_arguments_other_types_and_ambiguous_entries_remain_rejected(self):
        # Ordinary arguments and any generic return now sit beside self (behaviors
        # such as nav-mesh-connect); self aliasing an argument still refuses.
        self_register=self.fn['implicit_parameters'][0]['register']
        for field,value in [('parameters',[{'register':self_register,'type':'object','abi_type':'object'}]),
                            ('implicit_parameters',self.fn['implicit_parameters']*2)]:
            fn=copy.deepcopy(self.fn); fn[field]=value
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'implicit process/self'):
                compile_function(fn,runtime=True)
        # The exporter's type-system verdict, never a type name, makes self a process.
        self.assertIs(self.fn['implicit_parameters'][0]['process_type'],True)
        for field,value in [('name','pp'),('process_type',False),('abi_type','uint'),('kind','local')]:
            fn=copy.deepcopy(self.fn); fn['implicit_parameters'][0][field]=value
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'implicit process/self'):
                compile_function(fn,runtime=True)

    def test_unknown_or_missing_fixed_roles_remain_rejected(self):
        for value in ('unsupported','symbol_table'):
            fn=copy.deepcopy(self.fn); fn['fixed_registers'][0]['role']=value
            with self.subTest(value=value),self.assertRaisesRegex(ValueError,'role not implemented'):
                compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn); fn['fixed_registers']=[]
        with self.assertRaisesRegex(ValueError,'missing its fixed entry'): compile_function(fn,runtime=True)

    def test_resetless_and_nonentry_resets_cannot_invent_self(self):
        fn=copy.deepcopy(self.fn); fn['instructions'][0]={'kind':'nop','reads':[],'writes':[]}
        with self.assertRaisesRegex(ValueError,'explicit entry reset'): compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn); fn['instructions'].insert(0,{'kind':'nop','reads':[],'writes':[]})
        with self.assertRaisesRegex(ValueError,'must be at function entry'): compile_function(fn,runtime=True)

    def test_mutable_self_and_forged_current_process_operation_reject(self):
        fn=copy.deepcopy(self.fn)
        fn['instructions'].insert(1,{'kind':'constant','reads':[],
                                    'writes':[fn['implicit_parameters'][0]['register']],'value':0})
        with self.assertRaisesRegex(ValueError,'mutation is unsupported'): compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn); fn['instructions'][0]['kind']='current_process'
        with self.assertRaisesRegex(ValueError,'must originate'): compile_function(fn,runtime=True)

    def test_reset_binding_mismatch_and_assembly_remain_rejected(self):
        fn=copy.deepcopy(self.fn); fn['instructions'][0]['bindings'][0]['frontend_register']='r15'
        with self.assertRaisesRegex(ValueError,'does not match'): compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn); fn['asm']=True
        with self.assertRaisesRegex(ValueError,'assembly'): compile_function(fn,runtime=True)

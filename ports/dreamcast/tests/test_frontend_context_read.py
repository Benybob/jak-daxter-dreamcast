"""Original protect constructor and read-only typed pp entry guards (AI-assisted), ISC."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
from lower_sh4_ir import compile_function
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(PORT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for typed process-context tests')
class ContextReadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
        cls.source=PORT.parents[1]/'goal_src/jak1/kernel/gkernel.gc'
        cls.header=Path(cls.tmp.name)/'protect.h'
        cls.env=os.environ.copy()
        if os.name=='nt':cls.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+cls.env['PATH']
        cls.dependencies=[]
        for dep in ['gcommon.gc',PORT/'scalar_validation_context.gc','gkernel-h.gc','gstring-h.gc']:
            cls.dependencies+=['--preload',str(cls.source.parent/dep)]
        cmd=[sys.executable,str(PORT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
             '--source',str(cls.source),'--function','(method new protect-frame)',
             '--runtime','--header',str(cls.header)]+cls.dependencies
        run=subprocess.run(cmd,env=cls.env,capture_output=True,text=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.doc=json.loads(cls.header.with_suffix('.ir.json').read_text())
        cls.fn=next(f for f in cls.doc['functions'] if f['name']=='(method new protect-frame)')
        cls.index=cls.fn['fixed_registers'][0]['register']

    def test_original_constructor_exact_metadata_and_source_hash(self):
        self.assertEqual(self.fn['implicit_parameters'],[])
        self.assertEqual(len(self.fn['parameters']),3)
        self.assertEqual(self.fn['fixed_registers'],[dict(frontend_register='r13',register=self.index,role='process_context_read')])
        self.assertFalse(any(self.index in o['writes'] for o in self.fn['instructions']))
        report=json.loads(self.header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertIn('function:dc-c-current-process',{r['symbol'] for r in report['functions'][0]['relocations']})

    def test_numeric_unknown_and_wrong_physical_register_roles_refuse(self):
        with self.assertRaises(ValueError):compile_function(self.fn)
        for field,value in [('role','unsupported'),('frontend_register','r15')]:
            fn=copy.deepcopy(self.fn);fn['fixed_registers'][0][field]=value
            with self.subTest(field=field),self.assertRaisesRegex(ValueError,'role not implemented'):
                compile_function(fn,runtime=True)

    def test_mutation_reset_and_forged_getter_refuse(self):
        for op,reason in [(dict(kind='move',reads=[self.index],writes=[self.index]),'mutation'),
          (dict(kind='fixed_register_reset',reads=[],writes=[self.index],bindings=self.fn['fixed_registers']),'must not reset'),
          (dict(kind='current_process',reads=[],writes=[self.index]),'must originate')]:
            fn=copy.deepcopy(self.fn);fn['instructions'].insert(0,op)
            with self.subTest(reason=reason),self.assertRaisesRegex(ValueError,reason):compile_function(fn,runtime=True)

    def test_wrong_semantic_container_and_parameter_overlap_refuse(self):
        fn=copy.deepcopy(self.fn);next(r for r in fn['registers'] if r['id']==self.index)['type']='cpu-thread'
        with self.assertRaisesRegex(ValueError,'exact process container'):compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn);fn['parameters'][0]['register']=self.index
        with self.assertRaisesRegex(ValueError,'separate read input'):compile_function(fn,runtime=True)

    def test_ambiguous_contexts_and_wrong_abi_container_refuse(self):
        fn=copy.deepcopy(self.fn);second=max(r['id'] for r in fn['registers'])+1
        fn['registers'].append(dict(id=second,type='process',abi_type='object',**{'class':'gpr64'}))
        fn['fixed_registers'].append(dict(register=second,frontend_register='r13',role='process_context_read'))
        fn['instructions'].append(dict(kind='nop',reads=[second],writes=[]))
        with self.assertRaisesRegex(ValueError,'ambiguous read-only process context'):compile_function(fn,runtime=True)
        fn=copy.deepcopy(self.fn);next(r for r in fn['registers'] if r['id']==self.index)['abi_type']='uint'
        with self.assertRaisesRegex(ValueError,'exact process container'):compile_function(fn,runtime=True)

    def test_invalid_original_cfg_targets_are_not_made_valid_by_entry_insertion(self):
        for target in [-1,len(self.fn['instructions'])]:
            fn=copy.deepcopy(self.fn);fn['instructions'][0]=dict(kind='goto',reads=[],writes=[],target=target)
            with self.subTest(target=target),self.assertRaisesRegex(ValueError,'invalid IR branch target'):
                compile_function(fn,runtime=True)

    def test_loop_with_context_read_lowers_with_both_slot_allocators(self):
        output=Path(self.tmp.name)/'loop.json'
        run=subprocess.run([str(FRONTEND),str(PORT.parents[1]),str(PORT/'context_read_fixture.gc'),str(output)]+self.dependencies,
                           env=self.env,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        fn=next(f for f in json.loads(output.read_text())['functions'] if f['name']=='dc-context-read-loop')
        self.assertTrue(any(o['kind']=='branch' for o in fn['instructions']))
        for optimized in [False,True]:
            _,report=compile_function(fn,runtime=True,optimize=optimized)
            self.assertIn('function:dc-c-current-process',{r['symbol'] for r in report['relocations']})

    def test_original_deactivate_lowers_with_explicit_transfer(self):
        output=Path(self.tmp.name)/'deactivate.json'
        run=subprocess.run([str(FRONTEND),str(PORT.parents[1]),str(self.source),str(output)]+self.dependencies,
                           env=self.env,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        fn=next(f for f in json.loads(output.read_text())['functions'] if f['name']=='(method deactivate process)')
        # The exporter keeps the x86 r15 offset explicit; only the lowerer's exact
        # return-from-thread idiom consumes it.
        self.assertEqual({b['frontend_register']:b['role'] for b in fn['fixed_registers']},
                         {'r13':'process_lexical','r15':'unsupported'})
        _,report=compile_function(fn,runtime=True)
        symbols={r['symbol'] for r in report['relocations']}
        self.assertLessEqual({'function:dc-c-return-from-thread','function:dc-c-enter-process-scope',
                              'function:dc-c-leave-process-scope','cell:throw'},symbols)
        # Any other r15 use keeps refusing.
        bad=copy.deepcopy(fn);offset=[b['register'] for b in bad['fixed_registers'] if b['frontend_register']=='r15'][0]
        bad['instructions'].insert(1,dict(kind='nop',reads=[offset],writes=[]))
        with self.assertRaisesRegex(ValueError,'role not implemented'):compile_function(bad,runtime=True)

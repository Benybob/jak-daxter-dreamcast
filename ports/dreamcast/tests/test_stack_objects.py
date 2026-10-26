"""Independent stack reservation/metadata and original frontend checks (AI-assisted), ISC."""
import copy
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
def fixture(count=4):
    return dict(name='stack-meta',asm=False,return_type='uint',return_abi_type='uint',parameters=[],fixed_registers=[],
                registers=[dict(id=0,type='(pointer uint64)',abi_type='(pointer uint64)',**{'class':'gpr64'}),
                           dict(id=1,type='uint',abi_type='uint',**{'class':'gpr64'})],
                stack_slots_used_for_stack_vars=count,instructions=[dict(kind='stack_address',slot=0,reads=[],writes=[0]),
                dict(kind='constant',value=7,reads=[],writes=[1]),dict(kind='return',reads=[1],writes=[])])
class StackMetadataTest(unittest.TestCase):
    def lower(self,f,optimize=True):return compile_function(f,runtime=True,optimize=optimize)
    def test_capacity_is_reserved_not_inferred_from_first_address(self):
        for optimize in (True,False):
            _,a=self.lower(fixture(1),optimize);_,b=self.lower(fixture(4),optimize)
            self.assertEqual(b['stack_bytes']-a['stack_bytes'],24)
            self.assertEqual(b['stack_object_slots'],4)
            self.assertEqual(b['stack_bytes'],b['stack_object_region']+4*8+32)
    def test_missing_count_only_accepted_without_stack_addresses(self):
        f=fixture();del f['stack_slots_used_for_stack_vars']
        with self.assertRaisesRegex(ValueError,'missing stack slot count'):self.lower(f)
        f['instructions']=f['instructions'][1:]
        a,_=self.lower(f);f['stack_slots_used_for_stack_vars']=0;b,_=self.lower(f)
        self.assertEqual(a,b)
    def test_bad_counts_and_slots_refuse_before_emission(self):
        for count in (True,1.5,-1,513):
            with self.subTest(count=count),self.assertRaises(ValueError):self.lower(fixture(count))
        for slot in (-1,True,4):
            f=fixture();f['instructions'][0]['slot']=slot
            with self.subTest(slot=slot),self.assertRaises(ValueError):self.lower(f)
    def test_malformed_shapes_and_destination_types_refuse(self):
        for changes in (dict(reads=[1]),dict(reads=None),dict(writes=[]),dict(writes=[0,1]),dict(writes=[True])):
            f=fixture();f['instructions'][0].update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.lower(f)
        for changes in (dict(type='uint',abi_type='uint'),{'class':'float'}):
            f=fixture();f['registers'][0].update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.lower(f)
    def test_stack_address_cannot_write_a_fixed_runtime_register(self):
        f=fixture();f['registers'][0].update(type='uint',abi_type='uint')
        f['fixed_registers']=[dict(register=0,frontend_register='r14',role='symbol_table')]
        with self.assertRaises(ValueError):self.lower(f)
    def test_total_frame_budget_and_numeric_profile_refuse(self):
        with self.assertRaisesRegex(ValueError,'frame too large'):self.lower(fixture(510))
        with self.assertRaises(ValueError):compile_function(fixture(),runtime=False)
    def test_stack_storage_cannot_be_bypassed_by_leaf_fast_path(self):
        f=fixture();f['instructions']=f['instructions'][1:]
        _,report=self.lower(f)
        self.assertGreater(report['stack_bytes'],4*8)
        self.assertEqual(report['stack_object_slots'],4)

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend first')
class StackFrontendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
        k=PORT.parents[1]/'goal_src/jak1/kernel';env=os.environ.copy()
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cmd=[str(FRONTEND),str(PORT.parents[1]),str(PORT/'stack_objects_fixture.gc'),str(Path(cls.tmp.name)/'stack.json')]
        for dep in [k/'gcommon.gc',PORT/'scalar_validation_context.gc',k/'gkernel-h.gc',k/'gstring-h.gc',k/'gkernel.gc']:
            cmd+=['--preload',str(dep)]
        run=subprocess.run(cmd,env=env,capture_output=True,text=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.doc=json.loads((Path(cls.tmp.name)/'stack.json').read_text())
        cls.module={f['name']:f for f in cls.doc['functions']};cls.statics={s['id']:s for s in cls.doc.get('statics',[])}
    def test_original_frontend_reserves_complete_array_regions(self):
        f=self.module['dc-stack-bounds'];ops=[o for o in f['instructions'] if o['kind']=='stack_address']
        self.assertEqual(len(ops),2);self.assertEqual(f['stack_slots_used_for_stack_vars'],8)
        self.assertEqual([o['slot'] for o in ops],[0,4])
    def test_all_authored_shapes_lower_in_both_modes(self):
        names=[n for n in self.module if n.startswith('dc-stack-') or n=='(method new dc-stack-record)']
        self.assertEqual(len(names),14)
        for name in names:
            for optimize in (True,False):
                with self.subTest(name=name,optimize=optimize):
                    data,report=compile_function(copy.deepcopy(self.module[name]),self.module,True,optimize,self.statics)
                    self.assertGreater(len(data),0)
                    if self.module[name]['stack_slots_used_for_stack_vars']:
                        self.assertEqual(report['stack_object_slots'],self.module[name]['stack_slots_used_for_stack_vars'])
    def test_stack_region_and_lexical_cleanup_flag_are_separate(self):
        f=self.module['dc-stack-lexical'];_,report=compile_function(f,self.module,True,False,self.statics)
        self.assertEqual(report['stack_object_region'],len(f['registers'])*8+8)
        self.assertIn('function:dc-c-leave-process-scope',{r['symbol'] for r in report['relocations']})
    def test_original_suspend_remains_a_native_yield_with_stack_storage(self):
        f=self.module['dc-stack-suspend'];_,report=compile_function(f,self.module,True,True,self.statics)
        self.assertEqual(report['stack_object_slots'],2)
        self.assertIn('function:dc-c-suspend',{r['symbol'] for r in report['relocations']})
    def test_unselected_large_function_exports_but_selected_native_frame_refuses(self):
        source=Path(self.tmp.name)/'large.gc';out=source.with_suffix('.json')
        source.write_text("(in-package goal)\n(defun stack-small () 7)\n(defun stack-large () (let ((p (new 'stack-no-clear 'array 'uint64 513))) (set! (-> p 0) 1) (-> p 0)))\n")
        env=os.environ.copy()
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        run=subprocess.run([str(FRONTEND),str(PORT.parents[1]),str(source),str(out),'--preload',
                            str(PORT.parents[1]/'goal_src/jak1/kernel/gcommon.gc')],env=env,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        module={f['name']:f for f in json.loads(out.read_text())['functions']}
        self.assertGreaterEqual(module['stack-large']['stack_slots_used_for_stack_vars'],513)
        self.assertGreater(len(compile_function(module['stack-small'],module,True)[0]),0)
        with self.assertRaises(ValueError):compile_function(module['stack-large'],module,True)
    def test_unchanged_original_process_initializer_lowers_stack_objects(self):
        k=PORT.parents[1]/'goal_src/jak1/kernel';out=Path(self.tmp.name)/'initialize.json'
        env=os.environ.copy()
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cmd=[str(FRONTEND),str(PORT.parents[1]),str(k/'gkernel.gc'),str(out)]
        for dep in [k/'gcommon.gc',PORT/'scalar_validation_context.gc',k/'gkernel-h.gc',k/'gstring-h.gc']:
            cmd+=['--preload',str(dep)]
        run=subprocess.run(cmd,env=env,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        doc=json.loads(out.read_text());module={f['name']:f for f in doc['functions']}
        f=module['run-function-in-process'];statics={s['id']:s for s in doc.get('statics',[])}
        self.assertEqual(len([o for o in f['instructions'] if o['kind']=='stack_address']),2)
        self.assertGreaterEqual(f['stack_slots_used_for_stack_vars'],6)
        for optimize in (True,False):
            data,report=compile_function(f,module,True,optimize,statics)
            self.assertGreater(len(data),0)
            self.assertEqual(report['stack_object_slots'],f['stack_slots_used_for_stack_vars'])

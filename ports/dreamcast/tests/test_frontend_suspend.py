"""Original (suspend) idiom and native rsp/r15 roles on looping-code (AI-assisted), ISC."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
from lower_sh4_ir import compile_function, native_machine_registers, suspend_idiom
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(PORT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for suspend idiom checks')
class SuspendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
        kernel=PORT.parents[1]/'goal_src/jak1/kernel';out=Path(cls.tmp.name)/'gstate.json'
        env=os.environ.copy()
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cmd=[str(FRONTEND),str(PORT.parents[1]),str(kernel/'gstate.gc'),str(out)]
        for dep in [kernel/'gcommon.gc',PORT/'scalar_validation_context.gc',kernel/'gkernel-h.gc',
                    kernel/'gstring-h.gc',kernel/'gkernel.gc']:
            cmd+=['--preload',str(dep)]
        run=subprocess.run(cmd,env=env,capture_output=True,text=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.doc=json.loads(out.read_text())
        cls.module={f['name']:f for f in cls.doc['functions']}
        cls.statics={s['id']:s for s in cls.doc.get('statics',[])}
        cls.fn=cls.module['looping-code']

    def lower(self,fn):
        return compile_function(fn,self.module,True,True,self.statics)

    def test_original_looping_code_lowers_to_native_suspend(self):
        self.assertEqual(sorted(b['frontend_register'] for b in self.fn['fixed_registers']),['r13','r15','rsp'])
        _,report=self.lower(copy.deepcopy(self.fn))
        symbols={r['symbol'] for r in report['relocations']}
        self.assertLessEqual({'function:dc-c-suspend','function:dc-c-current-process'},symbols)
        mapped=suspend_idiom(native_machine_registers(dict(self.fn,registers=[dict(r,type=r.get('abi_type',r['type'])) for r in self.fn['registers']])))
        kinds=[o['kind'] for o in mapped['instructions']]
        self.assertEqual(kinds.count('suspend'),1);self.assertEqual(kinds.count('stack_pointer'),1)
        self.assertEqual(kinds.count('process_reread'),1);self.assertEqual(mapped['fixed_registers'],[])

    def test_suspend_near_misses_refuse(self):
        index=next(b['register'] for b in self.fn['fixed_registers'] if b['frontend_register']=='r13')
        call=next(p for p,o in enumerate(self.fn['instructions']) if o['kind']=='call' and o.get('abi',{}).get('parameters')==['cpu-thread'])
        hook=next(p for p,o in enumerate(self.fn['instructions']) if o['kind']=='load_memory' and o.get('offset')==12)
        variants=[]
        fn=copy.deepcopy(self.fn);fn['instructions'][hook]['offset']=16;variants.append(fn)
        fn=copy.deepcopy(self.fn);fn['instructions'][call]['abi']=dict(fn['instructions'][call]['abi'],parameters=['object']);variants.append(fn)
        fn=copy.deepcopy(self.fn);consts=[p for p,o in enumerate(fn['instructions'][:call]) if o['kind']=='constant']
        fn['instructions'][consts[-1]]['value']=1;variants.append(fn)
        fn=copy.deepcopy(self.fn);fn['instructions'].insert(call+1,dict(kind='move',reads=[0] if False else [index],writes=[index]));variants.append(fn)
        for number,fn in enumerate(variants):
            with self.subTest(number=number),self.assertRaises(ValueError):self.lower(fn)

    def test_original_enter_state_lowers_both_transfers(self):
        fn=self.module['enter-state']
        self.assertEqual(sorted(b['frontend_register'] for b in fn['fixed_registers']),
                         ['r13','r15','rcx','rdi','rdx','rsi','rsp'])
        _,report=self.lower(copy.deepcopy(fn))
        symbols={r['symbol'] for r in report['relocations']}
        self.assertLessEqual({'function:dc-c-enter-state-code','function:dc-c-return-from-thread',
                              'cell:return-from-thread-dead','cell:set-to-run'},symbols)
        # A different trampoline, or another use of a carg register, keeps refusing.
        bad=copy.deepcopy(fn);trampoline=next(o for o in bad['instructions'] if o.get('symbol')=='return-from-thread-dead')
        trampoline['symbol']='return-from-thread'
        with self.assertRaisesRegex(ValueError,'role not implemented'):self.lower(bad)
        bad=copy.deepcopy(fn);carg=next(b['register'] for b in bad['fixed_registers'] if b['frontend_register']=='rdi')
        bad['instructions'].append(dict(kind='nop',reads=[carg],writes=[]))
        with self.assertRaisesRegex(ValueError,'role not implemented'):self.lower(bad)

    def test_written_r15_and_non_reset_rsp_keep_refusing(self):
        for name in ('r15','rsp'):
            fn=copy.deepcopy(self.fn);index=next(b['register'] for b in fn['fixed_registers'] if b['frontend_register']==name)
            fn['instructions'].insert(1,dict(kind='constant',value=5,reads=[],writes=[index]))
            with self.subTest(register=name),self.assertRaisesRegex(ValueError,'role not implemented'):self.lower(fn)

if __name__=='__main__':
    unittest.main()

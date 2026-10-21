"""Native shift encodings and actual x86 frontend-contract oracle (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from sh4_backend import Emitter
from shift_sh4 import emit_shift64
from lower_sh4_ir import compile_function
class ShiftTest(unittest.TestCase):
    def test_shift_return_leaves_use_no_frame_except_incoming_stack_operands(self):
        fn={'name':'shift-leaf','return_type':'int',
            'registers':[{'id':i,'class':'gpr64','type':'int'} for i in range(3)],
            'parameters':[{'register':i,'type':'int'} for i in range(3)],
            'instructions':[{'kind':'shl64','shift':32,'reads':[0],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]}
        data,report=compile_function(fn)
        self.assertEqual(len(data),6)
        self.assertEqual(report['stack_bytes'],0)
        fn['instructions'][0]=dict(kind='shlv64',reads=[0,2],writes=[0])
        self.assertGreater(compile_function(fn)[1]['stack_bytes'],0)
        fn['instructions'][0]=dict(kind='shl64',shift=0,reads=[0],writes=[0])
        self.assertEqual(compile_function(fn)[1]['bytes'],6)
        fn['return_type']='float'
        fn['registers'][0].update(type='float',**{'class':'float'})
        fn['parameters'][0]['type']='float'; fn['parameters']=fn['parameters'][:1]
        with self.assertRaisesRegex(ValueError,'integer virtual'): compile_function(fn)
    def test_real_x86_variable_shift_count_contract(self):
        result=subprocess.run([str(ROOT/'build'/('shift-check.exe' if os.name=='nt' else 'shift-check'))],
                              text=True,capture_output=True)
        if result.returncode==77: self.skipTest('x86-64 shift oracle unavailable')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout),{'tests':1659,'passed':1659})

    def test_constant_count_paths_are_compact_and_validate_metadata(self):
        e=Emitter(); emit_shift64(e,'left',32)
        self.assertEqual(len(e.finish()[0]),4)
        e=Emitter(); emit_shift64(e,'left')
        self.assertGreater(len(e.finish()[0]),4)
        for count in (-1,256,1.5):
            with self.assertRaisesRegex(ValueError,'unsigned-byte'): emit_shift64(Emitter(),'left',count)
        with self.assertRaises(ValueError): emit_shift64(Emitter(),'rotate',1)

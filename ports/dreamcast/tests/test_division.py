"""Actual x86 division oracle and native fault dependency validation (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lower_sh4_ir import compile_function
class DivisionTest(unittest.TestCase):
    def test_actual_x86_signed_unsigned_low32_extension_contract(self):
        result=subprocess.run([str(ROOT/'build'/('division-check.exe' if os.name=='nt' else 'division-check'))],text=True,capture_output=True)
        if result.returncode==77: self.skipTest('x86-64 division oracle unavailable')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout),{'tests':838,'passed':838})

    def test_runtime_fault_bridge_is_a_required_link_dependency(self):
        fn={'name':'division','return_type':'int','registers':[{'id':i,'class':'gpr64','type':'int'} for i in range(2)],
            'parameters':[{'register':i,'type':'int'} for i in range(2)],
            'instructions':[{'kind':'idiv32','reads':[0,1],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]}
        with self.assertRaisesRegex(ValueError,'runtime fault bridge'): compile_function(fn)
        _,report=compile_function(fn,runtime=True)
        self.assertIn('function:dc-c-integer-error',[r['symbol'] for r in report['relocations']])
        fn['instructions'][0]['reads']=[0]
        with self.assertRaisesRegex(ValueError,'operand layout'): compile_function(fn,runtime=True)

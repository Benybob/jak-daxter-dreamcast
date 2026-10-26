"""Original x86 return-from-thread idiom and void phantom copies (AI-assisted), ISC."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import compile_function, return_from_thread_idiom

def transfer():
    """Shape of deactivate's self branch: address, moves, +offset, push, ret."""
    regs=[{'id':i,'class':'gpr64','type':'uint','abi_type':'uint'} for i in range(5)]
    return {'name':'dc-transfer','asm':False,'return_type':'none','return_abi_type':'none',
            'registers':regs,'parameters':[],
            'fixed_registers':[{'register':4,'frontend_register':'r15','role':'unsupported'}],
            'instructions':[
                {'kind':'symbol_value','symbol':'return-from-thread','reads':[],'writes':[0]},
                {'kind':'move','reads':[0],'writes':[1]},
                {'kind':'move','reads':[1],'writes':[2]},
                {'kind':'add64','reads':[2,4],'writes':[2]},
                {'kind':'move','reads':[2],'writes':[1]},
                {'kind':'unsupported','reads':[1],'writes':[],'text':'.push igpr-1'},
                {'kind':'unsupported','reads':[],'writes':[],'text':'.ret'},
                {'kind':'return','reads':[],'writes':[]}]}

class ReturnFromThreadTest(unittest.TestCase):
    def test_exact_idiom_becomes_one_explicit_transfer(self):
        fn=return_from_thread_idiom(transfer())
        self.assertEqual(fn['fixed_registers'],[])
        self.assertEqual([o['kind'] for o in fn['instructions']].count('return_from_thread'),1)
        self.assertNotIn('unsupported',[o['kind'] for o in fn['instructions']])
        _,report=compile_function(transfer(),runtime=True)
        self.assertIn('function:dc-c-return-from-thread',{r['symbol'] for r in report['relocations']})

    def test_numeric_profile_never_rewrites(self):
        with self.assertRaises(ValueError):compile_function(transfer())

    def test_near_misses_stay_refused(self):
        cases=[]
        fn=transfer();fn['instructions'][0]['symbol']='other-function';cases.append(fn)
        fn=transfer();fn['instructions'][6]['text']='.nop';cases.append(fn)
        fn=transfer();fn['instructions'][3]['reads']=[2,3];cases.append(fn)
        fn=transfer();del fn['instructions'][3];cases.append(fn)
        fn=transfer();fn['instructions'].insert(7,{'kind':'move','reads':[4],'writes':[3]});cases.append(fn)
        fn=transfer();fn['instructions'].insert(0,{'kind':'goto','reads':[],'writes':[],'target':3});cases.append(fn)
        fn=transfer();fn['instructions'].insert(2,{'kind':'call','reads':[0],'writes':[]});cases.append(fn)
        for index,fn in enumerate(cases):
            with self.subTest(index=index):
                self.assertIs(return_from_thread_idiom(fn),fn)
                with self.assertRaises(ValueError):compile_function(fn,runtime=True)

    def test_unread_void_phantom_copy_is_dead_but_read_copy_is_not(self):
        fn={'name':'dc-phantom','asm':False,'return_type':'int','return_abi_type':'int',
            'registers':[{'id':0,'class':'gpr64','type':'none','abi_type':'none'},
                         {'id':1,'class':'gpr64','type':'object','abi_type':'object'},
                         {'id':2,'class':'gpr64','type':'int','abi_type':'int'}],
            'parameters':[{'name':'x','register':2,'type':'int','abi_type':'int'}],
            'instructions':[{'kind':'move','reads':[0],'writes':[1]},
                            {'kind':'return','reads':[2],'writes':[]}]}
        compile_function(copy.deepcopy(fn),runtime=True)
        fn['instructions'].insert(1,{'kind':'move','reads':[1],'writes':[2]})
        with self.assertRaisesRegex(ValueError,'uninitialized'):compile_function(fn,runtime=True)

if __name__=='__main__':
    unittest.main()

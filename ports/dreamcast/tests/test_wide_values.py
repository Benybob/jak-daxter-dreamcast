"""128-bit values, variadic integer slots and native bindings (AI-assisted), ISC."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import compile_function, compile_module

def wide_copy():
    """qmem-copy shape: load a quadword through one pointer, store through another."""
    return {'name':'dc-wide-copy','asm':False,'return_type':'int','return_abi_type':'int',
            'registers':[{'id':0,'class':'gpr64','type':'pointer','abi_type':'pointer'},
                         {'id':1,'class':'gpr64','type':'pointer','abi_type':'pointer'},
                         {'id':2,'class':'int128','type':'uint128','abi_type':'uint128'},
                         {'id':3,'class':'gpr64','type':'int','abi_type':'int'}],
            'parameters':[{'name':'dst','register':0,'type':'pointer','abi_type':'pointer'},
                          {'name':'src','register':1,'type':'pointer','abi_type':'pointer'}],
            'instructions':[{'kind':'load_memory','reads':[1],'writes':[2],'size':16,'offset':0,'signed':False,'memory_class':'int128'},
                            {'kind':'store_memory','reads':[2,0],'writes':[],'size':16,'offset':16},
                            {'kind':'constant','value':1,'reads':[],'writes':[3]},
                            {'kind':'return','reads':[3],'writes':[]}]}

class WideValueTest(unittest.TestCase):
    def test_quadword_copy_lowers_and_refuses_class_mismatch(self):
        data,report=compile_function(wide_copy(),runtime=True)
        self.assertGreater(len(data),0)
        self.assertGreaterEqual(report['stack_bytes'],16)
        with self.assertRaises(ValueError):compile_function(wide_copy())  # numeric profile
        fn=wide_copy();fn['instructions'][0]['memory_class']='gpr64'
        with self.assertRaisesRegex(ValueError,'class mismatch'):compile_function(fn,runtime=True)
        fn=wide_copy();fn['registers'][2]['class']='gpr64';fn['registers'][2]['type']=fn['registers'][2]['abi_type']='int'
        with self.assertRaisesRegex(ValueError,'width/offset'):compile_function(fn,runtime=True)

    def test_pack_operations_and_unknown_wide_classes(self):
        for op in ('.pcpyld','.pcpyud'):
            fn=wide_copy()
            fn['instructions'].insert(1,{'kind':'unsupported','reads':[2,2],'writes':[2],'text':op+' ii128-2, ii128-2, ii128-2'})
            with self.subTest(op=op):self.assertGreater(len(compile_function(fn,runtime=True)[0]),0)
        fn=wide_copy();fn['instructions'].insert(1,{'kind':'unsupported','reads':[2,2],'writes':[2],'text':'.pextlw ii128-2, ii128-2, ii128-2'})
        with self.assertRaisesRegex(ValueError,'unsupported typed IR'):compile_function(fn,runtime=True)
        fn=wide_copy();fn['registers'][2]['class']='vf128'
        with self.assertRaisesRegex(ValueError,'128-bit/vector'):compile_function(fn,runtime=True)

    def test_128_bit_values_cannot_enter_64_bit_arithmetic(self):
        fn=wide_copy();fn['instructions'].insert(1,{'kind':'add64','reads':[2,3],'writes':[3]})
        with self.assertRaises(ValueError):compile_function(fn,runtime=True)

    def test_128_bit_parameter_and_return_convention(self):
        fn={'name':'dc-wide-identity','asm':False,'return_type':'vec4s','return_abi_type':'vec4s',
            'registers':[{'id':0,'class':'int128','type':'vec4s','abi_type':'vec4s'}],
            'parameters':[{'name':'v','register':0,'type':'vec4s','abi_type':'vec4s'}],
            'instructions':[{'kind':'return','reads':[0],'writes':[]}]}
        self.assertGreater(len(compile_function(fn,runtime=True)[0]),0)
        five=copy.deepcopy(fn)
        five['registers']+=[{'id':i,'class':'int128','type':'vec4s','abi_type':'vec4s'} for i in range(1,5)]
        five['parameters']+=[{'name':'v%d'%i,'register':i,'type':'vec4s','abi_type':'vec4s'} for i in range(1,5)]
        with self.assertRaisesRegex(ValueError,'integer argument positions'):compile_function(five,runtime=True)

    def test_native_bindings_replace_uncompilable_function_objects(self):
        top={'name':'top-level','top_level':True,'asm':False,'return_type':'object','return_abi_type':'object',
             'registers':[{'id':0,'class':'gpr64','type':'function','abi_type':'function'}],'parameters':[],
             'instructions':[{'kind':'function_address','function':'asm-thing','reads':[],'writes':[0]},
                             {'kind':'set_symbol','symbol':'asm-thing','reads':[0],'writes':[]},
                             {'kind':'return','reads':[0],'writes':[]}]}
        asm={'name':'asm-thing','asm':True,'return_type':'none','registers':[],'parameters':[],'instructions':[]}
        doc={'format_version':1,'functions':[asm,top]}
        with self.assertRaisesRegex(ValueError,'unresolved function object'):
            compile_module(copy.deepcopy(doc),[],runtime=True,initializer='boot')
        out=compile_module(copy.deepcopy(doc),[],runtime=True,initializer='boot',native_bindings={'asm-thing':'dc-c-native'})
        self.assertIn('function:dc-c-native',{r['symbol'] for _,report in out.values() for r in report['relocations']})
        with self.assertRaisesRegex(ValueError,'also compiled'):
            compile_module(copy.deepcopy(doc),['asm-thing'],runtime=True,initializer='boot',native_bindings={'asm-thing':'x'})
        with self.assertRaisesRegex(ValueError,'absent original'):
            compile_module(copy.deepcopy(doc),[],runtime=True,initializer='boot',native_bindings={'missing':'x'})
        with self.assertRaisesRegex(ValueError,'runtime profile'):
            compile_module(copy.deepcopy(doc),[],runtime=False,native_bindings={'asm-thing':'x'})

if __name__=='__main__':
    unittest.main()

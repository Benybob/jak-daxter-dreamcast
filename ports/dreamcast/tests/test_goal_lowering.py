"""Verify that unsupported typed GOAL code cannot silently become native code."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import compile_function,compile_module,module_data,allocate_slots

def leaf():
    return {'name':'identity','asm':False,'return_type':'int',
            'registers':[{'id':0,'class':'gpr64','type':'int'}],
            'parameters':[{'name':'x','register':0,'type':'int'}],
            'instructions':[{'kind':'return','reads':[0],'writes':[]}]}

def linked_caller():
    fn=leaf(); fn['name']='caller'
    fn['registers'] += [{'id':1,'class':'gpr64','type':'(function int int)'},
                        {'id':2,'class':'gpr64','type':'int'}]
    fn['instructions']=[{'kind':'symbol_value','symbol':'identity','reads':[],'writes':[1]},
                        {'kind':'call','reads':[1,0],'writes':[2]},
                        {'kind':'return','reads':[2],'writes':[]}]
    return fn

class LinkedModuleTest(unittest.TestCase):
    def test_user_reference_types_use_original_abi_metadata(self):
        fn=leaf(); fn['return_type']='user-reference'; fn['return_abi_type']='object'
        fn['registers'][0].update(type='user-reference',abi_type='object')
        fn['parameters'][0].update(type='user-reference',abi_type='object')
        _,report=compile_function(fn,runtime=True)
        self.assertEqual(report['bytes'],6)
        del fn['return_abi_type']
        with self.assertRaisesRegex(ValueError,'return type'): compile_function(fn,runtime=True)

    def test_empty_no_result_ir_is_a_native_leaf(self):
        fn=leaf(); fn.update(return_type='none',instructions=[])
        data,report=compile_function(fn,runtime=True)
        self.assertEqual(data,b'\x0b\x00\x09\x00')
        self.assertEqual(report['stack_bytes'],0)
        fn['instructions']=[{'kind':'nop','reads':[],'writes':[]}]
        self.assertEqual(compile_function(fn,runtime=True)[0],data)
    def test_no_result_functions_and_startup_require_runtime_profile(self):
        fn=leaf(); fn['return_type']='none'; fn['parameters']=[]; fn['registers']=[]
        fn['instructions']=[{'kind':'nop','reads':[],'writes':[]}]
        doc={'format_version':1,'functions':[dict(fn,top_level=True)]}
        result=compile_module(doc,[],runtime=True,initializer='startup')
        self.assertIn('startup',result)
        with self.assertRaisesRegex(ValueError,'numeric return'): compile_function(fn)
        fn['instructions']=[{'kind':'return','reads':[0],'writes':[]}]
        fn['parameters']=leaf()['parameters']; fn['registers']=leaf()['registers']
        with self.assertRaisesRegex(ValueError,'return value'): compile_function(fn,runtime=True)

    def test_no_result_call_cannot_define_or_read_a_phantom_value(self):
        fn=linked_caller()
        fn['instructions'][1]['abi']={'parameters':['int'],'return_type':'none'}
        fn['instructions'][1]['writes']=[]
        with self.assertRaisesRegex(ValueError,'uninitialized'): compile_function(fn,runtime=True)
        fn['instructions'][-1]['reads']=[0]
        compile_function(fn,runtime=True)
        fn['instructions'][1]['writes']=[2]
        with self.assertRaisesRegex(ValueError,'result layout'): compile_function(fn,runtime=True)

    def test_value_returning_function_cannot_fall_through_a_branch(self):
        fn=leaf()
        fn['instructions']=[{'kind':'branch','reads':[0,0],'writes':[],
                             'target':2,'condition':'eq','signed':True},
                            {'kind':'return','reads':[0],'writes':[]},
                            {'kind':'nop','reads':[],'writes':[]}]
        with self.assertRaisesRegex(ValueError,'fallthrough'): compile_function(fn)

    def test_stack_slots_reuse_dying_values_without_overwriting_live_results(self):
        fn=leaf()
        fn['registers']=[{'id':i,'class':'gpr64','type':'int'} for i in range(4)]
        fn['parameters']=[{'name':str(i),'register':i,'type':'int'} for i in range(2)]
        fn['instructions']=[{'kind':'add64','reads':[0,1],'writes':[2]},
                            {'kind':'constant','reads':[],'writes':[3],'value':99},
                            {'kind':'return','reads':[2],'writes':[]}]
        slots=allocate_slots(fn)
        self.assertNotEqual(slots[0],slots[1])
        self.assertNotEqual(slots[2],slots[3])
        self.assertEqual(len(set(slots.values())),2)

    def test_stack_slot_liveness_includes_loop_backedges(self):
        fn=leaf(); fn['registers']=[{'id':i,'class':'gpr64','type':'int'} for i in range(4)]
        fn['parameters']=[{'name':str(i),'register':i,'type':'int'} for i in range(2)]
        fn['instructions']=[{'kind':'constant','reads':[],'writes':[2],'value':0},
            {'kind':'branch','reads':[0,2],'writes':[],'condition':'le','signed':True,'target':6},
            {'kind':'add64','reads':[2,1],'writes':[2]},
            {'kind':'constant','reads':[],'writes':[3],'value':1},
            {'kind':'sub64','reads':[0,3],'writes':[0]},
            {'kind':'goto','reads':[],'writes':[],'target':1},
            {'kind':'return','reads':[2],'writes':[]}]
        slots=allocate_slots(fn)
        self.assertEqual(len(set(slots.values())),4)

    def test_static_pair_graph_closure_sharing_and_cycles(self):
        fn=leaf(); fn['parameters']=[]; fn['return_type']='pair'
        fn['instructions']=[{'kind':'static_address','static_id':'root','reads':[],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]
        doc={'format_version':1,'functions':[fn],'statics':[
            {'id':'text','kind':'string','text_bytes':[65]},
            {'id':'root','kind':'pair','car':{'kind':'data','target':'text'},
             'cdr':{'kind':'data','target':'root'}}]}
        output=compile_module(doc,['identity'],runtime=True)
        data=module_data(doc,output)
        self.assertEqual([d['name'] for d in data],['text','root'])
        self.assertEqual(data[1]['address_offset'],2)
        self.assertEqual(data[1]['data'],b'\0'*8)
        self.assertEqual(data[1]['relocations'],[{'offset':0,'symbol':'data:text'},
                                                {'offset':4,'symbol':'data:root'}])
        doc['statics'][1]['cdr']['target']='missing'
        with self.assertRaisesRegex(ValueError,'missing referenced'): compile_module(doc,['identity'],runtime=True)
        doc['statics'][1]['cdr']={'kind':'function','target':'not-compiled'}
        with self.assertRaisesRegex(ValueError,'unresolved static function'): compile_module(doc,['identity'],runtime=True)

    def test_typed_dynamic_call_requires_function_value_and_matching_arity(self):
        fn=linked_caller()
        fn['instructions'][1]['abi']={'parameters':['int'],'return_type':'int'}
        doc={'format_version':1,'functions':[fn]}
        self.assertGreater(len(compile_module(doc,['caller'],runtime=True)['caller'][0]),0)
        fn['registers'][1]['type']='int'
        with self.assertRaisesRegex(ValueError,'function-typed'): compile_module(doc,['caller'],runtime=True)
        fn['registers'][1]['type']='(function int int)'
        fn['instructions'][1]['abi']['parameters']=[]
        with self.assertRaisesRegex(ValueError,'argument count'): compile_module(doc,['caller'],runtime=True)

    def test_stack_argument_return_does_not_use_register_leaf_shortcut(self):
        fn=leaf()
        fn['registers']=[{'id':i,'class':'gpr64','type':'int'} for i in range(3)]
        fn['parameters']=[{'name':str(i),'register':i,'type':'int'} for i in range(3)]
        fn['instructions']=[{'kind':'return','reads':[2],'writes':[]}]
        _,report=compile_function(fn)
        self.assertNotEqual(report['optimization'],'coalesced_argument_return')
        self.assertGreater(report['stack_bytes'],0)

    def test_static_string_preserves_raw_bytes_and_original_layout(self):
        fn=leaf(); fn['parameters']=[]; fn['return_type']='string'
        fn['instructions']=[{'kind':'static_address','static_id':'text','reads':[],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]
        doc={'format_version':1,'functions':[fn],
             'statics':[{'id':'unused','kind':'unsupported'},
                        {'id':'text','kind':'string','text_bytes':[65,0,66]}]}
        objects=compile_module(doc,['identity'],runtime=True)
        self.assertEqual(objects['identity'][1]['relocations'][0]['symbol'],'data:text')
        self.assertEqual(module_data(doc,objects),[{'name':'text','data':b'\0\0\0\0\x03\0\0\0A\0B\0',
                          'address_offset':4,'relocations':[{'offset':0,'symbol':'type:string'}]}])
        doc['statics'][1]['kind']='unsupported'
        with self.assertRaisesRegex(ValueError,'unsupported/missing static'): compile_module(doc,['identity'],runtime=True)

    def test_float_literal_accepts_only_exact_32_bit_scalar_payloads(self):
        fn=leaf(); fn['parameters']=[]; fn['return_type']='float'
        fn['registers'][0].update(type='float',**{'class':'float'})
        fn['instructions']=[{'kind':'float_constant','value_bits':0x80000000,'reads':[],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]
        data,_=compile_function(fn)
        self.assertIn(b'\0\0\0\x80',data)
        fn['instructions'][0]['value_bits']=1<<32
        with self.assertRaisesRegex(ValueError,'scalar float constant'): compile_function(fn)

    def test_original_top_level_body_and_function_object_relocation(self):
        startup={'name':'top-level','top_level':True,'return_type':'(function int int)',
                 'registers':[{'id':0,'class':'gpr64','type':'(function int int)'}],
                 'parameters':[],
                 'instructions':[{'kind':'function_address','function':'identity','reads':[],'writes':[0]},
                                 {'kind':'set_symbol','symbol':'identity','reads':[0],'writes':[]},
                                 {'kind':'return','reads':[0],'writes':[]}]}
        doc={'format_version':1,'functions':[leaf(),startup]}; original=copy.deepcopy(doc)
        output=compile_module(doc,['identity'],runtime=True,initializer='module-startup')
        self.assertEqual(doc,original)
        self.assertEqual(output['module_startup'][1]['name'],'module-startup')
        self.assertEqual({r['symbol'] for r in output['module_startup'][1]['relocations']},
                         {'function:identity','cell:identity'})
        with self.assertRaisesRegex(ValueError,'unresolved function object'):
            compile_module(doc,[],runtime=True,initializer='module-startup')
        with self.assertRaisesRegex(ValueError,'requires native runtime'):
            compile_module(doc,['identity'],initializer='module-startup')

    def test_initializer_selection_rejects_missing_ambiguous_or_parameterized_entry(self):
        fn=leaf(); fn['top_level']=True
        doc={'format_version':1,'functions':[fn]}
        with self.assertRaisesRegex(ValueError,'nonempty and unique'):
            compile_module(doc,['identity'],runtime=True,initializer='identity')
        with self.assertRaisesRegex(ValueError,'zero-argument'):
            compile_module(doc,['identity'],runtime=True,initializer='startup')
        fn['parameters']=[]
        other=copy.deepcopy(fn); other['name']='other'; doc['functions'].append(other)
        with self.assertRaisesRegex(ValueError,'one original'):
            compile_module(doc,['identity'],runtime=True,initializer='startup')
        doc['functions']=[]
        with self.assertRaisesRegex(ValueError,'one original'):
            compile_module(doc,[],runtime=True,initializer='startup')

    def test_runtime_calls_relocate_mutable_cells(self):
        output=compile_module({'format_version':1,'functions':[leaf(),linked_caller()]},
                              ['identity','caller'],runtime=True)
        self.assertEqual(output['caller'][1]['relocations'][0]['symbol'],'cell:identity')

    def test_argument_leaf_coalesces_copies_and_uses_delay_slot(self):
        fn=leaf()
        fn['registers'].append({'id':1,'class':'gpr64','type':'int'})
        fn['instructions']=[{'kind':'move','reads':[0],'writes':[1]},
                            {'kind':'return','reads':[1],'writes':[]}]
        optimized,report=compile_function(fn)
        baseline,old=compile_function(fn,optimize=False)
        self.assertEqual(optimized,bytes.fromhex('43600b005361'))
        self.assertEqual(report['stack_bytes'],0)
        self.assertEqual(report['minimum_32_byte_cache_lines'],1)
        self.assertGreater(len(baseline),len(optimized))
        self.assertGreater(old['stack_bytes'],0)

    def test_float_argument_leaf_uses_gcc_register_order(self):
        fn=leaf(); fn['return_type']='float'
        fn['parameters'][0]['type']='float'
        fn['registers'][0].update(type='float',**{'class':'float'})
        data,report=compile_function(fn)
        self.assertEqual(data,bytes.fromhex('0b005cf0'))
        self.assertEqual(report['stack_bytes'],0)

    def test_leaf_does_not_bypass_side_effects_or_abi_limits(self):
        fn=leaf()
        fn['instructions'].insert(0,{'kind':'set_symbol','symbol':'counter','reads':[0],'writes':[]})
        _,report=compile_function(fn,runtime=True)
        self.assertNotEqual(report['optimization'],'coalesced_argument_return')
        fn=leaf()
        fn['parameters']*=9
        with self.assertRaisesRegex(ValueError,'eight arguments'): compile_function(fn)

    def test_selected_module_resolves_only_declared_function_dependencies(self):
        doc={'format_version':1,'functions':[leaf(),linked_caller()]}
        with self.assertRaisesRegex(ValueError,'unresolved symbol'):
            compile_module(doc,['caller'])
        output=compile_module(doc,['identity','caller'])
        data,report=output['caller']
        self.assertEqual(len(report['relocations']),1)
        relocation=report['relocations'][0]
        self.assertEqual(relocation['symbol'],'identity')
        self.assertLessEqual(relocation['offset']+4,len(data))
        self.assertEqual(data[relocation['offset']:relocation['offset']+4],b'\0'*4)

    def test_raw_or_overwritten_function_addresses_are_rejected(self):
        for overwrite in (False,True):
            fn=linked_caller()
            value={'kind':'constant','value':0x8c010000,'reads':[],'writes':[1]}
            if overwrite: fn['instructions'].insert(1,value)
            else: fn['instructions'][0]=value
            with self.assertRaisesRegex(ValueError,'immutable linked function'):
                compile_module({'format_version':1,'functions':[leaf(),fn]},['identity','caller'])

    def test_call_argument_count_is_checked_against_linked_definition(self):
        fn=linked_caller(); fn['instructions'][1]['reads']=[1]
        with self.assertRaisesRegex(ValueError,'argument count'):
            compile_module({'format_version':1,'functions':[leaf(),fn]},['identity','caller'])

class UnsupportedGoalTest(unittest.TestCase):
    def test_memory_access_rejects_unknown_width_and_register_mismatch(self):
        fn=leaf(); fn['instructions'].insert(0,{'kind':'load_memory','reads':[0],'writes':[0],
                             'size':16,'offset':0,'signed':False,'memory_class':'gpr64'})
        with self.assertRaisesRegex(ValueError,'requires native runtime'): compile_function(fn)
        with self.assertRaisesRegex(ValueError,'width/offset'): compile_function(fn,runtime=True)
        fn['instructions'][0]['size']=4; fn['instructions'][0]['memory_class']='float'
        with self.assertRaisesRegex(ValueError,'class mismatch'): compile_function(fn,runtime=True)
        fn['instructions'][0]['memory_class']='gpr64'; fn['instructions'][0]['offset']=1<<32
        with self.assertRaisesRegex(ValueError,'width/offset'): compile_function(fn,runtime=True)

    def test_entry_argument_is_distinct_from_mutable_local_copy(self):
        fn=leaf()
        fn['registers'].append({'id':1,'class':'gpr64','type':'int'})
        fn['parameters'][0]['register']=1
        fn['instructions']=[{'kind':'nop','reads':[],'writes':[0]},
                            {'kind':'move','reads':[0],'writes':[1]},
                            {'kind':'return','reads':[1],'writes':[]}]
        with self.assertRaisesRegex(ValueError,'uninitialized'): compile_function(fn)
        fn['parameters'][0]['register']=0
        data,_=compile_function(fn)
        self.assertGreater(len(data),0)

    def test_value_must_be_initialized_on_both_branch_paths(self):
        fn=leaf(); fn['registers'].append({'id':1,'class':'gpr64','type':'int'})
        fn['instructions']=[{'kind':'branch','reads':[0,0],'writes':[],
                             'condition':'eq','signed':True,'target':2},
                            {'kind':'constant','reads':[],'writes':[1],'value':42},
                            {'kind':'return','reads':[1],'writes':[]}]
        with self.assertRaisesRegex(ValueError,'uninitialized'): compile_function(fn)

    def test_unsupported_ir_does_not_generate_fallback(self):
        for kind in ('unsupported','not-a-real-op'):
            fn=leaf()
            fn['instructions'].insert(0,{'kind':kind,'reads':[0],'writes':[0]})
            with self.assertRaisesRegex(ValueError,'unsupported typed IR operation'):
                compile_function(fn)

    def test_live_symbol_requires_a_runtime(self):
        fn=leaf()
        fn['instructions'].insert(0,{'kind':'symbol_pointer','symbol':'#f','reads':[],'writes':[0]})
        with self.assertRaisesRegex(ValueError,'live GOAL symbol'):
            compile_function(fn)

    def test_vector_and_assembly_require_explicit_implementation(self):
        fn=leaf(); fn['asm']=True
        with self.assertRaisesRegex(ValueError,'assembly'): compile_function(fn)
        fn=leaf(); fn['registers'][0]['class']='vector_float'
        with self.assertRaisesRegex(ValueError,'vector'): compile_function(fn)

    def test_unsupported_abi_does_not_guess_argument_or_result(self):
        fn=leaf(); fn['parameters'][0]['type']='basic'
        with self.assertRaisesRegex(ValueError,'numeric parameters'): compile_function(fn)
        fn=leaf(); fn['return_type']='symbol'
        with self.assertRaisesRegex(ValueError,'numeric return'): compile_function(fn)
        fn=leaf(); fn['instructions']=[]
        with self.assertRaisesRegex(ValueError,'no return'): compile_function(fn)
        fn=leaf(); fn['parameters'].append({'name':'f','register':0,'type':'float'})
        with self.assertRaisesRegex(ValueError,'mixed integer/float'): compile_function(fn)

    def test_module_selection_rejects_ambiguity(self):
        fn=leaf()
        with self.assertRaisesRegex(ValueError,'duplicate function'):
            compile_module({'format_version':1,'functions':[fn,copy.deepcopy(fn)]},['identity'])
        with self.assertRaisesRegex(ValueError,'missing function'):
            compile_module({'format_version':1,'functions':[fn]},['absent'])
        with self.assertRaisesRegex(ValueError,'format'):
            compile_module({'format_version':2,'functions':[fn]},['identity'])

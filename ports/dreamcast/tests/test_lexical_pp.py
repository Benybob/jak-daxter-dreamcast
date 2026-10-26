"""Lexical pp bracket lowering on synthetic original-shaped IR (AI-assisted), ISC."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import compile_function

BINDING={'register':1,'frontend_register':'r13','role':'process_lexical'}

def bracket():
    """Shape of original send-event-function: save pp, select proc, call, restore."""
    return {'name':'dc-lexical','asm':False,'return_type':'object','return_abi_type':'object',
            'registers':[{'id':0,'class':'gpr64','type':'process','abi_type':'object'},
                         {'id':1,'class':'gpr64','type':'process','abi_type':'object'},
                         {'id':2,'class':'gpr64','type':'process','abi_type':'object'},
                         {'id':3,'class':'gpr64','type':'(function object object)','abi_type':'(function object object)'},
                         {'id':4,'class':'gpr64','type':'object','abi_type':'object'}],
            'parameters':[{'name':'proc','register':0,'type':'process','abi_type':'object'}],
            'fixed_registers':[dict(BINDING)],
            'instructions':[
                {'kind':'fixed_register_reset','reads':[],'writes':[1],'bindings':[dict(BINDING)]},
                {'kind':'move','reads':[1],'writes':[2]},
                {'kind':'move','reads':[0],'writes':[1]},
                {'kind':'symbol_value','symbol':'dc-hook','reads':[],'writes':[3]},
                {'kind':'call','reads':[3,1],'writes':[4],'abi':{'parameters':['object'],'return_type':'object'}},
                {'kind':'move','reads':[2],'writes':[1]},
                {'kind':'return','reads':[4],'writes':[]}]}

def symbols(report):
    return [r['symbol'] for r in report['relocations']]

class LexicalProcessBracketTest(unittest.TestCase):
    def test_original_shaped_bracket_selects_and_restores_through_guarded_bridges(self):
        for optimize in (True,False):
            with self.subTest(optimize=optimize):
                data,report=compile_function(bracket(),runtime=True,optimize=optimize)
                names=symbols(report)
                self.assertGreater(len(data),0)
                self.assertEqual(names.count('function:dc-c-current-process'),1)
                self.assertEqual(names.count('function:dc-c-enter-process-scope'),1)
                # Normal and call-fault restores share one pooled literal.
                self.assertEqual(names.count('function:dc-c-leave-process-scope'),1)
                self.assertIn('function:dc-c-call-error',names)
                # The fault-path restore and the dedicated scope flag cost bytes.
                fn=bracket();fn['instructions'][4]=dict(fn['instructions'][4],kind='nop',writes=[],reads=[])
                fn['instructions'][6]['reads']=[0];fn['registers'][4]['type']=fn['registers'][4]['abi_type']='object'
                _,unguarded=compile_function(fn,runtime=True,optimize=optimize)
                self.assertNotIn('function:dc-c-call-error',symbols(unguarded))
                self.assertGreater(report['bytes'],unguarded['bytes'])

    def test_bracket_without_entry_reset_still_reads_authenticated_entry(self):
        fn=bracket();fn['instructions'][0]={'kind':'nop','reads':[],'writes':[]}
        _,report=compile_function(fn,runtime=True)
        self.assertEqual(symbols(report).count('function:dc-c-current-process'),1)

    def test_numeric_profile_and_wrong_containers_refuse(self):
        with self.assertRaises(ValueError):compile_function(bracket())
        fn=bracket();fn['registers'][1]['type']='cpu-thread'
        with self.assertRaisesRegex(ValueError,'exact process container'):compile_function(fn,runtime=True)
        fn=bracket();fn['parameters'][0]['register']=1
        with self.assertRaisesRegex(ValueError,'alias an argument'):compile_function(fn,runtime=True)

    def test_unbalanced_and_misdirected_restores_refuse(self):
        cases=[]
        fn=bracket();fn['instructions'].insert(5,{'kind':'move','reads':[0],'writes':[1]});cases.append((fn,'exactly one'))
        fn=bracket();del fn['instructions'][5];cases.append((fn,'exactly one'))
        fn=bracket();fn['instructions'][5]['reads']=[0];cases.append((fn,'own entry value|single saved copy'))
        fn=bracket();fn['instructions'][1]['reads']=[0];cases.append((fn,'single saved copy'))
        fn=bracket();fn['instructions'].insert(4,{'kind':'move','reads':[0],'writes':[2]});cases.append((fn,'single saved copy'))
        fn=bracket();fn['instructions'][2]={'kind':'constant','value':0,'reads':[],'writes':[1]};cases.append((fn,'plain move'))
        for fn,reason in cases:
            with self.subTest(reason=reason),self.assertRaisesRegex(ValueError,reason):
                compile_function(fn,runtime=True)

    def test_control_flow_must_not_bypass_the_restore(self):
        cases=[]
        fn=bracket();fn['instructions'].insert(4,{'kind':'return','reads':[4],'writes':[]});cases.append((fn,'return inside'))
        fn=bracket();fn['instructions'].insert(4,{'kind':'goto','reads':[],'writes':[],'target':7})
        cases.append((fn,'leaves lexical'))
        fn=bracket();fn['instructions'].insert(1,{'kind':'goto','reads':[],'writes':[],'target':5})
        cases.append((fn,'enters lexical'))
        fn=bracket();fn['instructions'].insert(0,{'kind':'goto','reads':[],'writes':[],'target':1})
        cases.append((fn,'precede every'))
        for fn,reason in cases:
            with self.subTest(reason=reason),self.assertRaisesRegex(ValueError,reason):
                compile_function(fn,runtime=True)

    def test_branch_to_the_restore_stays_inside_the_bracket(self):
        fn=bracket()
        fn['instructions'][3:3]=[{'kind':'constant','value':5,'reads':[],'writes':[4]},
                                 {'kind':'goto','reads':[],'writes':[],'target':7}]
        _,report=compile_function(fn,runtime=True)
        self.assertIn('function:dc-c-enter-process-scope',symbols(report))

    def test_lexical_and_read_only_contexts_cannot_be_combined(self):
        fn=bracket()
        fn['registers'].append({'id':5,'class':'gpr64','type':'process','abi_type':'object'})
        fn['fixed_registers'].append({'register':5,'frontend_register':'r13','role':'process_context_read'})
        fn['instructions'].insert(6,{'kind':'nop','reads':[5],'writes':[]})
        with self.assertRaisesRegex(ValueError,'ambiguous lexical'):compile_function(fn,runtime=True)

    def test_scope_operations_cannot_be_forged_without_the_role(self):
        fn=bracket();fn['fixed_registers']=[];fn['instructions'][0]={'kind':'nop','reads':[],'writes':[]}
        fn['instructions'][2]['kind']='process_scope_enter'
        with self.assertRaisesRegex(ValueError,'without its bracket'):compile_function(fn,runtime=True)

if __name__=='__main__':
    unittest.main()

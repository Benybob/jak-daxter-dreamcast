"""Structured bytes and closed relocation graphs remain strict (AI-assisted)."""
import copy
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(ROOT))
from lower_sh4_ir import compile_module,module_data
def document():
    return {'format_version':1,'functions':[{'name':'root','return_type':'object',
        'registers':[{'id':0,'class':'gpr64','type':'object'}],'parameters':[],
        'instructions':[{'kind':'static_address','static_id':'a','reads':[],'writes':[0]},
                        {'kind':'return','reads':[0],'writes':[]}]}],
        'statics':[{'id':'a','kind':'structure','data_bytes':[0]*8,'address_offset':4,
                    'relocations':[{'offset':0,'symbol':'type:basic'},{'offset':4,'symbol':'data:b'}]},
                   {'id':'b','kind':'structure','data_bytes':[0]*8,'address_offset':0,
                    'relocations':[{'offset':0,'symbol':'pointer:#t'},{'offset':4,'symbol':'data:a'}]}]}
class StructuredStaticTest(unittest.TestCase):
    def test_closed_structure_graph_retains_bytes_offsets_and_mutual_references(self):
        doc=document(); objects=compile_module(doc,['root'],runtime=True); data=module_data(doc,objects)
        self.assertEqual([d['name'] for d in data],['a','b'])
        self.assertEqual([d['address_offset'] for d in data],[4,0])
        self.assertEqual([d['data'] for d in data],[bytes(8),bytes(8)])
        self.assertEqual(data[0]['relocations'],doc['statics'][0]['relocations'])
        doc['statics'][0]['relocations'][1]['symbol']='function:missing'
        with self.assertRaisesRegex(ValueError,'unresolved static function'): compile_module(doc,['root'],runtime=True)

    def test_invalid_structured_payload_offsets_relocations_and_dependencies_reject(self):
        changes=[('data_bytes',[]),('data_bytes',[256]*8),('data_bytes',[True]*8),
                 ('address_offset',8),('address_offset',2),('address_offset',True),
                 ('relocations',[{'offset':1,'symbol':'pointer:#t'}]),
                 ('relocations',[{'offset':8,'symbol':'pointer:#t'}]),
                 ('relocations',[{'offset':0,'symbol':'pointer:#t'}]*2),
                 ('relocations',[{'offset':0,'symbol':'cell:unsupported'}]),
                 ('relocations',[{'offset':0,'symbol':'type:'}]),
                 ('relocations',[{'offset':0,'symbol':'pointer:a\0b'}]),
                 ('relocations',[{'offset':0,'symbol':'data:missing'}])]
        for field,value in changes:
            with self.subTest(field=field,value=value):
                doc=document(); doc['statics'][0][field]=value
                with self.assertRaises(ValueError): compile_module(doc,['root'],runtime=True)
        doc=document(); doc['statics'][0]['kind']='unsupported'
        with self.assertRaisesRegex(ValueError,'unsupported/missing static'): compile_module(doc,['root'],runtime=True)

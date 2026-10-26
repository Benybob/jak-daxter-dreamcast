"""Original compiler metadata and strict extraction guards (AI-assisted), ISC."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
import make_dead_pool_schema as schema
FRONTEND=PORT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe'


@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for dead-pool schema checks')
class DeadPoolSchemaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.header=Path(cls.temp.name)/'schema.h'
        cls.report=schema.generate(FRONTEND,cls.header)
        cls.doc=json.loads(cls.header.with_suffix('.ir.json').read_text())
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def test_original_flags_match_tree_prefix_and_added_method_capacity(self):
        self.assertEqual(self.report['schema'],dict(parent='process-tree',size=32,heap_base=0,methods=16,flags=68719476768))
        self.assertIn('metadata only',self.report['scope'])
        self.assertIn('UINT64_C(68719476768)',self.header.read_text())
    def test_missing_duplicate_and_control_flow_metadata_are_refused(self):
        for kind in ('missing','duplicate','branch'):
            doc=copy.deepcopy(self.doc);init=next(f for f in doc['functions'] if f['name']=='top-level')
            if kind=='missing':
                for op in init['instructions']:
                    if op.get('symbol')=='dead-pool':op['symbol']='absent-pool'
            elif kind=='duplicate':doc['functions'].append(copy.deepcopy(init))
            else:init['instructions'].insert(0,dict(kind='branch',reads=[],writes=[],jumps=[]))
            with self.subTest(kind=kind),self.assertRaises(ValueError):schema.extract(doc)

"""Original C bootstrap schema provenance and native metadata checks (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from generate_kernel_types import generate,schema

class KernelTypesTest(unittest.TestCase):
    def test_generated_schema_matches_original_source(self):
        self.assertEqual((ROOT/'kernel_types_data.h').read_text(),generate(ROOT.parents[1]))
        types,placeholders=schema(ROOT.parents[1]); by_name={t['name']:t for t in types}
        self.assertEqual(len(types),35)
        self.assertEqual((by_name['process']['size'],by_name['process']['methods']),(112,14))
        self.assertEqual((by_name['thread']['size'],by_name['thread']['methods']),(40,12))
        self.assertEqual(by_name['int8']['parent'],'FIX_SYM_SINTEGER_TYPE')
        self.assertEqual(by_name['uintger']['offset'],'FIX_SYM_UINTEGER_TYPE')
        self.assertEqual({p['name'] for p in placeholders},
                         {'level','art-group','texture-page-dir','texture-page','sound','dgo'})

    def test_shared_fixed_metadata_identity_and_atomic_failures(self):
        path=ROOT/'build'/('kernel-types-check.exe' if os.name=='nt' else 'kernel-types-check')
        result=subprocess.run([str(path)],capture_output=True,text=True,check=True)
        self.assertEqual(json.loads(result.stdout),{'tests':289,'passed':289})

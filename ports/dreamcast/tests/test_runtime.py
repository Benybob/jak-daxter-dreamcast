"""Check original layout provenance and the shared native runtime acceptance suite."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from generate_runtime_layout import generate

class RuntimeTest(unittest.TestCase):
    def test_original_data_relocation_forward_types_and_rollback(self):
        executable=ROOT/'build'/('object-load-check.exe' if os.name=='nt' else 'object-load-check')
        result=subprocess.run([str(executable)],check=True,text=True,capture_output=True)
        self.assertEqual(json.loads(result.stdout),{'tests':64,'passed':64})
    def test_original_type_flags_and_method_set_controls(self):
        executable=ROOT/'build'/('type-check.exe' if os.name=='nt' else 'type-check')
        result=subprocess.run([str(executable)],check=True,text=True,capture_output=True)
        self.assertEqual(json.loads(result.stdout),{'tests':55,'passed':55})
    def test_module_relocations_and_transaction_rollback(self):
        executable=ROOT/'build'/('module-check.exe' if os.name=='nt' else 'module-check')
        result=subprocess.run([str(executable)],check=True,text=True,capture_output=True)
        report=json.loads(result.stdout)
        self.assertEqual(report,{'tests':100,'passed':100})

    def test_layout_is_derived_from_current_original_headers(self):
        self.assertEqual((ROOT/'goal_layout.h').read_text(),generate(ROOT.parents[1]))

    def test_shared_runtime_layout_allocation_symbols_and_types(self):
        executable=ROOT/'build'/('runtime-check.exe' if os.name=='nt' else 'runtime-check')
        result=subprocess.run([str(executable)],check=True,text=True,capture_output=True)
        report=json.loads(result.stdout)
        self.assertEqual(report['tests'],81)
        self.assertEqual(report['passed'],81)
        self.assertEqual(report['failed_case'],0)
        self.assertEqual(report['arena_bytes'],1048576)
        self.assertEqual(report['boot_bytes'],263104)
        self.assertLessEqual(report['live_before_stress'],report['arena_bytes'])

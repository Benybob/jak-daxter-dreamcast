"""Dreamcast memory context and budget substitution, through the real frontend (AI-assisted), ISC.

Guards two things that failed silently before 2026-10-27: dreamcast_memory_context.gc
must really give PROCESS_HEAP_MULT 1 (the list-valued PC_BIG_MEMORY read as true),
and the exporter's --batch target constants must replace an in-file defconstant,
record it, and report a stale budget entry. Skipped without the built exporter.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[1]
FRONTEND = PORT.parents[2] / 'og-host/bin/goalc-dreamcast-ir.exe'
sys.path.insert(0, str(PORT))
from build_game_cgo import kernel_contexts  # noqa: E402


def constants(ir, name):
    fn = next(f for f in ir['functions'] if f['name'] == name)
    return [op['value'] for op in fn['instructions'] if op['kind'] == 'constant']


@unittest.skipUnless(FRONTEND.is_file(), 'build goalc-dreamcast-ir first')
class MemoryBudgetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        source = tmp / 'dc-budget-case.gc'
        source.write_text('(in-package goal)\n'
                          '(#cond (PC_PORT (defconstant DC_BUDGET_CASE (* 3 1024))) (#t (defconstant DC_BUDGET_CASE 1)))\n'
                          '(defun dc-budget-mult () PROCESS_HEAP_MULT)\n'
                          '(defun dc-budget-case () DC_BUDGET_CASE)\n')
        budget = tmp / 'budget.gc'
        budget.write_text('(defconstant DC_BUDGET_CASE 112)\n(defconstant DC_BUDGET_STALE 1)\n')
        batch = [{'source': str(p)} for p in kernel_contexts(ROOT, PORT)[:7]]  # through the thread context
        batch.append({'source': str(source), 'output': str(tmp / 'case.ir.json')})
        (tmp / 'batch.json').write_text(json.dumps(batch))
        cls.frontend_run = subprocess.run([str(FRONTEND), str(ROOT), '--batch', str(tmp / 'batch.json'),
                                  str(tmp / 'status.json'), str(budget)], capture_output=True, text=True)
        cls.status = json.loads((tmp / 'status.json').read_text())
        cls.ir = json.loads((tmp / 'case.ir.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_batch_succeeds(self):
        self.assertEqual(self.frontend_run.returncode, 0, self.frontend_run.stderr[-2000:])
        self.assertIsNone(self.status['failed'])

    def test_memory_context_gives_the_original_multiplier(self):
        self.assertEqual(constants(self.ir, 'dc-budget-mult'), [1])

    def test_budget_replaces_nested_in_file_constant_and_records_it(self):
        self.assertEqual(constants(self.ir, 'dc-budget-case'), [112])
        # Both #cond branches define it; both forms are replaced and recorded.
        self.assertEqual(self.ir['target_constants'], ['DC_BUDGET_CASE', 'DC_BUDGET_CASE'])

    def test_stale_budget_entry_is_reported(self):
        self.assertEqual(self.status['unused_target_constants'], ['DC_BUDGET_STALE'])


if __name__ == '__main__':
    unittest.main()

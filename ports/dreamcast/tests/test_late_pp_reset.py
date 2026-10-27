"""A pp reset placed after branches or calls, and the float-alias ABI (AI-assisted), ISC.

Original (method new fact-info) runs (go process-drawable-art-error "memory") on
one branch: the frontend resets pp (r13) at that first use, after a call and a
branch. Desktop IR_ValueReset emits no code and pp is preserved across calls, so
the entry value is what that reset names; only an earlier read or write of pp
must still refuse. meters/degrees are float value types and take the float ABI.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[1]
FRONTEND = PORT.parents[2] / 'og-host/bin/goalc-dreamcast-ir.exe'
sys.path.insert(0, str(PORT))
from build_game_cgo import kernel_contexts  # noqa: E402
from lower_sh4_ir import compile_function  # noqa: E402

BINDING = {'register': 1, 'frontend_register': 'r13', 'role': 'process_lexical'}


def go_from_method():
    """call; branch; (pp reset; store a state into pp's next-state); return."""
    return {'name': 'dc-late-pp', 'asm': False, 'return_type': 'object', 'return_abi_type': 'object',
            'registers': [{'id': 0, 'class': 'gpr64', 'type': 'object', 'abi_type': 'object'},
                          {'id': 1, 'class': 'gpr64', 'type': 'process', 'abi_type': 'object'},
                          {'id': 2, 'class': 'gpr64', 'type': '(function object)', 'abi_type': '(function object)'},
                          {'id': 3, 'class': 'gpr64', 'type': 'object', 'abi_type': 'object'},
                          {'id': 4, 'class': 'gpr64', 'type': 'object', 'abi_type': 'object'},
                          {'id': 5, 'class': 'gpr64', 'type': 'object', 'abi_type': 'object'}],
            'parameters': [{'name': 'x', 'register': 0, 'type': 'object', 'abi_type': 'object'}],
            'fixed_registers': [dict(BINDING)],
            'instructions': [
                {'kind': 'symbol_value', 'symbol': 'dc-hook', 'reads': [], 'writes': [2]},
                {'kind': 'call', 'reads': [2], 'writes': [3], 'abi': {'parameters': [], 'return_type': 'object'}},
                {'kind': 'constant', 'value': 0, 'reads': [], 'writes': [5]},
                {'kind': 'branch', 'condition': 'ne', 'signed': False, 'float': False, 'reads': [0, 5], 'writes': [],
                 'target': 7},
                {'kind': 'fixed_register_reset', 'reads': [], 'writes': [1], 'bindings': [dict(BINDING)]},
                {'kind': 'symbol_value', 'symbol': 'dc-state', 'reads': [], 'writes': [4]},
                {'kind': 'store_memory', 'memory_class': 'gpr64', 'offset': 72, 'size': 4, 'reads': [4, 1], 'writes': []},
                {'kind': 'return', 'reads': [3], 'writes': []}]}


class LatePpResetTest(unittest.TestCase):
    def test_reset_after_call_and_branch_reads_the_entry_value(self):
        for optimize in (True, False):
            with self.subTest(optimize=optimize):
                data, report = compile_function(go_from_method(), runtime=True, optimize=optimize)
                self.assertGreater(len(data), 0)
                names = [r['symbol'] for r in report['relocations']]
                self.assertEqual(names.count('function:dc-c-current-process'), 1)

    def test_use_of_pp_before_its_reset_still_refuses(self):
        fn = go_from_method()
        fn['instructions'][2] = {'kind': 'move', 'reads': [1], 'writes': [5]}
        with self.assertRaisesRegex(ValueError, 'precede every process use'):
            compile_function(fn, runtime=True)


@unittest.skipUnless(FRONTEND.is_file(), 'build goalc-dreamcast-ir first')
class FloatAliasAbiTest(unittest.TestCase):
    def test_meters_and_degrees_take_the_float_abi(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / 'dc-float-alias.gc'
            source.write_text('(in-package goal)\n'
                              '(defun dc-float-alias ((a meters) (b degrees)) (+ a (the meters b)))\n')
            batch = [{'source': str(p)} for p in kernel_contexts(ROOT, PORT)[:7]]
            batch.append({'source': str(source), 'output': str(tmp / 'case.ir.json')})
            (tmp / 'batch.json').write_text(json.dumps(batch))
            run = subprocess.run([str(FRONTEND), str(ROOT), '--batch', str(tmp / 'batch.json'), str(tmp / 'status.json')],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr[-2000:])
            ir = json.loads((tmp / 'case.ir.json').read_text())
        fn = next(f for f in ir['functions'] if f['name'] == 'dc-float-alias')
        self.assertEqual([(p['type'], p['abi_type']) for p in fn['parameters']],
                         [('meters', 'float'), ('degrees', 'float')])
        self.assertEqual(fn['return_abi_type'], 'float')


if __name__ == '__main__':
    unittest.main()

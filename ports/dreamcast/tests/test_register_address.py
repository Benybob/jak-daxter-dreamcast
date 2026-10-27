"""(& var) of a register variable: exporter op and SH-4 lowering (AI-assisted), ISC.

Desktop IR_RegValAddr forces the variable onto the stack and yields its home's
address; its to_rai() leaves the variable out of the reads. The lowerer pins
that variable's slot outside liveness reuse and roots it for dead-code removal.
"""
import json
import sys
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from lower_sh4_ir import compile_function, remove_dead_pure_ops  # noqa: E402


def reg(i, typ='int'):
    return {'id': i, 'class': 'gpr64', 'type': typ, 'abi_type': typ}


def fn(instructions, registers, parameters=()):
    return {'name': 'case', 'instructions': instructions, 'registers': registers, 'parameters': list(parameters),
            'return_type': '(pointer object)', 'return_abi_type': '(pointer object)', 'top_level': False,
            'fixed_registers': [], 'implicit_parameters': [], 'locals': [], 'register_aliases': []}


def case():
    # (let ((v arg)) (& v)): v is only observable through the pointer.
    ops = [{'kind': 'move', 'reads': [0], 'writes': [1], 'text': 'mov igpr-1, igpr-0'},
           {'kind': 'register_address', 'source': 1, 'reads': [], 'writes': [2], 'text': 'mov igpr-2, &igpr-1'},
           {'kind': 'return', 'reads': [2], 'writes': [], 'text': 'ret'}]
    regs = [reg(0, 'object'), reg(1, 'object'), reg(2, '(pointer object)')]
    return fn(ops, regs, [{'register': 0, 'type': 'object', 'abi_type': 'object', 'name': 'arg', 'local_register': 0}])


class RegisterAddressTest(unittest.TestCase):
    def test_lowers_with_a_pinned_slot(self):
        data, manifest = compile_function(case(), runtime=True)[:2]
        self.assertGreater(len(data), 0)
        # Liveness alone would fold all three values into one slot (v is never
        # read after the address op); pinning keeps v's home apart: two slots.
        self.assertEqual(manifest['stack_slot_count'], 2)

    def test_write_of_addressed_variable_is_not_dead(self):
        kept = remove_dead_pure_ops(case())['instructions']
        self.assertEqual(kept[0]['kind'], 'move')

    def test_requires_runtime_profile(self):
        with self.assertRaises(ValueError):
            compile_function(case(), runtime=False)

    def test_shape_is_checked(self):
        bad = case()
        bad['instructions'][1]['reads'] = [1]
        with self.assertRaisesRegex(ValueError, 'write one register and read none'):
            compile_function(bad, runtime=True)
        bad = case()
        bad['instructions'][1]['source'] = 9
        with self.assertRaisesRegex(ValueError, 'unknown virtual register'):
            compile_function(bad, runtime=True)

    @unittest.skipUnless((PORT / 'build/game_res.ir.json').is_file(), 'build GAME.CGO first')
    def test_res_exports_typed_register_addresses(self):
        doc = json.loads((PORT / 'build/game_res.ir.json').read_text())
        ops = [op for f in doc['functions'] for op in f['instructions']]
        addresses = [op for op in ops if op['kind'] == 'register_address']
        self.assertTrue(addresses)
        self.assertFalse([op for op in ops if op['kind'] == 'unsupported' and '&' in op.get('text', '')])
        for op in addresses:
            self.assertEqual(op['reads'], [])
            self.assertIsInstance(op['source'], int)


if __name__ == '__main__':
    unittest.main()

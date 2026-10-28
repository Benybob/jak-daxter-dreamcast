"""A behavior's own self brackets on synthetic original-shaped IR (AI-assisted), ISC.

subtitle.gc's trans sets pp to the movie's process twice around ja-aframe-num:
(protect (pp) (set! pp (handle->process ...)) ...). Each pair lowers to a lexical
scope enter and leave; overlapping pairs or a restore from a non-entry copy refuse.
"""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import compile_function  # noqa: E402

SELF = {'frontend_register': 'r13', 'register': 0, 'role': 'process_self'}


def reg(i, typ='object'):
    return {'id': i, 'class': 'gpr64', 'type': typ, 'abi_type': typ}


def two_brackets():
    """save; self=X; call; restore -- twice, each from its own entry copy."""
    def bracket(saved, other, fn, result):
        return [{'kind': 'move', 'reads': [0], 'writes': [saved]},
                {'kind': 'symbol_value', 'symbol': '*movie-process*', 'reads': [], 'writes': [other]},
                {'kind': 'move', 'reads': [other], 'writes': [0]},
                {'kind': 'symbol_value', 'symbol': 'dc-hook', 'reads': [], 'writes': [fn]},
                {'kind': 'call', 'reads': [fn], 'writes': [result], 'abi': {'parameters': [], 'return_type': 'object'}},
                {'kind': 'move', 'reads': [saved], 'writes': [0]}]
    return {'name': 'dc-self-brackets', 'asm': False, 'return_type': 'none', 'return_abi_type': 'none',
            'registers': [dict(reg(0), type='process'), reg(1), reg(2), reg(3, '(function object)'), reg(4),
                          reg(5), reg(6), reg(7, '(function object)'), reg(8)],
            'parameters': [],
            'implicit_parameters': [{'abi_type': 'object', 'kind': 'fixed_entry', 'name': 'self', 'register': 0,
                                     'type': 'process', 'process_type': True}],
            'fixed_registers': [dict(SELF)], 'locals': [], 'register_aliases': [],
            'instructions': [{'kind': 'fixed_register_reset', 'reads': [], 'writes': [0], 'bindings': [dict(SELF)]}] +
                            bracket(1, 2, 3, 4) + bracket(5, 6, 7, 8) +
                            [{'kind': 'return', 'reads': [], 'writes': []}]}


def symbols(report):
    return [r['symbol'] for r in report['relocations']]


class BehaviorSelfBracketTest(unittest.TestCase):
    def test_two_brackets_enter_and_leave_scopes(self):
        for optimize in (True, False):
            with self.subTest(optimize=optimize):
                data, report = compile_function(two_brackets(), runtime=True, optimize=optimize)
                names = symbols(report)
                self.assertGreater(len(data), 0)
                self.assertEqual(names.count('function:dc-c-current-process'), 1)
                self.assertEqual(names.count('function:dc-c-enter-process-scope'), 1)  # pooled literal
                self.assertIn('function:dc-c-leave-process-scope', names)
                self.assertIn('function:dc-c-call-error', names)

    def test_restore_from_a_copy_taken_inside_a_bracket_refuses(self):
        fn = two_brackets()
        ops = fn['instructions']
        ops[7], ops[9] = ops[9], ops[7]  # second save after its own assignment
        with self.assertRaisesRegex(ValueError, 'entry-value copy|plain move|restore its own'):
            compile_function(fn, runtime=True)

    def test_unpaired_assignment_refuses(self):
        fn = two_brackets()
        fn['instructions'][12] = {'kind': 'nop', 'reads': [], 'writes': []}
        with self.assertRaisesRegex(ValueError, 'pairs'):
            compile_function(fn, runtime=True)

    def test_return_inside_a_bracket_refuses(self):
        fn = two_brackets()
        fn['return_type'] = fn['return_abi_type'] = 'none'
        fn['instructions'].insert(5, {'kind': 'return', 'reads': [], 'writes': []})
        with self.assertRaisesRegex(ValueError, 'return inside'):
            compile_function(fn, runtime=True)


if __name__ == '__main__':
    unittest.main()

"""Lowering rules added for original GAME.CGO engine files (AI-assisted), ISC."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gp_deps  # noqa: E402
from build_game_cgo import compile_order, game_order  # noqa: E402
from lower_sh4_ir import (compile_function, mmi_asm, remove_dead_pure_ops, scalar_kind,  # noqa: E402
                          UninitializedRead, validate_definitions)

ROOT = Path(__file__).resolve().parents[3]


def reg(i, cls='gpr64', typ='int'):
    return {'id': i, 'class': cls, 'type': typ, 'abi_type': typ}


def fn(instructions, registers, return_type='int', top_level=False, parameters=()):
    return {'name': 'case', 'instructions': instructions, 'registers': registers, 'parameters': list(parameters),
            'return_type': return_type, 'return_abi_type': return_type, 'top_level': top_level,
            'fixed_registers': [], 'implicit_parameters': [], 'locals': [], 'register_aliases': []}


class GameGpTest(unittest.TestCase):
    def test_closure_is_dependencies_first(self):
        graph = gp_deps.load(ROOT)
        order = gp_deps.closure(graph, 'matrix')
        self.assertLess(order.index('vector-h'), order.index('matrix-h'))
        self.assertNotIn('matrix', order)
        # Compile order is not load order: pc-debug-methods needs target-util.
        self.assertIn('target-util', gp_deps.closure(graph, 'pc-debug-methods'))

    def test_dgo_order_skips_commented_objects(self):
        order = game_order(ROOT)
        self.assertEqual(order[:3], ['types-h', 'vu1-macros', 'math'])
        self.assertNotIn('collide-planes', order)  # ;;  "collide-planes.o" in game.gd

    def test_compile_order_respects_every_target(self):
        graph = gp_deps.load(ROOT)
        order, sources = compile_order(graph, ['math', 'pc-debug-methods'])
        self.assertEqual(sources, ['math', 'pc-debug-methods'])
        for name in order:
            for dep in graph[name][1]:
                if dep in order: self.assertLess(order.index(dep), order.index(name))
        self.assertNotIn('gkernel', order)  # the kernel is preloaded in its Dreamcast configuration


class RuleTest(unittest.TestCase):
    def test_inline_array_is_an_address(self):
        self.assertEqual(scalar_kind('(inline-array sphere)', True), 'pointer')
        self.assertEqual(scalar_kind('(inline-array sphere)', False), '(inline-array sphere)')

    def test_mmi_parse(self):
        op = mmi_asm({'kind': 'unsupported', 'text': '.pextlw ii128-5, ii128-16, ii128-14', 'reads': [16, 14], 'writes': [5]})
        self.assertEqual((op['kind'], op['code'], op['dst'], op['srcs']), ('mmi', 3, 5, [16, 14]))
        op = mmi_asm({'kind': 'unsupported', 'text': '.pw.sra ii128-1, ii128-2, 5', 'reads': [2], 'writes': [1]})
        self.assertEqual(op['code'], 34 | 5 << 8)
        self.assertIsNone(mmi_asm({'kind': 'unsupported', 'text': '.pextlw ii128-5, ii128-16, ii128-14',
                                   'reads': [16], 'writes': [5]}))

    def test_dead_pure_cycle_is_removed_but_roots_stay(self):
        ops = [{'kind': 'move', 'reads': [1], 'writes': [6], 'text': 'a'},
               {'kind': 'and64', 'reads': [6, 5], 'writes': [6], 'text': 'b'},
               {'kind': 'move', 'reads': [6], 'writes': [1], 'text': 'c'},
               {'kind': 'constant', 'reads': [], 'writes': [7], 'value': 3, 'text': 'd'},
               {'kind': 'return', 'reads': [7], 'writes': [8], 'text': 'e'}]
        out = remove_dead_pure_ops(fn(ops, [reg(i) for i in range(9)]))['instructions']
        self.assertEqual([o['kind'] for o in out], ['nop', 'nop', 'nop', 'constant', 'return'])

    def test_top_level_void_only_for_an_unassigned_return(self):
        ops = [{'kind': 'symbol_pointer', 'reads': [], 'writes': [1], 'symbol': '#f', 'text': ''},
               {'kind': 'branch', 'reads': [1, 1], 'writes': [], 'target': 3, 'condition': 'eq', 'signed': False, 'text': ''},
               {'kind': 'symbol_pointer', 'reads': [], 'writes': [0], 'symbol': '#f', 'text': ''},
               {'kind': 'return', 'reads': [0], 'writes': [2], 'text': ''},
               {'kind': 'nop', 'reads': [], 'writes': [], 'text': 'null'}]
        regs = [reg(0, typ='object'), reg(1, typ='object'), reg(2, typ='object')]
        with self.assertRaises(UninitializedRead):
            validate_definitions(fn(ops, regs, 'object'), {0, 1, 2})
        self.assertGreater(len(compile_function(fn(ops, regs, 'object', top_level=True), runtime=True)[0]), 0)
        with self.assertRaisesRegex(ValueError, 'uninitialized'):
            compile_function(fn(ops, regs, 'object'), runtime=True)

    def test_module_defined_types_are_only_own_deftypes(self):
        from lower_sh4_ir import module_defined_types
        ops = [{'kind': 'symbol_value', 'reads': [], 'writes': [1], 'symbol': 'type'},
               {'kind': 'load_memory', 'reads': [1], 'writes': [2], 'offset': 16, 'size': 4},
               {'kind': 'symbol_pointer', 'reads': [], 'writes': [3], 'symbol': 'my-elt'},
               {'kind': 'symbol_value', 'reads': [], 'writes': [4], 'symbol': 'structure'},
               {'kind': 'move', 'reads': [3], 'writes': [5]},
               {'kind': 'call', 'reads': [2, 5, 4], 'writes': [6]},
               # a call of some other method 0 does not define a type
               {'kind': 'symbol_value', 'reads': [], 'writes': [7], 'symbol': 'other'},
               {'kind': 'load_memory', 'reads': [7], 'writes': [8], 'offset': 16, 'size': 4},
               {'kind': 'symbol_pointer', 'reads': [], 'writes': [9], 'symbol': 'not-a-type'},
               {'kind': 'call', 'reads': [8, 9], 'writes': [10]}]
        doc = {'functions': [{'top_level': True, 'instructions': ops}]}
        self.assertEqual(module_defined_types(doc), {'my-elt'})

    def test_none_copy_is_dropped(self):
        ops = [{'kind': 'move', 'reads': [1], 'writes': [0], 'text': 'mov'},
               {'kind': 'return', 'reads': [0], 'writes': [2], 'text': 'ret'}]
        regs = [reg(0, typ='none'), reg(1, typ='none'), reg(2, typ='none')]
        self.assertGreater(len(compile_function(fn(ops, regs, 'none', top_level=True), runtime=True)[0]), 0)


if __name__ == '__main__':
    unittest.main()

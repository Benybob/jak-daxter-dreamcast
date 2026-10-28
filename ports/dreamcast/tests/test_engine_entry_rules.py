"""Entry rules for GAME.CGO files 145-173 (AI-assisted), ISC.

- a behavior's implicit self beside ordinary arguments (navigate-h's
  nav-mesh-connect), whose entry value-reset covers both;
- PS2 constant idioms (.pceqw x,x,x) that read no value (bsp);
- a register assigned on one path but read on another (bsp-camera-asm stores
  its a2 flags unset when the root is a leaf) is zero-filled at entry, while a
  register no op assigns still refuses.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import (compile_function, discard_debug_segment, mips_zero_register_writes,  # noqa: E402
                          mmi_asm, module_data, native_machine_registers)

SELF = {'frontend_register': 'r13', 'register': 2, 'role': 'process_self'}


def reg(i, cls='gpr64', typ='object'):
    return {'id': i, 'class': cls, 'type': typ, 'abi_type': typ}


def behavior_with_arguments():
    """(defbehavior b process ((a object) (b object)) (-> self ...)) at entry."""
    return {'name': 'dc-behavior', 'asm': False, 'return_type': 'object', 'return_abi_type': 'object',
            'registers': [reg(0), reg(1), dict(reg(2), type='process'), reg(3)],
            'parameters': [{'name': 'a', 'register': 0, 'type': 'object', 'abi_type': 'object'},
                           {'name': 'b', 'register': 1, 'type': 'object', 'abi_type': 'object'}],
            'implicit_parameters': [{'abi_type': 'object', 'kind': 'fixed_entry', 'name': 'self', 'register': 2,
                                     'type': 'process', 'process_type': True}],
            'fixed_registers': [dict(SELF)], 'locals': [], 'register_aliases': [],
            'instructions': [
                {'kind': 'nop', 'text': 'value-reset', 'reads': [], 'writes': [0, 1, 2]},
                {'kind': 'load_memory', 'memory_class': 'gpr64', 'offset': 48, 'size': 4, 'signed': False,
                 'reads': [2], 'writes': [3]},
                {'kind': 'return', 'reads': [3], 'writes': []}]}


def partly_assigned():
    """if (x) v = x; store v -- v unassigned on the fallthrough-to-store path."""
    return {'name': 'dc-partial', 'asm': False, 'return_type': 'none', 'return_abi_type': 'none',
            'registers': [reg(0, typ='int'), reg(1, typ='int'), reg(2, typ='int'), reg(3)],
            'parameters': [{'name': 'x', 'register': 0, 'type': 'int', 'abi_type': 'int'},
                           {'name': 'p', 'register': 3, 'type': 'object', 'abi_type': 'object'}],
            'implicit_parameters': [], 'fixed_registers': [], 'locals': [], 'register_aliases': [],
            'instructions': [
                {'kind': 'constant', 'value': 0, 'reads': [], 'writes': [2]},
                {'kind': 'branch', 'condition': 'eq', 'signed': False, 'float': False, 'reads': [0, 2],
                 'writes': [], 'target': 3},
                {'kind': 'move', 'reads': [0], 'writes': [1]},
                {'kind': 'store_memory', 'memory_class': 'gpr64', 'offset': 0, 'size': 4, 'reads': [1, 3],
                 'writes': []},
                {'kind': 'return', 'reads': [], 'writes': []}]}


class EntryRuleTest(unittest.TestCase):
    def test_implicit_self_beside_arguments(self):
        data, report = compile_function(behavior_with_arguments(), runtime=True)
        self.assertGreater(len(data), 0)
        names = [r['symbol'] for r in report['relocations']]
        self.assertEqual(names.count('function:dc-c-current-process'), 1)

    def test_self_as_an_argument_refuses(self):
        fn = behavior_with_arguments()
        fn['implicit_parameters'][0]['register'] = 1
        with self.assertRaisesRegex(ValueError, 'implicit process/self'):
            compile_function(fn, runtime=True)

    def test_self_constant_mmi_reads_nothing(self):
        op = mmi_asm({'kind': 'unsupported', 'text': '.pceqw ii128-9, ii128-9, ii128-9', 'reads': [9, 9],
                      'writes': [9]})
        self.assertEqual((op['reads'], op['srcs']), ([], [9, 9]))
        op = mmi_asm({'kind': 'unsupported', 'text': '.pand ii128-9, ii128-9, ii128-9', 'reads': [9, 9],
                      'writes': [9]})
        self.assertEqual(op['reads'], [9, 9])  # x&x is x: it does read x

    def test_partly_assigned_register_is_zero_filled_at_entry(self):
        data, report = compile_function(partly_assigned(), runtime=True)
        self.assertGreater(len(data), 0)

    def test_never_assigned_register_still_refuses(self):
        fn = partly_assigned()
        fn['instructions'][2] = {'kind': 'nop', 'reads': [], 'writes': []}
        with self.assertRaisesRegex(ValueError, 'uninitialized'):
            compile_function(fn, runtime=True)


def never_assigned_local():
    """process-particles' shape: (- v1-29 gp-0) of locals the PC port never sets."""
    fn = partly_assigned()
    fn['name'] = 'dc-timing'
    fn['locals'] = [{'name': 'v1-29', 'register': 1, 'type': 'int', 'abi_type': 'int'}]
    fn['instructions'] = [fn['instructions'][3], fn['instructions'][4]]  # store v1-29; return
    return fn


class ReviewedZeroLocalTest(unittest.TestCase):
    def test_unlisted_never_assigned_local_refuses(self):
        with self.assertRaisesRegex(ValueError, 'uninitialized'):
            compile_function(never_assigned_local(), runtime=True)

    def test_listed_local_is_zero_filled_and_recorded(self):
        fn = dict(never_assigned_local(), zero_locals=['v1-29'])
        _, report = compile_function(fn, runtime=True)
        self.assertEqual(report['zero_locals'], ['v1-29'])

    def test_stale_or_assigned_entries_refuse(self):
        with self.assertRaisesRegex(ValueError, 'not a local'):
            compile_function(dict(never_assigned_local(), zero_locals=['nope']), runtime=True)
        fn = dict(partly_assigned(), zero_locals=['v'],
                  locals=[{'name': 'v', 'register': 1, 'type': 'int', 'abi_type': 'int'}])
        with self.assertRaisesRegex(ValueError, 'is assigned'):
            compile_function(fn, runtime=True)

    def test_reviewed_file_entries_have_reasons(self):
        import json
        reviewed = json.loads((Path(__file__).resolve().parents[1] / 'dreamcast_unassigned_locals.json').read_text())
        for name, entry in reviewed.items():
            if name.startswith('_'): continue
            self.assertTrue((entry.get('locals') or entry.get('partial_return') is True) and entry['reason'], name)


class TypeReferenceImportTest(unittest.TestCase):
    def doc(self, relocation):
        data = {'id': 'static_0', 'kind': 'structure', 'data_bytes': [0] * 8, 'address_offset': 0,
                'relocations': [relocation]}
        return {'functions': [], 'statics': [data]}

    def imports(self, relocation):
        objects = {'f': (b'', {'name': 'f', 'relocations': [{'symbol': 'data:static_0'}]})}
        return [r['symbol'] for r in module_data(self.doc(relocation), objects)[0]['relocations']]

    def test_explicit_type_ref_count_interns_like_desktop(self):
        self.assertEqual(self.imports({'offset': 4, 'symbol': 'type:sage-finalboss', 'method_count': 53}),
                         ['deftype:sage-finalboss:53'])

    def test_plain_type_pointer_stays_strict(self):
        self.assertEqual(self.imports({'offset': 0, 'symbol': 'type:entity-info'}), ['type:entity-info'])


class DebugSegmentTest(unittest.TestCase):
    def test_debug_functions_drop_and_references_become_null(self):
        doc = {'functions': [
            {'name': 'debug-fn', 'segment': 1, 'instructions': []},
            {'name': 'main-fn', 'segment': 0, 'instructions': [
                {'kind': 'function_address', 'function': 'debug-fn', 'reads': [], 'writes': [1]},
                {'kind': 'function_address', 'function': 'main-fn', 'reads': [], 'writes': [2]}]},
            {'name': 'top-level', 'segment': 2, 'top_level': True, 'instructions': []}],
            'statics': [{'id': 's', 'relocations': [{'offset': 0, 'symbol': 'function:debug-fn'},
                                                    {'offset': 4, 'symbol': 'function:main-fn'}]},
                        {'id': 'p', 'kind': 'pair', 'car': {'kind': 'function', 'target': 'debug-fn'},
                         'cdr': {'kind': 'constant', 'value_bits': 8}}]}
        out, discarded = discard_debug_segment(doc)
        self.assertEqual(discarded, ['debug-fn'])
        self.assertEqual([f['name'] for f in out['functions']], ['main-fn', 'top-level'])
        ops = out['functions'][0]['instructions']
        self.assertEqual((ops[0]['kind'], ops[0]['value']), ('constant', 0))
        self.assertEqual(ops[1]['kind'], 'function_address')
        self.assertEqual([r['symbol'] for r in out['statics'][0]['relocations']], ['function:main-fn'])
        self.assertEqual(out['statics'][1]['car'], {'kind': 'constant', 'value_bits': 0})


class VoidResultValueTest(unittest.TestCase):
    def test_void_call_result_in_a_value_register_is_false(self):
        """game-save's (('die) (deactivate self)) arm: a void call's phantom result."""
        fn = {'name': 'dc-void-arm', 'asm': False, 'return_type': 'object', 'return_abi_type': 'object',
              'registers': [reg(0, typ='(function none)'), reg(1, typ='none'), reg(2)],
              'parameters': [], 'implicit_parameters': [], 'fixed_registers': [], 'locals': [],
              'register_aliases': [],
              'instructions': [
                  {'kind': 'symbol_value', 'symbol': 'dc-hook', 'reads': [], 'writes': [0]},
                  {'kind': 'call', 'reads': [0], 'writes': [], 'text': 'call igpr-0 (ret igpr-1) (args)',
                   'abi': {'parameters': [], 'return_type': 'none'}},
                  {'kind': 'move', 'reads': [1], 'writes': [2]},
                  {'kind': 'return', 'reads': [2], 'writes': []}]}
        data, report = compile_function(fn, runtime=True)
        self.assertIn('pointer:#f', [r['symbol'] for r in report['relocations']])


    def test_void_call_result_in_an_integer_register_is_zero(self):
        """process-taskable-hide-enter: (the-as int (ja-post)) through a none copy."""
        fn = {'name': 'dc-void-int', 'asm': False, 'return_type': 'int', 'return_abi_type': 'int',
              'registers': [reg(0, typ='(function none)'), reg(1, typ='none'), reg(2, typ='none'),
                            reg(3, typ='int')],
              'parameters': [], 'implicit_parameters': [], 'fixed_registers': [], 'locals': [],
              'register_aliases': [],
              'instructions': [
                  {'kind': 'symbol_value', 'symbol': 'dc-hook', 'reads': [], 'writes': [0]},
                  {'kind': 'call', 'reads': [0], 'writes': [], 'text': 'call igpr-0 (ret igpr-1) (args)',
                   'abi': {'parameters': [], 'return_type': 'none'}},
                  {'kind': 'nop', 'reads': [], 'writes': [], 'text': 'mov igpr-2, igpr-1'},
                  {'kind': 'move', 'reads': [2], 'writes': [3]},
                  {'kind': 'return', 'reads': [3], 'writes': []}]}
        data, report = compile_function(fn, runtime=True)
        self.assertGreater(len(data), 0)

    def test_none_register_nothing_produces_still_refuses(self):
        fn = {'name': 'dc-no-producer', 'asm': False, 'return_type': 'int', 'return_abi_type': 'int',
              'registers': [reg(1, typ='none'), reg(3, typ='int')],
              'parameters': [], 'implicit_parameters': [], 'fixed_registers': [], 'locals': [],
              'register_aliases': [],
              'instructions': [{'kind': 'move', 'reads': [1], 'writes': [3]},
                               {'kind': 'return', 'reads': [3], 'writes': []}]}
        with self.assertRaisesRegex(ValueError, 'uninitialized'):
            compile_function(fn, runtime=True)


class MipsZeroTest(unittest.TestCase):
    def test_write_to_r0_local_is_zero_and_reads_nothing(self):
        fn = {'locals': [{'name': 'r0-2', 'register': 7}], 'registers': [reg(7, 'int128', 'uint128'), reg(15, typ='none')],
              'instructions': [{'kind': 'unsupported', 'text': '.mov ii128-7 igpr-15', 'reads': [15], 'writes': [7]}]}
        op = mips_zero_register_writes(fn)['instructions'][0]
        self.assertEqual((op['kind'], op['reads'], op['dst']), ('vfzero', [], 7))

    def test_never_assigned_zero_local_reads_as_zero(self):
        fn = partly_assigned()
        fn['locals'] = [{'name': 'zero', 'register': 1, 'type': 'int', 'abi_type': 'int'}]
        fn['instructions'] = [fn['instructions'][3], fn['instructions'][4]]  # store zero; return
        self.assertGreater(len(compile_function(fn, runtime=True)[0]), 0)


class FixedRegisterResetTest(unittest.TestCase):
    def test_r15_reset_reads_the_zero_base(self):
        fn = {'registers': [reg(5, typ='uint')], 'fixed_registers': [{'frontend_register': 'r15', 'register': 5,
                                                                       'role': 'unsupported'}],
              'instructions': [{'kind': 'fixed_register_reset', 'reads': [], 'writes': [5]}]}
        out = native_machine_registers(fn)
        self.assertEqual(out['fixed_registers'], [])
        self.assertEqual([(o['kind'], o.get('value')) for o in out['instructions']], [('constant', 0), ('constant', 0)])

    def test_r15_real_write_keeps_refusing(self):
        fn = {'registers': [reg(5, typ='uint'), reg(6, typ='uint')],
              'fixed_registers': [{'frontend_register': 'r15', 'register': 5, 'role': 'unsupported'}],
              'instructions': [{'kind': 'move', 'reads': [6], 'writes': [5]}]}
        self.assertEqual(len(native_machine_registers(fn)['fixed_registers']), 1)


class ReviewedStaleLocalTest(unittest.TestCase):
    def test_assigned_elsewhere_and_false_initial(self):
        fn = dict(partly_assigned(), zero_locals=['v'], zero_locals_assigned_elsewhere=True, false_locals=['v'],
                  locals=[{'name': 'v', 'register': 1, 'type': 'int', 'abi_type': 'int'}])
        _, report = compile_function(fn, runtime=True)
        self.assertIn('pointer:#f', [r['symbol'] for r in report['relocations']])

    def test_false_local_must_be_listed(self):
        fn = dict(never_assigned_local(), zero_locals=['v1-29'], false_locals=['other'])
        with self.assertRaisesRegex(ValueError, 'must also be listed'):
            compile_function(fn, runtime=True)


class WideFalseTest(unittest.TestCase):
    def test_false_into_int128_is_low_pointer_high_zero(self):
        fn = {'name': 'dc-wide-false', 'asm': False, 'return_type': 'uint128', 'return_abi_type': 'uint128',
              'registers': [reg(2, 'int128', 'uint128')], 'parameters': [], 'implicit_parameters': [],
              'fixed_registers': [], 'locals': [], 'register_aliases': [],
              'instructions': [{'kind': 'symbol_pointer', 'symbol': '#f', 'reads': [], 'writes': [2]},
                               {'kind': 'return', 'reads': [2], 'writes': []}]}
        _, report = compile_function(fn, runtime=True)
        self.assertIn('pointer:#f', [r['symbol'] for r in report['relocations']])


if __name__ == '__main__':
    unittest.main()

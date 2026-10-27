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
from lower_sh4_ir import compile_function, mmi_asm, module_data  # noqa: E402

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
            self.assertTrue(entry['locals'] and entry['reason'], name)


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


if __name__ == '__main__':
    unittest.main()

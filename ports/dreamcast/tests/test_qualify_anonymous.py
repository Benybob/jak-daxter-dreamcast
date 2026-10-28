"""Module-local anonymous functions (AI-assisted), ISC.

Desktop GOAL links a file's lambdas (state code, hooks) by address and never
makes anon-function-N a global symbol. Native modules export every function by
name, and code entry is authenticated by the symbol that still holds it, so
GAME.CGO lowers with --qualify-anonymous MODULE: a definition and each
function_address of it become MODULE:anon-function-N, and named functions keep
their names.
"""
from pathlib import Path
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from lower_sh4_ir import qualify_anonymous  # noqa: E402


def address(name):
    return {'kind': 'function_address', 'function': name, 'reads': [], 'writes': [1]}


class QualifyAnonymous(unittest.TestCase):
    def setUp(self):
        self.doc = {'functions': [
            {'name': 'anon-function-1', 'instructions': [{'kind': 'return', 'reads': [], 'writes': []}]},
            {'name': 'top-level', 'instructions': [address('anon-function-1'), address('init-time-of-day')]},
            {'name': 'init-time-of-day', 'instructions': []},
        ], 'statics': []}

    def test_definitions_and_addresses_become_module_local(self):
        doc = qualify_anonymous(self.doc, 'time-of-day')
        names = [f['name'] for f in doc['functions']]
        self.assertEqual(names, ['time-of-day:anon-function-1', 'top-level', 'init-time-of-day'])
        top = doc['functions'][1]['instructions']
        self.assertEqual([op['function'] for op in top], ['time-of-day:anon-function-1', 'init-time-of-day'])

    def test_two_modules_no_longer_share_a_name(self):
        a = {f['name'] for f in qualify_anonymous(self.doc, 'time-of-day')['functions']}
        b = {f['name'] for f in qualify_anonymous(self.doc, 'cam-states')['functions']}
        self.assertNotIn('time-of-day:anon-function-1', b)
        self.assertEqual(a & b, {'top-level', 'init-time-of-day'})

    def test_only_exact_anonymous_names_change(self):
        doc = {'functions': [{'name': 'anon-function-12x', 'instructions': []},
                             {'name': '(method anon-function-1 foo)', 'instructions': []}]}
        self.assertEqual([f['name'] for f in qualify_anonymous(doc, 'm')['functions']],
                         ['anon-function-12x', '(method anon-function-1 foo)'])

    def test_static_references_follow(self):
        doc = dict(self.doc, statics=[
            {'id': 's1', 'kind': 'structure', 'relocations': [
                {'offset': 0, 'symbol': 'function:anon-function-1'},
                {'offset': 4, 'symbol': 'function:init-time-of-day'},
                {'offset': 8, 'symbol': 'type:anon-function-1'}]},
            {'id': 's2', 'kind': 'pair', 'car': {'kind': 'function', 'target': 'anon-function-1'},
             'cdr': {'kind': 'data', 'target': 's1'}}])
        statics = qualify_anonymous(doc, 'target-death')['statics']
        self.assertEqual([r['symbol'] for r in statics[0]['relocations']],
                         ['function:target-death:anon-function-1', 'function:init-time-of-day',
                          'type:anon-function-1'])
        self.assertEqual(statics[1]['car']['target'], 'target-death:anon-function-1')
        self.assertEqual(statics[1]['cdr'], {'kind': 'data', 'target': 's1'})
        self.assertEqual(doc['statics'][0]['relocations'][0]['symbol'], 'function:anon-function-1')

    def test_input_is_not_mutated(self):
        qualify_anonymous(self.doc, 'time-of-day')
        self.assertEqual(self.doc['functions'][0]['name'], 'anon-function-1')
        self.assertEqual(self.doc['functions'][1]['instructions'][0]['function'], 'anon-function-1')


if __name__ == '__main__':
    unittest.main()

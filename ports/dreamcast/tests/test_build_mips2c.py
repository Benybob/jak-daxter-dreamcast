"""build_mips2c.py's registration and signature parsing (AI-assisted), ISC.

Each registered mips2c name gets a C wrapper with the GOAL function's native
signature. These tests pin the parsing that decides it: registrations by
namespace (commented ones ignored), def-mips2c types, methods found by their
";; N" id on the type or an ancestor, define-extern types, and untyped helpers
(eight u64 arguments). A wrong float position would pass floats in the wrong
registers, so float arguments and results are checked explicitly.
"""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_mips2c import registrations, signature, split_type, table  # noqa: E402

ALL_TYPES = """
(deftype base-prim (basic)
  ()
  (:methods
    (resolve (_type_ object float int) float) ;; 9
    (other (_type_) none) ;; 10
    )
  )
(deftype child-prim (base-prim)
  ()
  (:methods
    (extra (_type_ int) symbol) ;; 11
    )
  )
(define-extern render-tri (function int dma-buffer none))
(define-extern helper function)
"""
GOAL = '(def-mips2c set-value (function float int float))\n'


class BuildMips2cTest(unittest.TestCase):
    def test_split_type(self):
        self.assertEqual(split_type('(function (pointer int) float none)'),
                         ['function', '(pointer int)', 'float', 'none'])
        self.assertEqual(split_type('function'), 'function')

    def test_signatures(self):
        self.assertEqual(signature('set-value', GOAL, ALL_TYPES), (['float', 'u64'], 'float'))
        self.assertEqual(signature('(method 9 child-prim)', GOAL, ALL_TYPES),
                         (['u64', 'u64', 'float', 'u64'], 'float'))
        self.assertEqual(signature('(method 11 child-prim)', GOAL, ALL_TYPES), (['u64', 'u64'], 'u64'))
        self.assertEqual(signature('render-tri', GOAL, ALL_TYPES), (['u64', 'u64'], 'u64'))
        self.assertEqual(signature('helper', GOAL, ALL_TYPES), (['u64'] * 8, 'u64'))
        self.assertEqual(signature('unknown-helper', GOAL, ALL_TYPES), (['u64'] * 8, 'u64'))
        with self.assertRaises(ValueError):
            signature('(method 12 child-prim)', GOAL, ALL_TYPES)

    def test_registrations_by_namespace(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'f.cpp'
            source.write_text('namespace Mips2C::jak1 {\nnamespace first {\nvoid link() {\n'
                              '  gLinkedFunctionTable.reg("one", execute, 64);\n'
                              '  // gLinkedFunctionTable.reg("ignored", execute, 64);\n}\n}\n}\n'
                              'namespace Mips2C::jak1 {\nnamespace second {\nvoid link() {\n'
                              '  gLinkedFunctionTable.reg("(method 9 child-prim)", execute, 128);\n}\n}\n}\n')
            self.assertEqual(registrations(source), [('first', 'one'), ('second', '(method 9 child-prim)')])

    def test_wrapper_passes_float_bits_and_returns_float(self):
        text = table([('second', '(method 9 child-prim)', (['u64', 'u64', 'float', 'u64'], 'float'))])
        self.assertIn('float dc_w0(u64 a0, u64 a1, float a2, u64 a3)', text)
        self.assertIn('{a0, a1, to_bits(a2), a3, 0, 0, 0, 0}', text)
        self.assertIn('return from_bits(dc_run(0, args));', text)
        self.assertIn('&Mips2C::jak1::second::link,', text)


if __name__ == '__main__':
    unittest.main()

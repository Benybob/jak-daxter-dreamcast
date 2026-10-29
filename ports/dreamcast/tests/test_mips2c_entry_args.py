"""build_mips2c.py's check that no generated entry drops an argument (AI-assisted), ISC.

A generated SH-4 entry passes only as many argument registers as its GOAL type
lists. Three functions read more than their type says (collide-cache methods 27
and 28, called through a cast, and collide-do-primitives, jumped to with the
caller's registers in place); their entries would have handed them zeros. The
build now compares each execute()'s argument registers read before written with
its entry. These tests pin that scan and run it over every built file.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_mips2c  # noqa: E402
from build_mips2c import argument_reads, check_arguments, registrations, signature  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]


def load_types():
    goal_text = '\n'.join(p.read_text(encoding='utf-8', errors='replace')
                          for p in sorted((ROOT / 'goal_src/jak1').rglob('*.gc')))
    all_types = (ROOT / 'decompiler/config/jak1/all-types.gc').read_text(encoding='utf-8', errors='replace')
    return goal_text, all_types


class ArgumentReadsTest(unittest.TestCase):
    def test_reads_before_writes(self):
        body = '\n'.join([
            '  c->mov64(s4, a0);',            # a0 read
            '  c->daddiu(a1, sp, 16);',       # a1 written first
            '  c->sw(a2, 0, a1);',            # a stored register is read
            '  get_fake_spad_addr(a3, cache.x, 0, c);',
            '  c->lw(t0, 4, a3);',            # a3 written above, t0 written here
            '  c->gprs[t1].du64[0] = 0;',
            '  bc = c->sgpr64(t2) == 0;',     # t2 read
            '  c->mov64(v1, t1);',
            '  // c->mov64(v1, t3);',         # comments ignored
        ])
        self.assertEqual(argument_reads(body), {'a0', 'a2', 't2'})


class EntryArgumentsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.goal_text, cls.all_types = load_types()

    def entries(self, source):
        return [(ns, name, signature(name, self.goal_text, self.all_types)) for ns, name in registrations(source)]

    def test_every_built_file_passes(self):
        for name in build_mips2c.DEFAULT_FILES:
            source = ROOT / 'game/mips2c/jak1_functions' / f'{name}.cpp'
            with self.subTest(file=name):
                check_arguments(source, self.entries(source))

    def test_the_dropped_arguments_are_caught(self):
        source = ROOT / 'game/mips2c/jak1_functions/collide_func.cpp'
        saved = build_mips2c.CALLED_AS.pop('collide-do-primitives')
        try:
            with self.assertRaisesRegex(ValueError, 'collide-do-primitives: reads a0,a1,a2,a3,t0'):
                check_arguments(source, self.entries(source))
        finally:
            build_mips2c.CALLED_AS['collide-do-primitives'] = saved


if __name__ == '__main__':
    unittest.main()

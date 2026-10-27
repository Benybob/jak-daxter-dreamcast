"""VU0 vf asm parsing and expectation generation (AI-assisted), ISC."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lower_sh4_ir import vector_asm  # noqa: E402
import gen_vf_cases  # noqa: E402


def op(text, reads, writes):
    return {'kind': 'unsupported', 'text': text, 'reads': reads, 'writes': writes}


class VectorAsmTest(unittest.TestCase):
    def test_three_operand_math(self):
        got = vector_asm(op('.mul.vf ivf-5, ivf-5, ivf-6', [5, 6], [5]))
        self.assertEqual((got['kind'], got['vop'], got['dst'], got['src1'], got['src2']), ('vf3', 'mul', 5, 5, 6))

    def test_repeated_source_listed_twice(self):
        got = vector_asm(op('.add.vf ivf-8, ivf-5, ivf-5', [5, 5], [8]))
        self.assertEqual((got['src1'], got['src2']), (5, 5))

    def test_xor_self_is_zero_with_no_reads(self):
        got = vector_asm(op('.xor.vf ivf-7, ivf-7, ivf-7', [7, 7], [7]))
        self.assertEqual((got['kind'], got['reads']), ('vfzero', []))

    def test_blend_mask_and_splat_control(self):
        got = vector_asm(op('.blend.vf ivf-5, ivf-5, ivf-8, 1', [5, 8], [5]))
        self.assertEqual((got['kind'], got['mask']), ('vfblend', 1))
        got = vector_asm(op('.splat.vf ivf-8, ivf-5, 2', [5], [8]))
        self.assertEqual((got['kind'], got['control']), ('vfswizzle', 0xaa))
        got = vector_asm(op('.swizzle.vf ivf-1, ivf-2, 9', [2], [1]))
        self.assertEqual(got['control'], 9)

    def test_refusals(self):
        refused = [
            op('.mul.vf :no-color xmm1, xmm1, xmm2', [], []),          # physical registers
            op('.mul.vf ivf-5, ivf-5, ivf-6', [5], [5]),                # reads disagree
            op('.mul.vf ivf-5, ivf-5, ivf-6', [5, 6], [6]),             # writes disagree
            op('.blend.vf ivf-5, ivf-5, ivf-8, 16', [5, 8], [5]),       # mask out of range
            op('.splat.vf ivf-8, ivf-5, 4', [5], [8]),                  # element out of range
            op('.swizzle.vf ivf-1, ivf-2, 256', [2], [1]),              # control out of range
            op('.mul.vf igpr-5, ivf-5, ivf-6', [5, 6], [5]),            # wrong register class
            op('.abs.vf ivf-1, ivf-2', [2], [1]),                       # not a lowered op
        ]
        for case in refused:
            self.assertIsNone(vector_asm(case), case['text'])
        self.assertIsNone(vector_asm(dict(op('.mul.vf ivf-5, ivf-5, ivf-6', [5, 6], [5]), kind='move')))


class ExpectationTest(unittest.TestCase):
    def test_cases_cover_every_form(self):
        sets, cases = gen_vf_cases.build()
        names = {name for name, _, _, _ in cases}
        self.assertTrue({name for name, _ in gen_vf_cases.FIXTURE} <= names)
        self.assertTrue({name for name, _, _ in gen_vf_cases.ORIGINAL} <= names)
        self.assertEqual(len(cases), 141)

    def test_hand_checked_lanes(self):
        F = gen_vf_cases.F
        a, b, c = (gen_vf_cases.lanes(v) for v in gen_vf_cases.GENERAL[0])
        self.assertEqual(gen_vf_cases.vector_dot_vu(a, b), F(1 * 5 + 2 * 6 + 3 * 7))
        self.assertEqual(gen_vf_cases.vector4_dot_vu(a, b), F(1 * 5 + 2 * 6 + 3 * 7 + 4 * 8))
        # vector+! takes w from the static <0,0,0,1>, whatever its docstring says.
        self.assertEqual(gen_vf_cases.vector_add(a, b), (F(6), F(8), F(10), F(1)))
        self.assertEqual(gen_vf_cases.ieee(a, b, 'outer'), (F(2 * 7), F(3 * 5), F(1 * 6), F(1 * 5)))
        self.assertEqual(gen_vf_cases.ftoi(gen_vf_cases.lanes((float('nan'), -2.5, 2.9999998, 3e9))),
                         (0x80000000, 0xfffffffe, 2, 0x80000000))


if __name__ == '__main__':
    unittest.main()

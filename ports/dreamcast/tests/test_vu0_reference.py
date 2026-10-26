"""Hand-checked architectural vectors, independent of lowering (AI-assisted)."""
from pathlib import Path
import ctypes
import os
import platform
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from vu0_reference import VU0, float_bits, bits_float


class VectorReferenceTest(unittest.TestCase):
    def setUp(self):
        self.v = VU0(bytearray(64))
        self.v.set_floats('vf1', (1, 2, 3, 4))
        self.v.set_floats('vf2', (5, 6, 7, 8))
        self.v.set_floats('vf3', (10, 20, 30, 40))
        self.v.set_floats('acc', (100, 200, 300, 400))

    def test_binary_all_masks_and_aliasing(self):
        for op, result in [('.add.vf', (6, 8, 10, 12)),
                           ('.sub.vf', (-4, -4, -4, -4)),
                           ('.mul.vf', (5, 12, 21, 32)),
                           ('.vmul', (5, 12, 21, 32))]:
            for mask in range(16):
                with self.subTest(op=op, mask=mask):
                    self.v.set_floats('vf1', (1, 2, 3, 4))
                    self.v.execute(op, 'vf1', 'vf1', 'vf2', mask=mask)
                    self.assertEqual(self.v.floats('vf1'), tuple(
                        result[i] if mask & (1 << i) else i + 1 for i in range(4)))

    def test_all_broadcasts_and_suffix_masks(self):
        for lane, value in zip('xyzw', (5, 6, 7, 8)):
            for op, expected in [('add', tuple(x + value for x in (1, 2, 3, 4))),
                                 ('sub', tuple(x - value for x in (1, 2, 3, 4))),
                                 ('mul', tuple(x * value for x in (1, 2, 3, 4)))]:
                for mask in range(1, 16):
                    suffix = ''.join(c for i, c in enumerate('xyzw') if mask & (1 << i))
                    with self.subTest(op=op, lane=lane, suffix=suffix):
                        self.v.set_floats('vf3', (10, 20, 30, 40))
                        self.v.execute(f'.{op}.{lane}.vf.{suffix}', 'vf3', 'vf1', 'vf2')
                        self.assertEqual(self.v.floats('vf3'), tuple(
                            expected[i] if mask & (1 << i) else (i+1)*10 for i in range(4)))
            self.v.execute('.vadd' + lane, 'vf4', 'vf1', 'vf2')
            self.assertEqual(self.v.floats('vf4'), tuple(x + value for x in (1, 2, 3, 4)))

    def test_original_dot_product_accumulator_sequence(self):
        self.v.set_floats('vf3', (1, 0, 0, 0))
        self.v.execute('.vmulax.x', 'acc', 'vf3', 'vf1')
        self.assertEqual(self.v.floats('acc'), (1, 200, 300, 400))
        self.v.execute('.vmadday.x', 'acc', 'vf3', 'vf1')
        self.v.execute('.vmaddaz.x', 'acc', 'vf3', 'vf1')
        self.v.execute('.vmaddw.x', 'vf4', 'vf3', 'vf1')
        self.assertEqual(self.v.floats('vf4'), (10, 0, 0, 0))
        self.assertEqual(self.v.floats('acc'), (6, 200, 300, 400))

    def test_active_explicit_accumulator(self):
        for lane, value in zip('xyzw', (5, 6, 7, 8)):
            self.v.execute(f'.add.mul.{lane}.vf', 'vf4', 'vf1', 'vf2', 'acc')
            self.assertEqual(self.v.floats('vf4'), tuple(x * value + c for x, c in
                                                       zip((1, 2, 3, 4), (100, 200, 300, 400))))
        self.v.execute('.add.mul.vf.x', 'vf4', 'vf1', 'vf2', 'vf3')
        self.assertEqual(self.v.floats('vf4'), (15, 216, 324, 432))

    def test_rounding_is_separate_not_fused(self):
        # (1+2^-23)*(1-2^-23) rounds to 1 before adding -1: +0.
        self.v.set_bits('vf1', (0x3f800001,) * 4)
        self.v.set_bits('vf2', (0x3f7ffffe,) * 4)
        self.v.set_floats('acc', (-1,) * 4)
        self.v.execute('.add.mul.vf', 'vf3', 'vf1', 'vf2', 'acc')
        self.assertEqual(self.v.bits('vf3'), (0,) * 4)
        self.v.set_bits('vf1', (0x3f800000, 0x3f800001, 0x00800000, 0x80000000))
        self.v.set_bits('vf2', (0x33800000, 0x33800000, 0x3f000000, 0x40000000))
        self.v.execute('.add.vf', 'vf3', 'vf1', 'vf2', mask=3)
        self.assertEqual(self.v.bits('vf3')[:2], (0x3f800000, 0x3f800002))
        self.v.execute('.mul.vf', 'vf3', 'vf1', 'vf2', mask=12)
        self.assertEqual(self.v.bits('vf3')[2:], (0x00400000, 0x80000000))

    def test_move_blend_xor_preserve_raw_bits_all_masks(self):
        a = (0x7fc12345, 0x80000000, 0x00000001, 0x7f800000)
        b = (0xffffffff, 0x7fa12345, 0x87654321, 0x12345678)
        self.v.set_bits('vf1', a); self.v.set_bits('vf2', b)
        for mask in range(16):
            self.v.set_bits('vf3', (42,) * 4)
            self.v.execute('.mov.vf', 'vf3', 'vf1', mask=mask)
            self.assertEqual(self.v.bits('vf3'), tuple(a[i] if mask & 1 << i else 42 for i in range(4)))
            self.v.execute('.blend.vf', 'vf3', 'vf1', 'vf2', mask=mask)
            expected = tuple(b[i] if mask & 1 << i else a[i] for i in range(4))
            self.assertEqual(self.v.bits('vf3'), expected)
            self.v.execute('.xor.vf', 'vf3', 'vf1', 'vf2', mask=mask)
            self.assertEqual(self.v.bits('vf3'), tuple(a[i] ^ b[i] if mask & 1 << i else expected[i] for i in range(4)))

    def test_memory_and_gpr_transfers(self):
        raw = (0x7fa12345, 0x80000000, 1, 0xfedcba98)
        struct.pack_into('<4I', self.v.memory, 16, *raw)
        self.v.execute('.lvf', 'vf1', 0, offset=16)
        self.v.execute('.svf', 32, 'vf1')
        self.assertEqual(self.v.memory[16:32], self.v.memory[32:48])
        self.v.execute('.lqc2', 'vf2', 16, 16)
        self.v.execute('.sqc2', 'vf2', 0, 0)
        self.assertEqual(self.v.memory[:16], self.v.memory[16:32])
        for op in ('.qmfc2', '.qmfc2.i'):
            self.v.execute(op, 'v0', 'vf2')
            self.assertEqual(self.v.gpr['v0'], int.from_bytes(self.v.memory[:16], 'little'))
            self.v.execute('.qmtc2', 'vf3', 'v0')
            self.assertEqual(self.v.bits('vf3'), raw)

    def test_vf0_is_immutable(self):
        self.v.set_bits('vf0', (0xffffffff,) * 4)
        self.v.execute('.add.vf', 'vf0', 'vf1', 'vf2')
        self.v.execute('.lvf', 'vf0', 0)
        self.assertEqual(self.v.bits('vf0'), (0, 0, 0, 0x3f800000))

    def test_accumulator_all_masks_and_broadcasts(self):
        for kind in ('mula', 'madda', 'madd'):
            for lane, value in zip('xyzw', (5, 6, 7, 8)):
                for mask in range(16):
                    with self.subTest(kind=kind, lane=lane, mask=mask):
                        self.v.set_floats('acc', (100, 200, 300, 400))
                        self.v.set_floats('vf4', (10, 20, 30, 40))
                        dest = 'vf4' if kind == 'madd' else 'acc'
                        old = self.v.floats(dest)
                        self.v.execute('.v' + kind + lane, dest, 'vf1', 'vf2', mask=mask)
                        expected = tuple((i+1)*value + (0 if kind == 'mula' else (i+1)*100)
                                         if mask & (1 << i) else old[i] for i in range(4))
                        self.assertEqual(self.v.floats(dest), expected)

    def test_fail_closed_without_partial_mutation(self):
        for op, args, kw in [('.unknown', ('vf3',), {}),
                              ('.add.vf', ('vf3', 'vf1', 'vf32'), {}),
                              ('.add.vf', ('vf3', 'vf1', 'vf2'), {'mask': 16}),
                              ('.add.vf.yx', ('vf3', 'vf1', 'vf2'), {}),
                              ('.add.vf.x', ('vf3', 'vf1', 'vf2'), {'mask': 1}),
                              ('.lvf', ('vf3', -16), {}),
                              ('.svf', (1, 'vf1'), {}),
                              ('.lvf', ('vf3', 64), {}),
                              ('.vmulax', ('vf3', 'vf1', 'vf2'), {})]:
            before = (self.v.bits('vf3'), bytes(self.v.memory))
            with self.subTest(op=op, args=args), self.assertRaises(ValueError):
                self.v.execute(op, *args, **kw)
            self.assertEqual((self.v.bits('vf3'), bytes(self.v.memory)), before)
        self.v.set_bits('vf2', (0x3f800000, 0x7fc12345, 0, 0))
        before = self.v.bits('vf3')
        with self.assertRaises(ValueError):
            self.v.execute('.add.vf', 'vf3', 'vf1', 'vf2')
        self.assertEqual(self.v.bits('vf3'), before)
        self.v.gpr['bad'] = -1
        with self.assertRaises(ValueError):
            self.v.execute('.qmtc2', 'vf3', 'bad')
        self.assertEqual(self.v.bits('vf3'), before)


@unittest.skipUnless(shutil.which('gcc') and platform.machine().lower() in ('amd64', 'x86_64'),
                     'x86-64 GCC required for independent SSE comparison')
class DesktopSSEComparisonTest(unittest.TestCase):
    def test_deterministic_finite_bit_patterns(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / ('oracle.dll' if os.name == 'nt' else 'oracle.so')
            source = Path(__file__).with_name('vu0_sse_bridge.c')
            run = subprocess.run([shutil.which('gcc'), '-std=c99', '-O2', '-Wall', '-Wextra',
                                  '-Werror', '-pedantic', '-ffp-contract=off', '-msse2', '-shared',
                                  '-fPIC', str(source), '-o', str(library)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            bridge = ctypes.CDLL(str(library))
            lane = bridge.vu0_sse_lane
            lane.argtypes = [ctypes.c_uint32] * 4
            lane.restype = ctypes.c_uint32
            rng = random.Random(0x565530)
            v = VU0()
            compared = 0
            for n in range(2048):
                # Wide finite exponent range plus hand-picked tie/denormal cases above.
                a = tuple(rng.getrandbits(32) & 0xfeffffff for _ in range(4))
                b = tuple(rng.getrandbits(32) & 0xfeffffff for _ in range(4))
                c = tuple(rng.getrandbits(32) & 0xfeffffff for _ in range(4))
                v.set_bits('vf1', a); v.set_bits('vf2', b); v.set_bits('acc', c)
                for op, spelling in enumerate(('.add.vf', '.sub.vf', '.mul.vf', '.add.mul.vf')):
                    args = ('vf3', 'vf1', 'vf2') + (('acc',) if op == 3 else ())
                    try:
                        v.execute(spelling, *args)
                    except ValueError:
                        self.assertEqual(op, 3)  # overflowing intermediate is outside profile
                        continue
                    with self.subTest(vector=n, op=spelling):
                        self.assertEqual(v.bits('vf3'), tuple(lane(op, a[i], b[i], c[i]) for i in range(4)))
                        compared += 4
            self.assertGreaterEqual(compared, 24576)
            # Windows keeps loaded DLLs mapped; unload before TemporaryDirectory cleanup.
            if os.name == 'nt':
                import _ctypes
                _ctypes.FreeLibrary(bridge._handle)


if __name__ == '__main__':
    unittest.main()

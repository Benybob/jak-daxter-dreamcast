"""PS2 MMI helper and native pad services, built for the host (AI-assisted), ISC.

goal_mmi.c is compared with an independent Python model of the x86 instructions
the desktop backend emits for each kind (operand order from goalc IR.cpp).
goal_pad.c's mapping and its CPadOpen/CPadGetData behavior without a controller
are checked directly. Needs an x86-64 GCC (MinGW) on PATH; skipped otherwise.
"""
import ctypes
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
MMI3 = ['pextlb', 'pextlh', 'pextlw', 'pextub', 'pextuh', 'pextuw', 'pceqb', 'pceqh', 'pceqw', 'pcgtb', 'pcgth',
        'pcgtw', 'psubw', 'por', 'pxor', 'pand', 'packuswb', 'paddb']
MMI2 = ['pw.sll', 'pw.srl', 'pw.sra', 'ph.sll', 'ph.srl', 'VPSRLDQ', 'VPSLLDQ', 'VPSHUFLW', 'VPSHUFHW']


def elems(v, size):
    return [int.from_bytes(v[i:i + size], 'little') for i in range(0, 16, size)]


def pack(values, size):
    return b''.join((x & ((1 << 8 * size) - 1)).to_bytes(size, 'little') for x in values)


def signed(x, size):
    bits = 8 * size
    return x - (1 << bits) if x >> (bits - 1) else x


def unpack(first, second, size, high):
    """x86 punpck{l,h}: interleave the low/high halves, first operand first."""
    a, b = elems(first, size), elems(second, size)
    n = 8 // size
    half = slice(n, 2 * n) if high else slice(0, n)
    return pack([x for pair in zip(a[half], b[half]) for x in pair], size)


def model(name, a, b, imm=0):
    """Independent model of each IR kind (src1=a, src2=b) via its x86 instruction."""
    if name.startswith('pext'):
        size = {'b': 1, 'h': 2, 'w': 4}[name[-1]]
        return unpack(b, a, size, name[4] == 'u')  # *_swapped: x86 (src2, src1)
    if name.startswith('pceq'):
        size = {'b': 1, 'h': 2, 'w': 4}[name[-1]]
        return pack([-1 if x == y else 0 for x, y in zip(elems(a, size), elems(b, size))], size)
    if name.startswith('pcgt'):
        size = {'b': 1, 'h': 2, 'w': 4}[name[-1]]
        return pack([-1 if signed(x, size) > signed(y, size) else 0
                     for x, y in zip(elems(a, size), elems(b, size))], size)
    if name == 'psubw':
        return pack([x - y for x, y in zip(elems(a, 4), elems(b, 4))], 4)
    if name in ('por', 'pxor', 'pand'):
        op = {'por': lambda x, y: x | y, 'pxor': lambda x, y: x ^ y, 'pand': lambda x, y: x & y}[name]
        return bytes(op(x, y) for x, y in zip(a, b))
    if name == 'packuswb':
        words = [signed(x, 2) for x in elems(a, 2) + elems(b, 2)]
        return bytes(min(255, max(0, w)) for w in words)
    if name == 'paddb':
        return bytes((x + y) & 255 for x, y in zip(a, b))
    if name in ('pw.sll', 'pw.srl', 'pw.sra', 'ph.sll', 'ph.srl'):
        size = 4 if name[1] == 'w' else 2
        bits = 8 * size
        out = []
        for x in elems(a, size):
            if name.endswith('sll'):
                out.append(0 if imm >= bits else x << imm)
            elif name.endswith('srl'):
                out.append(0 if imm >= bits else x >> imm)
            else:
                out.append(signed(x, size) >> min(imm, bits - 1))
        return pack(out, size)
    if name == 'VPSRLDQ':
        return bytes(a[i + imm] if imm <= 15 and i + imm < 16 else 0 for i in range(16))
    if name == 'VPSLLDQ':
        return bytes(a[i - imm] if imm <= 15 and i >= imm else 0 for i in range(16))
    if name in ('VPSHUFLW', 'VPSHUFHW'):
        w = elems(a, 2)
        base = 0 if name == 'VPSHUFLW' else 4
        sel = [w[base + ((imm >> 2 * i) & 3)] for i in range(4)]
        return pack(sel + w[4:] if base == 0 else w[:4] + sel, 2)
    raise ValueError(name)


@unittest.skipUnless(shutil.which('gcc'), 'x86-64 GCC required to build the host helpers')
class MmiPadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        lib = Path(cls.tmp.name) / 'helpers.dll'
        subprocess.run(['gcc', '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror', '-shared', '-o', str(lib),
                        str(PORT / 'goal_mmi.c'), str(PORT / 'goal_pad.c')], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.dc_goal_mmi.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        del cls.lib
        try:
            cls.tmp.cleanup()
        except OSError:
            pass  # Windows keeps the loaded DLL open

    def run_op(self, code, a, b):
        out = ctypes.create_string_buffer(16)
        ok = self.lib.dc_goal_mmi(code, out, ctypes.c_char_p(a), ctypes.c_char_p(b) if b is not None else None)
        return ok, out.raw

    def test_every_kind_matches_the_x86_model(self):
        rng = random.Random(1987)
        specials = [bytes(16), bytes([255]) * 16, bytes(range(16)), pack([0x7fff, 0x8000, 0x0100, 0xff00] * 2, 2)]
        for i, name in enumerate(MMI3):
            for trial in range(64):
                a = specials[trial] if trial < len(specials) else rng.randbytes(16)
                b = rng.randbytes(16) if trial % 3 else a
                ok, got = self.run_op(1 + i, a, b)
                self.assertEqual((ok, got), (1, model(name, a, b)), f'{name} trial {trial}')
        for i, name in enumerate(MMI2):
            for imm in (0, 1, 3, 7, 8, 15, 16, 31, 32, 0x1b, 0xe4, 200):
                a = rng.randbytes(16)
                ok, got = self.run_op(32 + i | imm << 8, a, None)
                self.assertEqual((ok, got), (1, model(name, a, None, imm)), f'{name} imm {imm}')

    def test_aliasing_and_unknown_kind(self):
        buf = ctypes.create_string_buffer(bytes(range(16)), 16)
        self.lib.dc_goal_mmi(1 + MMI3.index('paddb'), buf, buf, buf)
        self.assertEqual(buf.raw, bytes(2 * i for i in range(16)))
        before = bytes(buf.raw)
        self.assertEqual(self.lib.dc_goal_mmi(99, buf, buf, buf), 0)
        self.assertEqual(buf.raw, before)

    def test_pad_mapping_fills_the_hw_cpad_layout(self):
        class State(ctypes.Structure):
            _fields_ = [('buttons', ctypes.c_uint32), ('ltrig', ctypes.c_int), ('rtrig', ctypes.c_int),
                        ('joyx', ctypes.c_int), ('joyy', ctypes.c_int)]
        A, B, START, UP, X, Y = 1 << 2, 1 << 1, 1 << 3, 1 << 4, 1 << 10, 1 << 9
        cpad = ctypes.create_string_buffer(132)
        self.lib.dc_goal_pad_fill(cpad, ctypes.byref(State(A | START | UP | Y, 200, 10, -128, 127)))
        raw = cpad.raw
        button0 = struct.unpack_from('<H', raw, 2)[0]
        expected = (1 << 14) | (1 << 3) | (1 << 4) | (1 << 12) | (1 << 10)  # x start up triangle l1
        self.assertEqual((raw[0], raw[1], button0), (0, 0x7a, expected))
        self.assertEqual((raw[4], raw[5], raw[6], raw[7]), (128, 128, 0, 255))
        self.lib.dc_goal_pad_fill(cpad, ctypes.byref(State(B | X, 0, 255, 0, 0)))
        self.assertEqual(struct.unpack_from('<H', cpad.raw, 2)[0], (1 << 13) | (1 << 15) | (1 << 11))

    def test_open_and_disconnected_get_data(self):
        cpad = ctypes.create_string_buffer(132)
        struct.pack_into('<i', cpad, 32, 1)          # number
        struct.pack_into('<i', cpad, 80, 99)         # state
        self.lib.dc_goal_cpad_open(cpad, 1)
        self.assertEqual(struct.unpack_from('<iiii', cpad.raw, 32)[:2], (1, 2))  # cpad-file = port + 1
        self.assertEqual(struct.unpack_from('<ii', cpad.raw, 76), (1, 0))       # new-pad, state
        struct.pack_into('<i', cpad, 36, 7)
        self.lib.dc_goal_cpad_open(cpad, 1)                                      # already open: unchanged
        self.assertEqual(struct.unpack_from('<i', cpad.raw, 36)[0], 7)
        struct.pack_into('<i', cpad, 80, 99)
        self.lib.dc_goal_cpad_get_data(cpad)                                     # host: no controller
        self.assertEqual((cpad.raw[0], struct.unpack_from('<i', cpad.raw, 80)[0]), (0x80, 0))


if __name__ == '__main__':
    unittest.main()

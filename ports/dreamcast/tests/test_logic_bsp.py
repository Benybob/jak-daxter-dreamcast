"""Logic-BSP link tables round-trip through the v4 decoder (AI-assisted), ISC.

Synthetic objects only; no retail bytes. The retail build is exercised by
build_logic_bsp.py on the owner's DGO, which checks its own round trip.
"""
from pathlib import Path
import struct
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import build_logic_bsp as logic  # noqa: E402
import stage_jak_art as stage  # noqa: E402


def v4(data, pointers, bindings):
    table = logic.encode_table(pointers, bindings, len(data))
    return struct.pack('<4I', 0xffffffff, len(table), 4, len(data)) + bytes(data) + table


class LogicBspTables(unittest.TestCase):
    def check(self, size, pointers, bindings):
        data = bytearray(size)
        for at in pointers:
            struct.pack_into('<I', data, at, 16)  # any in-object target
        for _, _, sites in bindings:
            for at in sites:
                struct.pack_into('<I', data, at, 0xffffffff)
        _, got_pointers, got_bindings = stage.decode_v4(v4(data, pointers, bindings), 'bsp-header')
        self.assertEqual(sorted(got_pointers), sorted(pointers))
        self.assertEqual(sorted((n, m, sorted(s)) for n, m, s in got_bindings),
                         sorted((n, m, sorted(s)) for n, m, s in bindings))

    def test_short_runs(self):
        self.check(256, [16, 20, 24, 64], [('bsp-header', 9, [0]), ('symbol-a', None, [32, 96])])

    def test_runs_of_255_and_more(self):
        size = 4 * 2000
        pointers = list(range(4 * 3, 4 * 258, 4)) + list(range(4 * 600, 4 * 1110, 4)) + [4 * 1999]
        self.check(size, pointers, [('bsp-header', 9, [0])])

    def test_exact_multiple_of_255(self):
        pointers = list(range(4 * 5, 4 * 260, 4))  # exactly 255 words
        self.check(4 * 600, pointers, [('bsp-header', 9, [0])])

    def test_wide_binding_deltas(self):
        size = 1 << 25
        sites = [0, 252, 256, 70000, 70004, (1 << 24) + 16, (1 << 25) - 16]
        self.check(size, [8], [('bsp-header', 9, [0]), ('drawable-tree-actor', 12, sites[1:])])

    def test_pointer_at_zero_refused(self):
        with self.assertRaises(ValueError):
            logic.encode_pointers([0], 64)


if __name__ == '__main__':
    unittest.main()

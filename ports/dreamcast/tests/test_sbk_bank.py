"""Synthetic silent-bank checks for sbk_bank.py. No disc data (AI-assisted), ISC.

Drafted by Grok (read-only report), reviewed against game/sound/989snd/loader.cpp.
"""

from pathlib import Path
import ast
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbk_bank
import vag_adpcm
from sbk_bank import parse_sbk
from vag_adpcm import decode_vag


def fixture(prefix=2048, count=2, version=2, kind=1):
    """Build one .sbk in memory.

    The prefix is filled with 0xCD so a span that includes it cannot pass as
    the sample block. The u32 at offset 16 is deliberately not the count.
    The sample is one SPU block: filter 0, shift 0, flags 1, nibbles 1 then
    15. (AI-assisted), ISC.
    """
    names = bytearray(b'\xcd' * prefix)
    names[:16] = b'SYNTHETIC_BANK!!'
    struct.pack_into('<II', names, 16, 0x12345678, count)
    for index in range(count):
        name = index.to_bytes(4, 'big') + b'abcdefghijkl'
        struct.pack_into('<16sI', names, 24 + index * 20, name, 0x90014003 + index)
    block = bytearray(64 if version == 2 else 60)
    block[:4] = b'SBlk'
    struct.pack_into('<III', block, 4, version, 0, 0x12345678)
    struct.pack_into('<hhh', block, 22, 0, 0, 1)
    struct.pack_into('<II', block, 40, 16, 4096)
    sample = bytes([0, 1, 0xF1]) + bytes(13)
    image = (struct.pack('<6I', kind, 2, 32, len(block), 128, len(sample))
             + b'\xef' * 8 + block)
    image += b'\xab' * (128 - len(image)) + sample + b'TRAILING'
    return bytes(names) + image


class SilentBankTest(unittest.TestCase):
    def test_directory_sample_span_and_metadata(self):
        for prefix in (2048, 20480):
            for version in (1, 2):
                for kind in (1, 3):
                    data = fixture(prefix, version=version, kind=kind)
                    bank = parse_sbk(data, prefix_bytes=prefix)
                    self.assertEqual(bank.name, b'SYNTHETIC_BANK!!')
                    self.assertEqual(len(bank.sounds), 2)
                    self.assertEqual(bank.sounds[0].name, bytes(4) + b'abcdefghijkl')
                    self.assertEqual(bank.sounds[0].falloff, 0x90014003)
                    self.assertEqual((bank.sounds[0].falloff_min,
                                      bank.sounds[0].falloff_max,
                                      bank.sounds[0].falloff_curve), (3, 5, 9))
                    self.assertGreaterEqual(bank.samples.offset, prefix)
                    self.assertEqual(bank.samples.offset, prefix + 128)
                    self.assertEqual(bank.samples.data, data[prefix + 128:prefix + 144])
                    self.assertFalse(bank.samples.data.startswith(b'\xcd'))
                    self.assertEqual(bank.program.offset, prefix + 32)
                    self.assertEqual(bank.sblk.version, version)
                    self.assertEqual(bank.sblk.bank_id, 0x12345678)
                    self.assertEqual(bank.sblk.vag_data_size, 16)
                    self.assertEqual(bank.sblk.sram_alloc_size, 4096)
                    self.assertEqual(decode_vag(bank.samples.data).samples,
                                     (4096, -4096) + (0,) * 26)

    def test_full_slots_read_past_the_prefix(self):
        for prefix, count in ((2048, 101), (20480, 1022)):
            data = fixture(prefix, count)
            bank = parse_sbk(data, prefix_bytes=prefix)
            self.assertEqual(len(bank.sounds), count)
            self.assertEqual(bank.sounds[-1].name,
                             (count - 1).to_bytes(4, 'big') + b'abcdefghijkl')
            self.assertGreaterEqual(bank.samples.offset, prefix)
            self.assertEqual(decode_vag(bank.samples.data).samples,
                             (4096, -4096) + (0,) * 26)
            overflow = bytearray(data)
            struct.pack_into('<I', overflow, 20, count + 1)
            with self.assertRaisesRegex(ValueError, 'name count'):
                parse_sbk(overflow, prefix_bytes=prefix)
        with self.assertRaises(ValueError):
            parse_sbk(fixture(20480, 1022))

    def test_exact_sixteen_byte_keys(self):
        data = bytearray(fixture())
        data[24:40] = b'jump\0ABCDEFGHIJK'
        bank = parse_sbk(data)
        self.assertIs(bank.find_sound(b'jump\0ABCDEFGHIJK'), bank.sounds[0])
        self.assertIsNone(bank.find_sound(b'jump' + bytes(12)))
        for name in (b'jump', 'jump', b'x' * 17):
            with self.assertRaises(ValueError):
                bank.find_sound(name)

    def test_truncation_and_malformed_directory(self):
        good = fixture()
        for length in (0, 20, 2047, 2048, 2071, 2080, 2176, 2191):
            with self.assertRaises(ValueError, msg=str(length)):
                parse_sbk(good[:length])
        changes = [(2048, 0), (2052, 0xFFFFFFFF), (2056, 0),
                   (2060, 0xFFFFFFFF), (2064, 40), (2064, 0xFFFFFFFF),
                   (2068, 15), (2080, 0), (2084, 3)]
        for offset, value in changes:
            data = bytearray(good)
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError, msg=str((offset, value))):
                parse_sbk(data)
        data = bytearray(good)
        struct.pack_into('<h', data, 2048 + 32 + 22, -1)
        with self.assertRaises(ValueError):
            parse_sbk(data)
        with self.assertRaises(ValueError):
            parse_sbk(good, prefix_bytes=4096)
        with self.assertRaises(TypeError):
            parse_sbk('not bytes')

    def test_imports_are_standard_library_or_these_modules(self):
        allowed = sys.stdlib_module_names | {'sbk_bank', 'vag_adpcm'}
        for path in (Path(sbk_bank.__file__), Path(__file__)):
            for node in ast.walk(ast.parse(path.read_text(encoding='utf-8'))):
                names = ([alias.name for alias in node.names] if isinstance(node, ast.Import)
                         else [node.module] if isinstance(node, ast.ImportFrom) else [])
                for name in names:
                    self.assertIn(name.split('.')[0], allowed)


if __name__ == '__main__':
    unittest.main()

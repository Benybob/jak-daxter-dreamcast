"""Synthetic silent-bank milestone; no retail files. (AI-assisted), ISC."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sbk_bank import parse_sbk
from vag_adpcm import decode_vag


def fixture(prefix=2048, count=2, version=2, kind=1):
    """Build file sections separately, with conspicuous non-sample padding."""
    names = bytearray(b'\xcd' * prefix)
    names[:16] = b'SYNTHETIC_BANK!!'
    # Reserved word deliberately nonzero: not a count or a prefix selector.
    struct.pack_into('<II', names, 16, 0x12345678, count)
    for i in range(count):
        name = i.to_bytes(4, 'big') + b'abcdefghijkl'
        struct.pack_into('<16sI', names, 24 + i*20, name, 0x90014003 + i)
    block = bytearray(64 if version == 2 else 60)
    block[:4] = b'SBlk'
    struct.pack_into('<III', block, 4, version, 0, 0x12345678)
    struct.pack_into('<hhh', block, 22, 0, 0, 1)
    struct.pack_into('<II', block, 40, 16, 4096)
    # One signed SPU block: literal residuals 4096 and -4096, then zeros.
    sample = bytes([0, 1, 0xf1]) + bytes(13)
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
                    self.assertEqual(bank.samples.offset, prefix + 128)
                    self.assertEqual(bank.samples.data, data[prefix+128:prefix+144])
                    self.assertEqual(bank.program.offset, prefix + 32)
                    self.assertEqual(bank.sblk.version, version)
                    self.assertEqual(bank.sblk.bank_id, 0x12345678)
                    self.assertEqual(bank.sblk.vag_data_size, 16)
                    self.assertEqual(bank.sblk.sram_alloc_size, 4096)
                    self.assertEqual(decode_vag(bank.samples.data).samples,
                                     (4096, -4096) + (0,) * 26)

    def test_full_slots_and_prefix_overflow(self):
        for prefix, count in ((2048, 101), (20480, 1022)):
            bank = parse_sbk(fixture(prefix, count), prefix_bytes=prefix)
            self.assertEqual(len(bank.sounds), count)
            self.assertEqual(bank.sounds[-1].name,
                             (count - 1).to_bytes(4, 'big') + b'abcdefghijkl')
            data = bytearray(fixture(prefix, count))
            struct.pack_into('<I', data, 20, count + 1)
            with self.assertRaisesRegex(ValueError, 'name count'):
                parse_sbk(data, prefix_bytes=prefix)
            # Selecting the wrong slot must not infer the prefix from count.
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
        # Header, count, chunk offset/size, SBlk magic/version/count and alignment.
        changes = [(2048, 0), (2052, 0xffffffff), (2056, 0),
                   (2060, 0xffffffff), (2064, 40), (2064, 0xffffffff),
                   (2068, 15), (2080, 0), (2084, 3)]
        for offset, value in changes:
            data = bytearray(good)
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError, msg=str((offset, value))):
                parse_sbk(data)
        data = bytearray(good)
        struct.pack_into('<h', data, 2048+32+22, -1)
        with self.assertRaises(ValueError):
            parse_sbk(data)
        with self.assertRaises(ValueError):
            parse_sbk(good, prefix_bytes=4096)
        with self.assertRaises(TypeError):
            parse_sbk('not bytes')


if __name__ == '__main__':
    unittest.main()

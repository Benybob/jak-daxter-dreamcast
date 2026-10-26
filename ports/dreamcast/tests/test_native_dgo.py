"""Native DGO packer layout (AI-assisted), ISC."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pack_native_dgo import pack_dgo  # noqa: E402


class NativeDgoTest(unittest.TestCase):
    def test_layout_matches_original_container(self):
        a = b'DCSH' + b'\x01' * 61   # 65 bytes: 15 bytes of padding
        b = b'DCSH' + b'\x02' * 12   # 16 bytes: no padding
        data = pack_dgo('KERNEL.CGO', [('gcommon', a), ('gstate', b)])
        self.assertEqual(struct.unpack_from('<I', data, 0)[0], 2)
        self.assertEqual(data[4:64].rstrip(b'\0'), b'KERNEL.CGO')
        self.assertEqual(struct.unpack_from('<I', data, 64)[0], 65)
        self.assertEqual(data[68:128].rstrip(b'\0'), b'gcommon')
        self.assertEqual(data[128:193], a)
        self.assertEqual(data[193:208], b'\0' * 15)
        self.assertEqual(struct.unpack_from('<I', data, 208)[0], 16)
        self.assertEqual(data[272:288], b)
        self.assertEqual(len(data), 288)

    def test_refuses_bad_names_and_payloads(self):
        with self.assertRaises(ValueError):
            pack_dgo('x' * 60, [('a', b'DCSH')])
        with self.assertRaises(ValueError):
            pack_dgo('K', [('', b'DCSH')])
        with self.assertRaises(ValueError):
            pack_dgo('K', [('a', b'\x7fELF')])


if __name__ == '__main__':
    unittest.main()

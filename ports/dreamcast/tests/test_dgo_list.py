"""List packed synthetic native objects without executing them (AI-assisted)."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from pack_native_dgo import HEADER, list_dgo, pack_dgo, write_dgo


class DGOListTest(unittest.TestCase):
    def test_pack_two_objects_then_list_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a, b = root / 'a.dcn', root / 'b.dcn'
            a.write_bytes(b'DCSHa'); b.write_bytes(b'DCSH' + bytes(16))
            data = write_dgo(root / 'test.cgo', 'TEST.CGO', [('first', a), ('second', b)])
            self.assertEqual(list_dgo(data), dict(name='TEST.CGO', total=25,
                                                objects=[dict(name='first', size=5),
                                                         dict(name='second', size=20)]))
            out = subprocess.run([sys.executable, str(PORT / 'pack_native_dgo.py'), '--list',
                                  str(root / 'test.cgo')], text=True, capture_output=True, check=True).stdout
            self.assertEqual(out.splitlines(), ['first\t5', 'second\t20',
                                               'total: 2 objects, 25 payload bytes'])

    def test_empty_archive(self):
        self.assertEqual(list_dgo(pack_dgo('EMPTY.CGO', [])), dict(name='EMPTY.CGO', objects=[], total=0))

    def test_truncation_padding_and_trailer_refuse(self):
        data = pack_dgo('TEST.CGO', [('a', b'DCSHx')])
        corrupt_padding = bytearray(data); corrupt_padding[-1] = 1
        corrupt_name = bytearray(data); corrupt_name[4:64] = b'x' * 60
        corrupt_payload = bytearray(data); corrupt_payload[2 * HEADER.size] = 0
        for bad in (b'', data[:HEADER.size - 1], data[:-1], data + b'x',
                    corrupt_padding, corrupt_name, corrupt_payload):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                list_dgo(bad)


if __name__ == '__main__':
    unittest.main()

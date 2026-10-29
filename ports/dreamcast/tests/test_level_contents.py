"""level_contents.py's DGO and v4 link-table parsing (AI-assisted), ISC.

Synthetic bytes only: a DGO with a code object, a texture page, an art group and a
BSP whose v4 link table types two basics, so no game files are needed.
"""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import level_contents as lc  # noqa: E402


def v4_object(code, link_body):
    """A v4 object: header, data segment, then a v2 link block."""
    link = struct.pack('<3I', 0xffffffff, len(link_body) + 12, 2) + link_body
    return struct.pack('<4I', 0xffffffff, len(link), 4, len(code)) + code + link


def v3_object(size):
    return struct.pack('<4I', 0, size, 3, 3) + bytes(size - 16)


def dgo(entries):
    out = struct.pack('<I', len(entries)) + b'TEST.DGO'.ljust(60, b'\0')
    for name, body in entries:
        out += struct.pack('<I', len(body)) + name.encode().ljust(60, b'\0') + body
    return out


# Two basics: type 'aaa' at offset 0 (16 bytes), 'bbb' at offset 16 (32 bytes).
CODE = struct.pack('<12I', 0xffffffff, 1, 2, 3, 0xffffffff, 4, 5, 6, 7, 8, 9, 10)
# No pointer table (0), then two type links (0x81: one method) each with one seek.
LINK = b'\0' + b'\x81aaa\0' + b'\x00\0' + b'\x81bbb\0' + b'\x10\0' + b'\0'


class LevelContentsTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name) / 'TEST.DGO'
        bsp = v4_object(CODE, LINK)
        self.path.write_bytes(dgo([('thing', v3_object(64)), ('tpage-1', v4_object(bytes(32), b'\0\0')),
                                   ('thing', v4_object(bytes(48), b'\0\0')), ('test-vis', bsp)]))

    def tearDown(self):
        self.dir.cleanup()

    def test_kinds(self):
        kinds = [(name, kind) for name, _, kind in lc.objects(self.path)]
        self.assertEqual(kinds, [('thing', 'code'), ('tpage-1', 'textures'), ('thing', 'art'), ('test-vis', 'level')])

    def test_basics_and_owned_bytes(self):
        obj = lc.object_bytes(self.path, 'test-vis')
        self.assertEqual(lc.basics(obj), (48, [(0, 'aaa'), (16, 'bbb')]))
        self.assertEqual(lc.by_type(obj), (48, {'aaa': 16, 'bbb': 32}))
        self.assertEqual(lc.bsp_categories(obj)['other'], 48)


if __name__ == '__main__':
    unittest.main()

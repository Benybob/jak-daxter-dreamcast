"""instanced_level.py's prototype, instance and density reading (AI-assisted), ISC.

Synthetic bytes only: a v4 BSP with one TIE prototype (a bucket, its prototype-tie
and one tie-fragment) and two instances of it in different 16 m cells.
"""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import instanced_level  # noqa: E402


def v4_object(code, link_body):
    link = struct.pack('<3I', 0xffffffff, len(link_body) + 12, 2) + link_body
    return struct.pack('<4I', 0xffffffff, len(link), 4, len(code)) + code + link


def bsp():
    code = bytearray(400)
    for slot in (0, 32, 128, 176, 240, 320):  # type slots; each basic's pointer is slot + 4
        struct.pack_into('<I', code, slot, 0xffffffff)
    struct.pack_into('<I4s', code, 4, 4, b'rock')       # the name string
    struct.pack_into('<I', code, 36, 4)                  # bucket: name -> the string
    struct.pack_into('<I', code, 48, 132)                # bucket: geometry[0] -> the prototype-tie
    struct.pack_into('<h', code, 134, 1)                 # prototype-tie: one fragment
    struct.pack_into('<3H', code, 212, 10, 2, 8)         # tie-fragment: vertex-count 10, color-count, num-tris 8
    for slot, x in ((240, 20.0), (320, 40.0)):           # instances: bucket-ptr, then the bsphere
        struct.pack_into('<I4f', code, slot + 12, 36, x * 4096, 0.0, 3 * 4096, 4096)
    link = b'\0'
    for name, seeks in (('string', [0]), ('prototype-bucket-tie', [32]), ('prototype-tie', [128]),
                        ('tie-fragment', [176]), ('instance-tie', [240, 80])):
        link += b'\x81' + name.encode() + b'\0' + bytes(seeks) + b'\0'
    return v4_object(bytes(code), link + b'\0')


class InstancedLevelTest(unittest.TestCase):
    def test_model_and_density(self):
        prototypes, instances = instanced_level.model(bsp())
        self.assertEqual(len(prototypes), 1)
        self.assertEqual(prototypes[0]['name'], 'rock')
        self.assertEqual(prototypes[0]['lods'][0], {'verts': 10, 'tris': 8})
        self.assertEqual(prototypes[0]['instances'], 2)
        self.assertEqual([round(i['x']) for i in instances], [20, 40])
        s = instanced_level.summary(prototypes, instances, 16.0)
        self.assertEqual(s['tie']['expanded_tris_lod0'], 16)
        self.assertEqual(s['density']['cells'], 2)
        self.assertEqual(s['density']['lod0_tris_max'], 8)
        self.assertEqual(s['density']['lod0_tris_window_3x3_max'], 16)


if __name__ == '__main__':
    unittest.main()

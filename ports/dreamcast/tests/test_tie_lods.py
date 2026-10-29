"""tie_lods.py: the decompiler dump reader and LOD geometry building (AI-assisted), ISC.

Synthetic OBJ files only.
"""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tie_lods  # noqa: E402

OBJ = """# strip dump
v 0 0 0
v 0.0625 0 0
v 0 0.0625 0
v 0.0625 0 0
vt 0 0
f 1/1 2/1 3/1
f 2/1 3/1 4/1
f 1/1 2/1 4/1
"""


class TieLodsTest(unittest.TestCase):
    def test_obj_is_sixteenths_of_a_meter_and_drops_degenerate_joins(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'rock.obj'
            path.write_text(OBJ)
            tris = tie_lods.read_obj(path)
        # face 2 repeats vertex 2 as 4 (a strip join, zero area); face 3 is degenerate too
        self.assertEqual(tris, [((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))])

    def test_dump_lods_reads_every_geometry_and_skips_all(self):
        with tempfile.TemporaryDirectory() as tmp:
            for lod in (0, 1, 3):
                d = Path(tmp) / f'lod{lod}-tie-TEST.DGO-2-tie'
                d.mkdir()
                (d / 'rock.mb.obj').write_text(OBJ)
                (d / 'ALL.obj').write_text(OBJ)
            lods = tie_lods.dump_lods(tmp)
        self.assertEqual(list(lods), ['rock.mb'])
        self.assertEqual([len(x) if x is not None else None for x in lods['rock.mb']], [1, 1, None, 1])

    def test_lower_lod_takes_the_nearest_geometry_zero_colour(self):
        base = {'name': 'rock', 'material': 9,
                'verts': [(0.0, 0.0, 0.0, 0.0, 0.0, 0xff0000ff), (5.0, 0.0, 0.0, 0.0, 0.0, 0xffff0000)],
                'indices': [0, 1, 0]}
        g = tie_lods.from_triangles([((0.1, 0.0, 0.0), (4.9, 0.0, 0.0), (0.0, 1.0, 0.0))], base, 'rock:1')
        self.assertEqual([v[5] for v in g['verts']], [0xff0000ff, 0xffff0000, 0xff0000ff])
        self.assertEqual((g['material'], g['indices']), (9, [0, 1, 2]))

    def test_same_ignores_order_and_sub_millimetre_noise(self):
        a = [((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)), ((2.0, 0.0, 0.0), (3.0, 0.0, 0.0), (2.0, 1.0, 0.0))]
        b = [tuple(reversed(a[1])), ((0.0, 0.0, 0.0001), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))]
        self.assertTrue(tie_lods.same(a, b))
        self.assertFalse(tie_lods.same(a, a[:1]))
        self.assertFalse(tie_lods.same(a, None))


if __name__ == '__main__':
    unittest.main()

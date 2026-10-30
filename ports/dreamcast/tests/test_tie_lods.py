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

    def test_textured_build_splits_by_material_and_keeps_lod_uvs(self):
        import struct
        tri = lambda x, mat, c=(0.5, 0.25, 1.0, 1.0): (  # noqa: E731
            ((x, 0.0, 0.0), (x + 1.0, 0.0, 0.0), (x, 1.0, 0.0)), ((0.0, 0.0), (1.0, 0.0), (0.0, 1.0)), (c, c, c), mat)
        results = [{'name': 'hut', 'geometry': [tri(0.0, 7), tri(2.0, 3), tri(4.0, 7)]}, {'name': 'none'}]
        origin = [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]
        instances = [{'proto': 0, 'origin': origin, 'x': 1.0, 'y': 0.0, 'z': 0.0},
                     {'proto': 1, 'origin': origin, 'x': 5.0, 'y': 0.0, 'z': 0.0}]
        lod1 = ([((2.1, 0.0, 0.0), (3.0, 0.0, 0.0), (2.0, 0.9, 0.0))], [((0.5, 0.5), (0.75, 0.5), (0.5, 0.75))])
        lods = {'hut': [([t[0] for t in results[0]['geometry']], None), lod1, None, None]}
        blob, summary = tie_lods.build_textured(results, instances, lods, {'hut': (10.0, 20.0, 30.0)},
                                                {7: (2.0, 2.0, 2.0, 2.0)})
        self.assertEqual((summary['geometries'], summary['instances'], summary['materials']), (3, 1, [3, 7]))
        geoms, table, vstart, _, inst = tie_lods.read_dci2(blob)
        self.assertEqual([g[4] for g in geoms], [3, 7, 3])    # LOD 0: materials 3 and 7; LOD 1: nearest is 3
        self.assertEqual(table[0][:4], (0, 2, 0xffff, 0xffff))
        self.assertEqual(table[0][7:], (1, 0, 0, 0))          # LOD 0 runs over two geometries
        # material 7's colour is doubled and clamped like the terrain's; material 3 has factor 1
        colour = lambda g: struct.unpack_from('<5fI', blob, vstart + 24 * geoms[g][2])[5]  # noqa: E731
        self.assertEqual(colour(1), 0xffff80ff)  # (0.5, 0.25, 1) x 2, clamped
        self.assertEqual(colour(0), 0xff8040ff)
        # LOD 1 keeps its own UVs
        self.assertEqual(struct.unpack_from('<5fI', blob, vstart + 24 * geoms[2][2])[3:5], (0.5, 0.5))
        # flattened, the near instance draws both LOD 0 materials
        flat = tie_lods.flatten(blob, 1.0, 0.0)
        self.assertEqual(struct.unpack_from('<4s2I', flat, 0), (b'DCI1', 3, 2))


if __name__ == '__main__':
    unittest.main()

"""tfrag_glb.py: tfrag roots kept, the rest dropped, and the XZ crop (AI-assisted), ISC.

Synthetic GLBs only, built with test_render_sectors.GlbBuilder.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_render_sectors as rs  # noqa: E402
import tfrag_glb  # noqa: E402
from test_render_sectors import GlbBuilder  # noqa: E402


def level():
    """Roots tfrag-normal (two triangles 100 m apart), tfrag-lowres (one), tfrag-trans
    and tie-0 (one each)."""
    b = GlbBuilder()
    mat = b.add_solid_material((255, 255, 255, 255))

    def tri_mesh(xs):
        pos, idx = [], []
        for x in xs:
            base = len(pos)
            pos += [(x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 0.0, 1.0)]
            idx += [base, base + 1, base + 2]
        return b.add_mesh([dict(attributes=dict(POSITION=b.add_vec3(pos)), indices=b.add_indices(idx), material=mat)])
    roots = []
    for name, xs in (('lvl-tfrag-normal', (0.0, 100.0)), ('lvl-tfrag-trans', (0.0,)),
                     ('lvl-tfrag-lowres', (50.0,)), ('lvl-tie-0', (0.0,))):
        child = b.add_node(mesh=tri_mesh(xs))
        root = b.add_node(children=[child])
        b.nodes[root]['name'] = name
        roots.append(root)
    b.scenes[0]['nodes'] = roots
    return b.to_bytes()


def triangles(blob):
    doc, binary = rs.parse_glb_bytes(blob)
    scene = doc['scenes'][doc.get('scene', 0)]
    names = [doc['nodes'][i]['name'] for i in scene['nodes']]
    count = 0
    stack = list(scene['nodes'])
    while stack:
        n = doc['nodes'][stack.pop()]
        stack += n.get('children', [])
        if 'mesh' in n:
            count += sum(doc['accessors'][p['indices']]['count'] // 3 for p in doc['meshes'][n['mesh']]['primitives'])
    return names, count


class TfragGlbTest(unittest.TestCase):
    def test_keeps_the_opaque_tfrag_roots(self):
        blob, report = tfrag_glb.cut(level())
        self.assertEqual(report, {'lvl-tfrag-normal': True, 'lvl-tfrag-trans': False,
                                  'lvl-tfrag-lowres': True, 'lvl-tie-0': False})
        self.assertEqual(triangles(blob), (['lvl-tfrag-normal', 'lvl-tfrag-lowres'], 3))

    def test_missing_root_is_refused(self):
        with self.assertRaises(ValueError):
            tfrag_glb.cut(level(), ('normal', 'dirt'))

    def test_crop_keeps_overlapping_triangles_and_parses(self):
        blob, _ = tfrag_glb.cut(level(), rect=(-5.0, 0.5, -5.0, 5.0))
        self.assertEqual(triangles(blob), (['lvl-tfrag-normal', 'lvl-tfrag-lowres'], 1))
        # a crop touching only the lowres triangle's far corner still keeps it
        blob, _ = tfrag_glb.cut(level(), rect=(51.0, 60.0, 0.5, 2.0))
        self.assertEqual(triangles(blob)[1], 1)
        # the cropped GLB is still a valid input for the sector builder
        doc, binary = rs.parse_glb_bytes(blob)
        self.assertGreater(rs.build_package(doc, binary).summary['source_count'], 0)

    def test_probes_add_one_triangle_a_material_as_the_last_sources(self):
        import struct
        import tie_lods
        geoms = [{'name': n, 'material': m, 'verts': [(0.0, 0.0, 0.0, 0.0, 0.0, 0), (1.0, 0.0, 0.0, 0.0, 0.0, 0),
                                                       (0.0, 1.0, 0.0, 0.0, 0.0, 0)], 'indices': [0, 1, 2]}
                 for n, m in (('a', 0), ('b', 0))]
        package = tie_lods.pack(geoms, [((0, 0xffff, 0xffff, 0xffff), (1e18,) * 3, (1, 0, 0, 0))], [])
        blob, report = tfrag_glb.cut(level(), probe_package=package)
        self.assertEqual(report['material-probes'], [0])
        doc, binary = rs.parse_glb_bytes(blob)
        self.assertEqual(doc['nodes'][doc['scenes'][0]['nodes'][-1]]['name'], 'material-probes')
        summary = rs.build_package(doc, binary).summary
        self.assertEqual(summary['source_count'], 3 + 1)
        self.assertEqual(struct.unpack_from('<I', package, 8)[0], 2)


if __name__ == '__main__':
    unittest.main()

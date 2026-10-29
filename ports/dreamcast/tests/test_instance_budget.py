"""instance_budget.py: draw-distance and LOD budgets (AI-assisted), ISC.

Synthetic packages only, built by prototype_geometry.package.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import instance_budget as ib  # noqa: E402
import make_instance_fixture as mf  # noqa: E402
import prototype_geometry as pg  # noqa: E402

ORIGIN = [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]


def tris(n):
    return [([(0.0, 0.0, 0.0), (1.0, 0.0, float(k)), (0.0, 1.0, 0.0)], [(0.0, 0.0)] * 3, [(1, 2, 3, 255)] * 3, 0)
            for k in range(n)]


def package(placements):
    """'small' has 1 triangle, 'big' 4; placements are (proto, x, z) in meters."""
    results = [{'name': 'small', 'geometry': tris(1)}, {'name': 'big', 'geometry': tris(4)}]
    instances = [{'proto': p, 'origin': ORIGIN, 'x': x, 'y': 0.0, 'z': z} for p, x, z in placements]
    blob, _ = pg.package(results, instances)
    return blob


class InstanceBudgetTest(unittest.TestCase):
    def test_boundary_distance_is_inclusive_like_the_pass(self):
        protos, inst = mf.read(package([(0, 3.0, 4.0), (1, 6.0, 8.0)]))
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 5.0), (1, 1))
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 10.0), (2, 5))
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 4.999), (0, 0))

    def test_matches_the_fixture_expectation(self):
        blob = package([(i % 2, 7.3 * i - 40, 3.1 * i * i % 50 - 20) for i in range(40)])
        protos, inst = mf.read(blob)
        target = mf.camera(protos, inst)
        for d in (5.0, 17.5, 30.0):
            self.assertEqual(ib.drawn(protos, inst, target[0], target[2], d),
                             mf.expected(protos, inst, target, d))

    def test_worst_cell_median_and_p90(self):
        # a crowd of 'big' around (40, 40), singles of 'small' elsewhere
        crowd = [(1, 40.0 + i, 40.0) for i in range(5)]
        singles = [(0, x, 0.0) for x in (-100.0, -60.0, 100.0)]
        out = ib.report(package(crowd + singles), cell=16.0, distances=(10.0,))  # singles sit 8.9 m off centre
        row = out['lod0'][0]
        self.assertEqual(out['cells'], 4)
        self.assertEqual((row['worst_tris'], row['worst_instances']), (20, 5))
        self.assertEqual(row['worst_cell_centre_m'], [40.0, 40.0])
        self.assertEqual(row['median_tris'], 1)
        self.assertEqual(out['lod'], 'skipped: no --dgo')
        self.assertEqual(ib.percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 0.9), 9)

    def test_lod_estimate_scales_by_the_bsp_ratio(self):
        # 'big' halves at LOD 1, has no LOD 2 (keeps LOD 1), quarters at LOD 3; 'small' is unknown
        blob = package([(1, 10.0, 0.0), (1, 20.0, 0.0), (1, 30.0, 0.0), (1, 50.0, 0.0), (0, 1.0, 0.0)])
        table = {'big': [400, 200, 0, 100]}
        out = ib.report(blob, cell=1000.0, distances=(60.0,), switches=(15.0, 25.0, 40.0), lod_table=table)
        self.assertEqual(out['lod_unmatched_prototypes'], 1)
        # centre (500, 500) of the single 1 km cell sees nothing within 60 m
        self.assertEqual(out['lod'][0]['worst_tris'], 0)
        protos, inst = mf.read(blob)
        scales, unmatched = ib.lod_scales(['small', 'big'], table)
        self.assertEqual(unmatched, ['small'])
        self.assertEqual(scales[1], [1.0, 0.5, 0.5, 0.25])
        # from the origin: LOD 0 (4), LOD 1 (2), LOD 2 -> 1's ratio (2), LOD 3 (1), small LOD 0 (1)
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 60.0, (15.0, 25.0, 40.0), scales), (5, 10))
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 60.0), (5, 17))
        # per-prototype planes: 'big' switches at 35/45/55 (10, 20, 30 m draw LOD 0, 50 m LOD 2); 'small' has none
        per = [(), (35.0, 45.0, 55.0)]
        self.assertEqual(ib.drawn(protos, inst, 0.0, 0.0, 60.0, per, scales), (5, 4 + 4 + 4 + 2 + 1))
        out = ib.report(blob, cell=1000.0, distances=(60.0,), lod_table=table, planes={'big': (35.0, 45.0, 55.0)})
        self.assertEqual(out['lod_planes_m'], [(35.0, 45.0, 55.0)])


if __name__ == '__main__':
    unittest.main()

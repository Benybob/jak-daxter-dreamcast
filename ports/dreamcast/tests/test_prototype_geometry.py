"""prototype_geometry.py: the instance inverse and the cut matching (AI-assisted), ISC.

Synthetic data only.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prototype_geometry as pg  # noqa: E402


class PrototypeGeometryTest(unittest.TestCase):
    def test_inverse_round_trip(self):
        # A quarter turn about y, scaled 2, moved 3 m along x (game units).
        m = [[0.0, 0.0, -2.0, 0], [0.0, 2.0, 0.0, 0], [2.0, 0.0, 0.0, 0], [3 * 4096.0, 0.0, 0.0, 1]]
        inv, t = pg.inverse_affine(m)
        local = (0.5, 0.25, -1.0)
        world = [sum(local[j] * 4096 * m[j][i] for j in range(3)) / 4096 + m[3][i] / 4096 for i in range(3)]
        back = pg.to_local(world, inv, t)
        for a, b in zip(back, local):
            self.assertAlmostEqual(a, b, places=9)

    def test_shared_matches_across_grid_lines_and_drops_strangers(self):
        def tri(x):
            return (((x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 1.0, 0.0)), None, None, 0)
        a = [tri(0.0039999), tri(5.0), tri(9.0)]       # 9.0 is a neighbour's, only in a
        b = [tri(0.0040001), tri(5.0005), tri(20.0)]   # just across a 4 mm grid line, and within 2 mm
        common = pg.shared(a, b)
        self.assertEqual([t[0][0][0] for t in common], [0.0039999, 5.0])


if __name__ == '__main__':
    unittest.main()

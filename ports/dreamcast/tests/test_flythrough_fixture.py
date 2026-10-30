"""make_flythrough_fixture.py: the camera path (AI-assisted), ISC.

Synthetic values only.
"""
from pathlib import Path
import math
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import make_flythrough_fixture as ff  # noqa: E402
import make_instance_fixture as mf  # noqa: E402


class FlythroughFixtureTest(unittest.TestCase):
    def test_path_runs_along_the_view_direction(self):
        targets = ff.path([10.0, 5.0, -3.0], 5, 8.0)
        self.assertEqual(len(targets), 5)
        self.assertEqual(targets[0], [10.0, 5.0, -3.0])
        step = [targets[1][0] - targets[0][0], targets[1][2] - targets[0][2]]
        self.assertAlmostEqual(math.hypot(*step), 2.0, places=5)
        # render_world_view.c: view z grows with -cos(pitch) sin(yaw) x + cos(pitch) cos(yaw) z,
        # so moving along the path moves the target straight ahead (away from the eye)
        cy, sy = math.cos(mf.YAW), math.sin(mf.YAW)
        ahead = -sy * step[0] + cy * step[1]
        across = cy * step[0] + sy * step[1]
        self.assertAlmostEqual(ahead, 2.0, places=5)
        self.assertAlmostEqual(across, 0.0, places=5)
        self.assertTrue(all(t[1] == 5.0 for t in targets))

    def test_single_frame_path_stays_put(self):
        self.assertEqual(ff.path([1.0, 2.0, 3.0], 1, 50.0), [[1.0, 2.0, 3.0]])


if __name__ == '__main__':
    unittest.main()

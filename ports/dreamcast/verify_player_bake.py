"""Independent analytic checks for the offline skin math (AI-assisted)."""
import json
import math
import numpy as np
from bake_player import decompose, quaternion_matrix, sample

checks = {}
times = np.array([0., 1.])
checks['translation_midpoint'] = np.allclose(sample(times, np.array([[2., 4., 6.], [4., 8., 12.]]), .5), [3, 6, 9])
checks['before_first_key'] = np.allclose(sample(times, np.array([[2.], [4.]]), -1), [2])
checks['after_last_key'] = np.allclose(sample(times, np.array([[2.], [4.]]), 2), [4])
identity = np.array([0., 0., 0., 1.])
half_turn = np.array([0., 1., 0., 0.])
q = sample(times, np.array([identity, half_turn]), .5, True)
checks['rotation_halfway_vector'] = np.allclose(quaternion_matrix(q) @ [1, 0, 0, 1], [0, 0, -1, 1], atol=1e-6)
checks['antipodal_no_spin'] = np.allclose(quaternion_matrix(sample(times, np.array([identity, -identity]), .5, True)), np.eye(4))
# Explicit column-major matrix: +90 degrees about Y, scales (2,3,4), T=(5,6,7).
matrix = [0, 0, -2, 0, 0, 3, 0, 0, 4, 0, 0, 0, 5, 6, 7, 1]
t, q, s = decompose({'matrix': matrix})
checks['decompose_translation'] = np.allclose(t, [5, 6, 7])
checks['decompose_scale'] = np.allclose(s, [2, 3, 4])
checks['decompose_rotation_vector'] = np.allclose(quaternion_matrix(q) @ [1, 0, 0, 1], [0, 0, -1, 1], atol=1e-6)
try:
    sample(np.array([0., 0.]), np.array([[1.], [2.]]), .5)
    checks['duplicate_time_rejected'] = False
except ValueError:
    checks['duplicate_time_rejected'] = True
try:
    decompose({'scale': [0, 1, 1]})
    checks['singular_transform_rejected'] = False
except ValueError:
    checks['singular_transform_rejected'] = True
print(json.dumps(dict(ai_assisted=True, result='PASS' if all(checks.values()) else 'FAIL', checks=checks), indent=2))
raise SystemExit(0 if all(checks.values()) else 1)

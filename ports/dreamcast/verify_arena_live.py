"""Audit EVERY live camera record after verify_backend.py (AI-assisted), ISC.

No emulator launch. Host fixtures must explicitly opt into host_fixture=True;
they exercise the audit and are never evidence of original SH-4 execution.
"""
import argparse
import json
import math
from pathlib import Path
import struct

WORDS = ('version producer reason dispatches update_calls update_returns poison_batches witnessed_updates '
         'pose_changes camera prevent_mask selected_pad pad_valid pad_services valid_services '
         'read_analog read_buttons master_mode time_ratio_bits registered_slave runtime_error error_site '
         'master_process master_status master_state master_mask master_attempts master_resumed master_free '
         'slave_process slave_status slave_state slave_mask slave_attempts slave_resumed slave_free '
         'combiner_process combiner_status combiner_state combiner_mask combiner_attempts combiner_resumed combiner_free '
         'poisoned_lanes affine_reads rendered warmup init_events init_verified neutral_pairs moving_pairs '
         'turning_pairs position_changes orientation_changes pad_stage selected_services_before_update '
         'original_update shim restored').split()
UPDATES = 181
TRACE_BYTES = 216
EVIDENCE_BYTES = 380+TRACE_BYTES*UPDATES
# Expected success arithmetic is also checked by the host full seam test.
EXPECTED_CHECKS = 6984


def capture_contract(suite, dump, tests):
    """Resolve capture bytes before any backend process is launched."""
    if suite != 'arena-live':
        return dump, 17 if tests is None else tests
    expected = ['dc_arena_live_evidence', str(EVIDENCE_BYTES)]
    need(dump is None or list(dump) == expected, 'arena-live requires the full version-3 evidence dump')
    need(tests is None or tests == EXPECTED_CHECKS, 'arena-live requires its exact expected check count')
    return expected, EXPECTED_CHECKS


def need(ok, message):
    if not ok:
        raise ValueError(message)


def stage(completed):
    for boundary, value in ((16,0),(28,1),(40,2),(44,3),(64,4),(88,5),(100,6),(124,7),(136,8),(160,9),(172,10)):
        if completed < boundary:
            return value
    return 11


def affine(p):
    need(len(p) == 36 and all(math.isfinite(x) for x in p), 'nonfinite/missing affine lane')
    t, inv, rot = p[:4], p[4:20], p[20:]
    need(all(abs(x) <= 4096000000 for x in t[:3]), 'eye outside GOAL range')
    need(t[3] == inv[15] == rot[15] == 1, 'homogeneous w')
    need(all(inv[4*i+3] == rot[4*i+3] == 0 for i in range(3)), 'basis homogeneous lane')
    need(inv[12:15] == t[:3], 'inverse affine eye mismatch')
    h = math.hypot(inv[8], inv[10])
    need(h >= .001, 'pole')
    yaw, pitch = math.atan2(-inv[8], inv[10]), math.atan2(inv[9], h)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    basis = [cy, 0, sy, 0, sp*sy, cp, -sp*cy, 0, -cp*sy, sp, cp*cy, 0]
    need(all(abs(a-b) <= .0001 for a, b in zip(inv[:12], basis)), 'roll/scale/shear/reflection')
    for i in range(3):
        need(all(abs(rot[4*i+j]-inv[4*j+i]) <= .0001 for j in range(3)), 'upper rotation transpose')
        translated = -sum(inv[4*i+j]*t[j] for j in range(3))
        need(abs(rot[12+i]-translated) <= .25+abs(translated)*.000002, 'rotated negative eye')
    return [x/4096 for x in t[:3]], yaw, pitch


def delta(a, b):
    moved = any(abs(x-y) > .02 for x, y in zip(a[:3], b[:3]))
    turned = any(abs(x-y) > .00001 for x, y in zip(a[4:16], b[4:16]))
    return (2*int(moved) | 4*int(turned)) or 1


def audit(blob, *, host_fixture=False):
    need(len(WORDS) == 59 and len(blob) == EVIDENCE_BYTES, 'evidence ABI/length')
    m = dict(zip(WORDS, struct.unpack_from('<59I', blob)))
    exact = dict(version=3, producer=2, reason=0, dispatches=185, update_calls=181, update_returns=181,
                 poison_batches=181, witnessed_updates=181, poisoned_lanes=6516, affine_reads=181,
                 rendered=181, warmup=4, init_events=1, init_verified=1, neutral_pairs=50,
                 moving_pairs=71, turning_pairs=2, runtime_error=0, selected_pad=0, restored=1)
    for name, expected in exact.items():
        need(m[name] == expected, f'{name}={m[name]}, expected {expected}')
    need(m['original_update'] and m['shim'] and m['original_update'] != m['shim'], 'saved original entry/shim')
    if not host_fixture:
        for name in ('camera', 'original_update', 'shim', 'master_process', 'slave_process', 'combiner_process'):
            need(0x8c010000 <= m[name] < 0x8d000000, f'{name} outside cached stock main RAM')
    need(m['valid_services'] >= 181 and m['pad_services'] >= m['valid_services'] and not m['pad_valid'] & 128,
         'selected pad service/validity')
    ratio = struct.unpack('<f', struct.pack('<I', m['time_ratio_bits']))[0]
    need(math.isfinite(ratio) and 1 <= ratio <= 4.800001, 'display time ratio')
    need(m['read_analog'] and m['read_buttons'] and m['master_mode'], 'camera flags/mode diagnostics')
    need(m['registered_slave'] == m['slave_process'] and m['slave_free'], 'registered original free slave')
    need(all(m[x+'_resumed'] for x in ('master', 'slave', 'combiner')), 'original process resumption')
    initial = struct.unpack_from('<36f', blob, 236)
    affine(initial)
    previous = None
    path_m, yaw_steps = 0.0, []
    counts = dict(neutral_pairs=0, moving_pairs=0, turning_pairs=0, position_changes=0, orientation_changes=0, pose_changes=0)
    last_service = 0
    last_resumes = (0, 0, 0)
    for index in range(UPDATES):
        offset, seq = 380+index*TRACE_BYTES, index+1
        sequence, dispatch, input_stage, services, master, slave, combiner, observed_delta = struct.unpack_from('<8I', blob, offset)
        p = struct.unpack_from('<36f', blob, offset+32)
        drawn, = struct.unpack_from('<I', blob, offset+176)
        target_x, target_y, target_z, yaw, pitch, distance, half, draw_distance = struct.unpack_from('<8f', blob, offset+180)
        eye, converted_yaw, converted_pitch = affine(p)
        consumed_ratio, = struct.unpack_from('<f', blob, offset+212)
        need(math.isfinite(consumed_ratio) and 1 <= consumed_ratio <= 4.800001,
             f'frame {seq}: unsupported consumed display ratio')
        need(sequence == seq and dispatch == seq+4 and input_stage == stage(index) and drawn == 1,
             f'frame {seq}: missing/stale/unrendered/stage')
        need(services > last_service and all(a > b for a, b in zip((master, slave, combiner), last_resumes)),
             f'frame {seq}: stale pad/process evidence')
        last_service, last_resumes = services, (master, slave, combiner)
        values = (target_x, target_y, target_z, yaw, pitch, distance, half, draw_distance)
        need(all(math.isfinite(x) for x in values), f'frame {seq}: render values nonfinite')
        need(distance == 55 and half == 14 and draw_distance == 80, f'frame {seq}: adapter/radii')
        need(abs(math.sin(yaw)-math.sin(converted_yaw)) < .00001 and abs(pitch-converted_pitch) < .00001,
             f'frame {seq}: render orientation differs')
        expected_target = [eye[j]+p[12+j]*distance for j in range(3)]
        need(all(abs(a-b) <= .0002 for a, b in zip((target_x,target_y,target_z), expected_target)), f'frame {seq}: render anchor differs')
        d = delta(previous, p) if previous else 0
        need(d == observed_delta, f'frame {seq}: reported motion differs from raw output')
        if previous:
            path_m += math.sqrt(sum(((a-b)/4096)**2 for a,b in zip(p[:3],previous[:3])))
            counts['position_changes'] += bool(d & 2)
            counts['orientation_changes'] += bool(d & 4)
            counts['pose_changes'] += d != 1
        else:
            need(p == initial, 'initial verified pose differs from first output')
            need(any(abs(x) > 4096 for x in p[:3]), 'reset-origin combiner is not beach initialization')
        if 4 <= seq <= 14 or 32 <= seq <= 38 or 48 <= seq <= 60 or 92 <= seq <= 98 or 128 <= seq <= 134 or 176 <= seq <= 180:
            need(d == 1, f'frame {seq}: neutral instability'); counts['neutral_pairs'] += 1
        if 20 <= seq <= 26 or 68 <= seq <= 86 or 104 <= seq <= 122 or 140 <= seq <= 158 or 164 <= seq <= 170:
            need(d == 2, f'frame {seq}: original translation response absent'); counts['moving_pairs'] += 1
        if seq in (41, 42):
            need(d == 1, f'frame {seq}: turn arrived before its recorded control')
        if 43 <= seq <= 44:
            need(d == 4, f'frame {seq}: original turn response absent')
            old_yaw = math.atan2(-previous[12], previous[14])
            angle = abs(math.atan2(math.sin(converted_yaw-old_yaw), math.cos(converted_yaw-old_yaw)))
            need(abs(angle-math.radians(.6)*consumed_ratio) <= .00021,
                 f'frame {seq}: yaw magnitude differs from original input at consumed ratio')
            yaw_steps.append(math.degrees(angle))
            counts['turning_pairs'] += 1
        previous = p
    for name, value in counts.items():
        need(m[name] == value, f'{name}: summary differs from trace')
    need(4.5 <= path_m <= 20, 'original eye did not travel 4.5..20 m')
    return {'eye_path_m': path_m, 'yaw_steps_degrees': yaw_steps, 'source': 'HOST FIXTURE' if host_fixture else 'recorded pad -> original free camera',
            'updates': UPDATES, 'draws': UPDATES, 'poisoned_output_lanes': 36*UPDATES, **counts}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--result', type=Path, required=True, help='same run result.json from verify_backend.py')
    args = parser.parse_args()
    result = json.loads(args.result.read_text())
    need(result['state'] == 2 and result['total'] == result['passed'] == EXPECTED_CHECKS
         and result['system_ram'] == 16777216 and not result['failed_case'], 'backend is not exact stock-RAM success')
    print(json.dumps(audit(args.dump.read_bytes()), indent=2))

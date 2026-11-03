"""Stage beach coverage from exported production pad samples (AI-assisted), ISC.

The trajectory is a source-derived HOST PREDICTION, never a camera producer.
Only original GOAL output drives the live renderer. Keep the existing fly assets.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import indexed_sectors
import make_flythrough_fixture as fly
import make_instance_fixture as mf


def predict(samples, sign, ratio=4):
    start = [-281.1929931640625, 31.275928497314453, -280.4587707519531]
    yaw, pitch, distance = 2.2, -.45, 55
    eye = [start[0]+math.cos(pitch)*math.sin(yaw)*distance,
           start[1]-math.sin(pitch)*distance,
           start[2]-math.cos(pitch)*math.cos(yaw)*distance]
    rows, length = [], 0
    for i, s in enumerate(samples):
        assert int(s['completed']) == i
        joy, buttons = int(s['joyx']), int(s['buttons'])
        assert joy in (-52, 0, 52) and buttons in (0, 1 << 17)
        # Original analog-input deadzone 48, range 110; speed=1600.
        step = -math.copysign(2*1600*ratio*max(0, abs(joy)-48)/62/4096, joy)
        eye[0] += step*math.cos(yaw)
        eye[2] += step*math.sin(yaw)
        length += abs(step)
        if buttons:
            # Right X=207: 2*rot-speed*(207-128-48)/62 = rot-speed.
            yaw += sign*math.radians(.6)*ratio
        target = [mf.f32(eye[0]-math.cos(pitch)*math.sin(yaw)*distance), mf.f32(start[1]),
                  mf.f32(eye[2]+math.cos(pitch)*math.cos(yaw)*distance)]
        assert abs(target[0]-start[0]) <= 6 and abs(target[2]-start[2]) <= 6
        rows.append(dict(sequence=i+1, target=target, eye=eye.copy(), yaw=yaw, pitch=pitch))
    return rows, length


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('glb', type=Path)
    ap.add_argument('package', type=Path)
    ap.add_argument('recording', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--header', type=Path, default=Path('build/arena_live_fixture.h'))
    a = ap.parse_args()
    if a.out.exists():
        raise ValueError('use a fresh output directory; existing staging is preserved')
    samples = list(csv.DictReader(a.recording.open()))
    assert len(samples) == 181
    rows, length = predict(samples, 1)
    yaw_steps = [math.degrees(b['yaw']-a['yaw']) for a,b in zip(rows,rows[1:])
                 if abs(b['yaw']-a['yaw']) > 1e-8]
    assert len(yaw_steps) == 2 and all(abs(x-2.4)<1e-6 for x in yaw_steps)
    envelope = []
    for sign in (-1, 1):
        for ratio in (1, 2, 3, 4):
            route, _ = predict(samples, sign, ratio)
            envelope.extend(r['target'] for r in route)
    report, staged = fly.make(a.glb, a.package, a.out, frames=len(rows), length=length,
                              header=a.header, instance_cells=True,
                              recorded_targets=[r['target'] for r in rows], coverage_targets=envelope)
    report['draw_file_bytes'] = indexed_sectors.write_draw_files(a.out, remove_sectors=True)
    # Bound ALL windows allowed by the unchanged +/-6 m travel guard, not just
    # predicted rows. Existing live capacity cushions remain in the renderer.
    origin = report['start']
    corners = [dict(target=[origin[0]+dx,origin[1],origin[2]+dz])
               for dx in (-6,0,6) for dz in (-6,0,6)]
    assert all(abs(a-b)<.0001 for a,b in zip(report['start'], rows[0]['target'])), 'wrong level package start'
    assert report['max_instance_triangles'] > 0, 'recorded route has no instances'
    report['window'] = fly.window_caps_for(a.out, staged+corners, 14, a.header)
    report.update(recording_sha256=hashlib.sha256(a.recording.read_bytes()).hexdigest(),
                  prediction_only=True, ratio=4, travel_cap_xz=6, travel_cap_y=1,
                  predicted_eye_path_m=length, yaw_steps_degrees=yaw_steps,
                  yaw_total_degrees=sum(yaw_steps),
                  coverage_source='production recording, both yaw signs, ratios 1..4',
                  recording=samples, predicted_rows=rows)
    (a.out / 'FLY.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('recording','predicted_rows','blend_faces_by_frame')}, indent=2))


if __name__ == '__main__':
    main()

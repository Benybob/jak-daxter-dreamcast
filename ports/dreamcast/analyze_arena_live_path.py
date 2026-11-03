"""Read a partial live camera dump without claiming acceptance (AI-assisted), ISC.

Writes all versioned trace slots to CSV: missing rows are explicitly unrecorded. Reconstructs
the anchor from raw original trans/inverse, even for an update whose draw failed.
verify_arena_live.py remains the required, strict success auditor.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from verify_arena_live import WORDS, affine, need


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def decode(blob, fixture):
    need(len(blob) >= 380, 'unexpected live evidence length')
    m = dict(zip(WORDS, struct.unpack_from('<59I', blob)))
    row_bytes = 216 if m['version'] == 3 else 212
    slots, remainder = divmod(len(blob)-380, row_bytes)
    need(not remainder and m['producer'] == 2 and
         ((m['version'] == 1 and slots == 61) or
          (m['version'] in (2, 3) and slots in (61, 181))), 'unknown evidence version/producer/length')
    reported_count = m['witnessed_updates']
    need(0 < reported_count <= (61 if m['version'] == 1 else 181), 'invalid witnessed count')
    count = min(reported_count, slots)
    need(0 < count <= slots, 'no bounded witnessed prefix')
    initial, crop = fixture['start'], fixture['crop']
    rows = []
    for index in range(slots):
        offset = 380 + index * row_bytes
        seq, dispatch, stage, services, master, slave, combiner, delta = struct.unpack_from('<8I', blob, offset)
        row = dict(sequence=index+1, status='unrecorded')
        if index >= count:
            need(not any(blob[offset:offset+row_bytes]), 'nonzero unwitnessed trace row')
            rows.append(row)
            continue
        need(seq == index+1 and dispatch == seq+4, 'noncontiguous witnessed prefix')
        p = struct.unpack_from('<36f', blob, offset+32)
        eye, yaw, pitch = affine(p)
        target = [f32(eye[j] + f32(55*p[12+j])) for j in range(3)]
        drawn, = struct.unpack_from('<I', blob, offset+176)
        need(drawn in (0, 1), 'invalid drawn flag')
        if drawn:
            submitted = struct.unpack_from('<3f', blob, offset+180)
            need(all(abs(a-b) < .0002 for a, b in zip(target, submitted)), 'submitted anchor differs')
        dx, dy, dz = [target[j]-initial[j] for j in range(3)]
        inside = (target[0]-14 >= crop[0] and target[0]+14 <= crop[1]
                  and target[2]-14 >= crop[2] and target[2]+14 <= crop[3])
        travel = abs(dx) <= 6 and abs(dy) <= 1 and abs(dz) <= 6
        if m['version'] == 3:
            row['consumed_ratio'] = struct.unpack_from('<f', blob, offset+212)[0]
        row.update(status='drawn' if drawn else 'witnessed_not_drawn', dispatch=dispatch, stage=stage,
                   delta=delta, selected_services=services, master_resumed=master,
                   slave_resumed=slave, combiner_resumed=combiner,
                   **{f'eye_{axis}': eye[j] for j, axis in enumerate('xyz')},
                   **{f'anchor_{axis}': target[j] for j, axis in enumerate('xyz')},
                   yaw=yaw, pitch=pitch, dx=dx, dy=dy, dz=dz,
                   query_in_crop=inside, within_travel_cap=travel)
        rows.append(row)
    summary = dict(ai_assisted=True, isc=True, acceptance=False,
                   dump_sha256=hashlib.sha256(blob).hexdigest(),
                   witnessed=count, reported_witnessed=reported_count,
                   captured_bytes=len(blob), complete_capture=(slots == (61 if m['version'] == 1 else 181)),
                   drawn=sum(r['status'] == 'drawn' for r in rows),
                   unrecorded=slots-count,
                   uncaptured_slots=(181-slots if m['version'] in (2, 3) else 0),
                   evidence_version=m['version'], reason=m['reason'], runtime_error=m['runtime_error'],
                   error_site=m['error_site'], time_ratio=struct.unpack('<f', struct.pack('<I', m['time_ratio_bits']))[0],
                   crop=crop, initial_anchor=initial,
                   yaw_measurement=[dict(sequence=r['sequence'], yaw=r['yaw'],
                                         delta=r['yaw']-rows[i-1]['yaw'])
                                    for i,r in enumerate(rows[:count]) if 41 <= r['sequence'] <= 45],
                   first_coverage_failure=next((r for r in rows[:count]
                                                if not r['query_in_crop'] or not r['within_travel_cap']), None))
    return rows, summary


def budget_bounds(assets, header, image, boot):
    """Conservative union over EVERY anchor allowed by the unchanged XZ cap.

    A half-20 query includes every half-14 query centred within +/-6 m.
    A circle expanded by sqrt(6**2+6**2) includes every instance cell such an
    anchor can request. These overcounts are independent of a predicted path.
    They bound capacities, not slab allocator/PVR high-water or image identity.
    """
    import instance_cells
    import window_caps

    fixture = json.loads((assets / 'FLY.json').read_text())
    x, _, z = fixture['start']
    package = window_caps.load_package(assets)
    needs = window_caps.window_needs(package, x, z, 20)
    # Vertex ownership depends on the FIRST touched replica of each source
    # face. A larger window alone need not upper-bound deduplicated vertices.
    # Enumerate every block-membership partition (including closed edges),
    # keeping all faces touching the full half-20 envelope in each partition.
    def centres(axis, origin):
        lo, hi = origin-6, origin+6
        edges = {lo, hi}
        for block in package.locals:
            for bound in block['bounds'][axis:axis+2]:
                edges.update(b for b in (bound-14, bound+14) if lo <= b <= hi)
        edges = sorted(edges)
        return edges + [(a+b)/2 for a, b in zip(edges, edges[1:])]

    memberships = set()
    for tx in centres(0, x):
        for tz in centres(2, z):
            blocks = list(package.touched(tx-14, tx+14, tz-14, tz+14))
            key = tuple(id(b) for b in blocks)
            if key in memberships:
                continue
            memberships.add(key)
            seen, vertices = set(), 0
            for block in blocks:
                used = set()
                for source, a, b, c, x0, x1, z0, z1 in block['faces']:
                    if source in seen:
                        continue
                    seen.add(source)
                    if x0 <= x+20 and x1 >= x-20 and z0 <= z+20 and z1 >= z-20:
                        used.update((a, b, c))
                vertices += len(used)
            needs['vertices'] = max(needs['vertices'], vertices)
    caps = {k: int(v) for k, v in re.findall(r'^#define (FLY_\w+) (\d+)u$', header.read_text(), re.M)}
    limits = dict(faces=caps['FLY_WINDOW_FACES']+2048, vertices=caps['FLY_WINDOW_VERTICES']+2048,
                  remap=caps['FLY_WINDOW_REMAP']+4096, budget=caps['FLY_CACHE_BUDGET']+262144)
    for k in needs:
        need(needs[k] <= limits[k], f'cap-square union exceeds {k} capacity')
    records = 0
    for path in (assets / 'cells').glob('*.dci'):
        ix, iz = map(int, path.stem.split('_'))
        size = path.stat().st_size
        need(size % 52 == 0, 'not a baseline instance cell')
        if instance_cells.touches(ix, iz, x, z, 80+math.sqrt(72)):
            records += size//52
    need(records <= caps['FLY_INSTANCE_RECORDS']+128, 'expanded instance union exceeds capacity')
    image_bytes, boot_bytes = image.stat().st_size, boot.stat().st_size
    start = 0x8cb00000 + (boot_bytes+31)//32*32
    end = start + (image_bytes+31)//32*32
    need(0 < image_bytes <= 1700000 and end <= 0x8d000000, 'existing staged images exceed Make limits')
    return dict(terrain_union=needs, block_memberships=len(memberships), live_limits=limits, instance_upper_bound=records,
                instance_limit=caps['FLY_INSTANCE_RECORDS']+128,
                existing_image_bytes=image_bytes, image_ceiling=1700000,
                existing_image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
                boot_bytes=boot_bytes, staged_start=hex(start), staged_end=hex(end),
                unverified='Image inspected; runtime slab/VRAM/PVR and final linked placement unmeasured')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='output prefix for .csv and .json')
    parser.add_argument('--header', type=Path, help='also bound capacities; requires --image and --boot')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--boot', type=Path)
    args = parser.parse_args()
    rows, summary = decode(args.dump.read_bytes(), json.loads((args.assets / 'FLY.json').read_text()))
    if args.header:
        need(args.image is not None and args.boot is not None, '--header requires --image and --boot')
        summary['budget_bounds'] = budget_bounds(args.assets, args.header, args.image, args.boot)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.with_suffix('.csv').open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    args.output.with_suffix('.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()

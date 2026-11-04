"""Measure fixture CPU residency with the real slab/cache (AI-assisted), ISC.

Host CPU allocation replay only. No ELF, emulator or build_dreamcast.py.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

PORT = Path(__file__).resolve().parent


def measure(root, fixture, output, margin, evidence=None):
    env = os.environ.copy()
    env['PATH'] += os.pathsep + 'C:/msys64/mingw64/bin'
    cc = shutil.which('gcc', path=env['PATH'])
    if not cc:
        raise RuntimeError('host gcc required')
    output.mkdir(parents=True, exist_ok=True)
    defines = dict(re.findall(r'#define (FLY_\w+) (\d+)u', fixture.read_text()))
    n = lambda k: int(defines['FLY_' + k])
    # Identical allocation capacities to arena_live_render.c, including its
    # deliberate live-path reserves. Never infer fit from the old game path.
    sizes = [n('WINDOW_FACES')+2048, n('WINDOW_VERTICES')+2048,
             n('WINDOW_REMAP')+4096, n('CACHE_BUDGET')+262144,
             n('INSTANCE_RECORDS')+128, n('PACKAGE_BYTES')]
    # Targets/query half are from the actual compiled fixture, not rounded JSON.
    rows = re.findall(r'^\{\{([^}]+)\},\{([^}]+)\}', fixture.read_text(), re.M)
    path = output / 'queries.txt'
    queries = []
    for target, query in rows:
        t = [float(v.rstrip('f')) for v in target.split(',')]
        q = [float(v) for v in query.split(',')]
        queries.append((t[0], t[2], (q[2]-q[0])/2))
    if len(queries) != n('FRAMES'):
        raise ValueError('fixture rows')
    predicted_queries = queries
    evidence_sha = None
    if evidence:
        from verify_arena_live import audit, WORDS
        from analyze_arena_live_path import decode
        import struct
        blob = evidence.read_bytes()
        # Native GOAL uses cached P0 addresses (goal_runtime_dc.c). The older
        # live CLI accepts only P1. Validate the real addresses here explicitly,
        # then run all of its semantic/store/input checks without its P1 gate.
        words = dict(zip(WORDS, struct.unpack_from('<59I', blob)))
        for key in ('camera', 'original_update', 'shim', 'master_process', 'slave_process', 'combiner_process'):
            if not 0x0c010000 <= (words[key] & 0x1fffffff) < 0x0d000000:
                raise ValueError('evidence address outside stock main RAM: ' + key)
        audit(blob, host_fixture=True)
        doc = json.loads((root/'FLY.json').read_text())
        actual, _ = decode(blob, doc)
        queries = [(r['anchor_x'], r['anchor_z'], predicted_queries[0][2]) for r in actual]
        evidence_sha = hashlib.sha256(blob).hexdigest()
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in queries))
    flags = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
             '-fdata-sections', '-I', str(PORT)]
    sources = ['tests/arena_jak_slab_probe.c', 'arena_slab.c', 'render_block_cache.c',
               'render_draw_window.c', 'render_indexed.c', 'render_cache.c',
               'render_package.c', 'sector_inflate_stored.c', 'instance_cells.c', 'instance_pass.c']
    objects = []
    for index, source in enumerate(sources):
        obj = output / (Path(source).stem + '.o')
        redirect = [] if index < 2 else ['-DDC_ARENA_SLAB_REDIRECT', '-include', str(PORT/'arena_slab.h')]
        subprocess.run([cc, *flags, *redirect, '-c', str(PORT/source), '-o', str(obj)], env=env, check=True)
        objects.append(str(obj))
    exe = output / 'slab-probe.exe'
    subprocess.run([cc, *objects, '-Wl,--gc-sections', '-lz', '-lm', '-o', str(exe)], env=env, check=True)
    def replay(cap):
        return int(subprocess.check_output([str(exe), str(root.resolve()), str(cap),
                   *map(str, sizes), str(path.resolve())], env=env, text=True).strip())
    high = replay(2621440)
    # Current fixture prediction and the supplied completed original-camera
    # recording are separate inputs. Reserve for whichever CPU trace is larger.
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in predicted_queries))
    predicted_high = replay(2621440)
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in queries))
    recorded_high = high
    # A busier valid camera route traverses the entire existing +/-6 m bound.
    # This is a host stress path, not an original or on-target measurement.
    cx, cz, half = predicted_queries[0]
    stress = [(cx+dx, cz+dz, half) for dz in range(-6, 7)
              for dx in (range(-6, 7) if dz % 2 else range(6, -7, -1))]
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in stress))
    bounded_high = replay(2621440)
    # Also exercise a busier route over this same staged beach package. It is
    # beyond the live observer's +/-6 m acceptance domain, so it must not be
    # described as an original player/camera recording. All 49 queries stay
    # inside the staged crop and use the unchanged renderer buffer capacities.
    broad = [(cx+dx, cz+dz, half) for dz in range(-24,25,8) for dx in range(-24,25,8)]
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in broad))
    stress_high = replay(2621440)
    high = max(recorded_high, predicted_high, bounded_high, stress_high)
    slab = (high + margin + 31) & ~31
    replay(slab)
    path.write_text(''.join('%.17g %.17g %.17g\n' % q for q in queries))
    # Exercise fragmentation at the proposed capacity, not just sum live bytes.
    replay(slab)
    report = dict(ai_assisted=True, license='ISC', host_replay=True, frames=len(rows),
                  recorded_game_high_water=1255392, measured_host_high_water=high,
                  margin=margin, slab_bytes=slab, goal_bytes=13*1024*1024-slab,
                  returned_bytes=2621440-slab, capacities=sizes,
                  evidence_sha256=evidence_sha, predicted_high_water=predicted_high,
                  recorded_host_high_water=recorded_high, bounded_host_high_water=bounded_high,
                  busier_host_high_water=stress_high, busier_excess=max(0, stress_high-slab),
                  fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
                  files={p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(root.rglob('*')) if p.is_file() and p.suffix in ('.drm','.drx','.dci')})
    (output/'budget.json').write_text(json.dumps(report, indent=2)+'\n')
    (output/'budget.h').write_text(
        '/* Fixture CPU replay + explicit margin (AI-assisted), ISC. */\n'
        f'#define DC_JAK_RENDER_MEASURED {high}u\n'
        f'#define DC_JAK_RENDER_MARGIN {margin}u\n'
        f'#define DC_JAK_SLAB_BYTES {slab}u\n'
        f'#define DC_JAK_GOAL_BYTES {13*1024*1024-slab}u\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'files'}, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, default=PORT/'build/arena-live-disc/live')
    p.add_argument('--fixture', type=Path, default=PORT/'build/arena_live_fixture.h')
    p.add_argument('--output', type=Path, default=PORT/'build/arena-jak-budget')
    p.add_argument('--margin', type=int, default=65536)
    p.add_argument('--evidence', type=Path, help='completed live v3 dump (strict audit required)')
    a = p.parse_args()
    if a.margin < 32768 or a.margin > 262144:
        p.error('margin must be 32768..262144 bytes')
    measure(a.assets, a.fixture, a.output, a.margin, a.evidence)

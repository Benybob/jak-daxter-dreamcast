"""Stage a level's render data around a point for the level ELF (AI-assisted), ISC.

    python make_level_render_assets.py BACKGROUND.glb PACKAGE.dci3 OUT_DIR --at X,Y,Z [--radius 100]

The same terrain sectors, instance cells and fixture header as
make_flythrough_fixture.py, but for a renderer that follows the game camera
instead of a recorded path: the staged terrain covers a square RADIUS meters
around AT (Geyser Rock's training-start for the level ELF), and the per-frame
window caps are measured on a ring of camera targets around it. Writes
OUT_DIR (git-ignored, derived from the owner's files) and --header.
"""
import argparse
import json
import math
from pathlib import Path

import indexed_sectors
import make_flythrough_fixture as fly


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('glb', type=Path)
    ap.add_argument('package', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--at', required=True, help='X,Y,Z in meters')
    ap.add_argument('--radius', type=float, default=100.0)
    ap.add_argument('--half', type=float, default=14.0)
    ap.add_argument('--frames', type=int, default=16)
    ap.add_argument('--header', default='build/level_render_fixture.h')
    a = ap.parse_args()
    x, y, z = (float(v) for v in a.at.split(','))
    r = a.radius
    # Window rows on a ring around AT; coverage is the whole square.
    ring = [[x + 0.5 * r * math.cos(2 * math.pi * i / a.frames), y, z + 0.5 * r * math.sin(2 * math.pi * i / a.frames)]
            for i in range(a.frames)]
    corners = [[x - r, y, z - r], [x + r, y, z + r]]
    report, rows = fly.make(a.glb, a.package, a.out, a.frames, 0.0, a.half, a.header, instance_cells=True,
                            recorded_targets=ring, coverage_targets=corners)
    report['draw_file_bytes'] = indexed_sectors.write_draw_files(a.out, remove_sectors=True)
    import instance_cells  # every cell in one resident blob: no cell opens while walking
    (a.out / 'cells.pak').write_bytes(instance_cells.pack(a.out))
    report['window'] = fly.window_caps_for(a.out, rows, a.half, a.header)
    report.update(at=[x, y, z], radius=r, ai_assisted=True)
    (a.out / 'FLY.json').write_text(json.dumps(report, indent=1), newline='\n')
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, list)}, indent=1))


if __name__ == '__main__':
    main()

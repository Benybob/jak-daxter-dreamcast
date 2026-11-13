"""Stage a level's render data around a point for the level ELF (AI-assisted), ISC.

    python make_level_render_assets.py BACKGROUND.glb PACKAGE.dci3 OUT_DIR --at X,Y,Z [--radius 100] [--area X0,Z0,X1,Z1]

The same terrain sectors, instance cells and fixture header as
make_flythrough_fixture.py, but for a renderer that follows the game camera
instead of a recorded path: the staged terrain covers a square RADIUS meters
around AT (Geyser Rock's training-start for the level ELF), and the per-frame
window caps are measured on a ring of camera targets around it. Writes
OUT_DIR (git-ignored, derived from the owner's files) and --header.

--area stages that rectangle instead of the square (Geyser Rock's actors span
230 by 350 m and its lake is 145 m from the start: a 100 m square held half of
it). The caps are then the maxima over an 8 m grid of the rectangle (the window
WINDOW_MARGIN wider than --half, as level_draw.c builds it) with the instance
records of the same grid. The manifest's own bounds are no guide: far scenery
and a sentinel stretch them to 100 km.
"""
import argparse
import json
import math
from pathlib import Path

import indexed_sectors
import make_flythrough_fixture as fly


def instance_cells_peak(blob, grid, draw):
    """The most instance records any grid centre holds at once."""
    import instance_cells
    import instance_palette
    return instance_cells.peak(instance_palette.baseline(blob), grid, draw)


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
    ap.add_argument('--area', help='X0,Z0,X1,Z1 in meters: stage this rectangle; caps from a sweep of it')
    ap.add_argument('--window-margin', type=float, default=4.0, help="level_draw.c's WINDOW_MARGIN_M (with --area)")
    a = ap.parse_args()
    x, y, z = (float(v) for v in a.at.split(','))
    r = a.radius
    # Window rows on a ring around AT; coverage is the whole square.
    ring = [[x + 0.5 * r * math.cos(2 * math.pi * i / a.frames), y, z + 0.5 * r * math.sin(2 * math.pi * i / a.frames)]
            for i in range(a.frames)]
    corners = [[x - r, y, z - r], [x + r, y, z + r]]
    if a.area:
        ax0, az0, ax1, az1 = (float(v) for v in a.area.split(','))
        assert ax0 < ax1 and az0 < az1
        corners = [[ax0, y, az0], [ax1, y, az1]]
    report, rows = fly.make(a.glb, a.package, a.out, a.frames, 0.0, a.half, a.header, instance_cells=True,
                            recorded_targets=ring, coverage_targets=corners)
    report['draw_file_bytes'] = indexed_sectors.write_draw_files(a.out, remove_sectors=True)
    import instance_cells  # every cell in one resident blob: no cell opens while walking
    (a.out / 'cells.pak').write_bytes(instance_cells.pack(a.out))
    if a.area:
        import re
        import make_instance_fixture as mf
        import window_caps
        loaded = window_caps.load_package(a.out)
        grid = [(ax0 + 8 * i, az0 + 8 * k)
                for i in range(int((ax1 - ax0) // 8) + 1) for k in range(int((az1 - az0) // 8) + 1)]
        most = dict.fromkeys(window_caps.METRICS, 0)
        for gx, gz in grid:
            needs = window_caps.window_needs(loaded, gx, gz, a.half + a.window_margin)
            most = {k: max(most[k], needs[k]) for k in most}
        records = instance_cells_peak(a.package.read_bytes(), grid, mf.DRAW_DISTANCE)
        text = Path(a.header).read_text()
        text, n = re.subn(r'#define FLY_INSTANCE_RECORDS \d+u', f'#define FLY_INSTANCE_RECORDS {records}u', text)
        assert n == 1
        names = dict(faces='FLY_WINDOW_FACES', vertices='FLY_WINDOW_VERTICES', remap='FLY_WINDOW_REMAP',
                     budget='FLY_CACHE_BUDGET')
        text += '#define FLY_SWEPT 1u'+chr(10)
        text += ''.join(f'#ifndef {name}\n#define {name} {most[key]}u\n#endif\n' for key, name in names.items())
        Path(a.header).write_text(text, newline='\n')
        report['window'] = most
        report['instance_records'] = records
        report['area'] = [ax0, az0, ax1, az1]
    else:
        report['window'] = fly.window_caps_for(a.out, rows, a.half, a.header)
    report.update(at=[x, y, z], radius=r, ai_assisted=True)
    (a.out / 'FLY.json').write_text(json.dumps(report, indent=1), newline='\n')
    print(json.dumps({k: v for k, v in report.items() if not isinstance(v, list)}, indent=1))


if __name__ == '__main__':
    main()

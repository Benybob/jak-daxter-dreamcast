"""Measure what a retail level DGO needs in Dreamcast RAM (AI-assisted), ISC.

Reads the owner's extracted level DGO (for example TRA.DGO) and prints, per
object, its kind and size, then a byte attribution of the level's BSP by GOAL
type. Nothing is written; no retail bytes leave the input file.

The BSP attribution uses the v4 link table: every basic has a type binding at
its tag, so sorting the tags gives each basic's start, and the bytes up to the
next tag are attributed to it (inline arrays and structures that follow a
basic count toward that basic). It is an estimate of extent, not a layout.
"""
import argparse
import collections
from pathlib import Path

from stage_jak_art import archive_objects, decode_v4, u32

# Types whose data only the PS2 renderers read. A Dreamcast level keeps game
# logic resident and draws from converted packages instead.
RENDER_TYPES = {
    'instance-tie', 'tfragment', 'tie-fragment', 'time-of-day-palette',
    'instance-shrubbery', 'generic-shrub-fragment', 'prototype-bucket-tie',
    'shrubbery', 'prototype-tie', 'prototype-bucket-shrub', 'adgif-shader-array',
}


def kind(name, obj):
    version = u32(obj, 8) if len(obj) > 12 else 0
    if version == 3:
        return 'code'
    if name.startswith('tpage-') or name.endswith('-tx'):
        return 'texture-page'
    if name.endswith('-vis') or name.endswith('-bsp'):
        return 'bsp'
    return 'data'


def bsp_types(obj):
    data, _, bindings = decode_v4(obj, 'bsp-header')
    starts = sorted((at, name) for name, methods, sites in bindings if methods is not None for at in sites)
    size, count = collections.Counter(), collections.Counter()
    for i, (at, name) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(data)
        size[name] += end - at
        count[name] += 1
    return len(data), size, count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dgo', type=Path, help="owner's extracted level DGO, e.g. TRA.DGO")
    parser.add_argument('--types', type=int, default=25, help='BSP types to list')
    args = parser.parse_args()
    totals = collections.Counter()
    bsp = None
    for _, name, obj in archive_objects(args.dgo):
        k = kind(name, obj)
        totals[k] += len(obj)
        print(f'{k:13s} {len(obj):9d}  {name}')
        if k == 'bsp':
            bsp = (name, obj)
    print()
    for k, n in totals.most_common():
        print(f'{k:13s} {n:9d}')
    if not bsp:
        return
    resident, size, count = bsp_types(bsp[1])
    render = sum(n for t, n in size.items() if t in RENDER_TYPES)
    print(f'\n{bsp[0]}: {resident} resident bytes, {len(size)} types')
    print(f'render-only types {render} ({100 * render / resident:.1f}%), other {resident - render}')
    for t, n in size.most_common(args.types):
        tag = 'render' if t in RENDER_TYPES else ''
        print(f'{t:40s} {count[t]:6d} {n:9d} {100 * n / resident:5.1f}% {tag}')


if __name__ == '__main__':
    main()

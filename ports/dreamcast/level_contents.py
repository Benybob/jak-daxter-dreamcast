"""What each Jak 1 level DGO holds, by kind (AI-assisted), ISC.

Reads level DGOs from the user's own disc (nothing is committed) and splits each
one's objects into four kinds by their link header and name:

- code: v3 objects (compiled GOAL), linked into the level heap;
- textures: v4 `tpage-*` objects, the level's texture pages;
- level: the v4 `*-vis` object, the level's BSP: background geometry (tfrag,
  tie, shrub), collision and entities;
- art: the other v4 objects, art groups: skinned models, their joints and their
  compressed animations.

On the PS2 a level's objects load into a 10,416 KiB level heap, two levels at a
time; the Dreamcast has 16 MiB of main RAM in all. These are file sizes, not
resident sizes: linking drops headers and link tables, and nothing here is
decompressed or converted.

  python level_contents.py DIR [--markdown | --bsp | --art]

--bsp splits each level's BSP object by basic type, from the type links in its
v4 link table (each basic owns the bytes up to the next one), grouped into
categories. --art does the same for art groups (models, animations, joints), and
also reads .CGO archives, where the disc keeps Jak's own art.
"""
import argparse
from pathlib import Path
import struct
import sys

KINDS = ('code', 'textures', 'level', 'art')


def objects(path):
    """(name, size, kind) for each object in a DGO."""
    data = path.read_bytes()
    count, = struct.unpack_from('<I', data, 0)
    offset, out = 64, []
    for _ in range(count):
        size, = struct.unpack_from('<I', data, offset)
        name = data[offset + 4:offset + 64].split(b'\0')[0].decode('ascii')
        version, = struct.unpack_from('<I', data, offset + 64 + 8)
        if version == 3:
            kind = 'code'
        elif version != 4:
            raise ValueError(f'{path.name}: {name} has link version {version}')
        elif name.startswith('tpage-'):
            kind = 'textures'
        elif name.endswith('-vis'):
            kind = 'level'
        else:
            kind = 'art'
        out.append((name, size, kind))
        offset += 64 + size
    if offset != len(data):
        raise ValueError(f'{path.name}: {len(data) - offset} bytes after the last object')
    return out


def object_bytes(path, wanted):
    """The bytes of the object named `wanted` (the last one of that name)."""
    data = path.read_bytes()
    count, = struct.unpack_from('<I', data, 0)
    offset, found = 64, None
    for _ in range(count):
        size, = struct.unpack_from('<I', data, offset)
        if data[offset + 4:offset + 64].split(b'\0')[0].decode('ascii') == wanted:
            found = data[offset + 64:offset + 64 + size]
        offset += 64 + size
    if found is None:
        raise ValueError(f'{path.name}: no object {wanted}')
    return found


def basics(obj):
    """(offset, type name) of each basic in a v4 object's data, from the type links
    of its link table (as the decompiler's link_v2_or_v4 and c_symlink2 read them)."""
    _, length, version, code_size = struct.unpack_from('<4I', obj, 0)
    if version != 4:
        raise ValueError('not a v4 object')
    code, link = 16, 16 + code_size
    p = link + 12
    if obj[p] == 0:
        p += 1
    else:  # the pointer table: alternating seek and fix counts, runs of 255 continue
        while True:
            while True:
                count = obj[p]
                p += 1
                if count != 0xff:
                    break
                if obj[p] == 0:
                    p += 1
            if obj[p] == 0:
                break
        p += 1
    out = []
    while obj[p] != 0:
        reloc = obj[p]
        p += 1
        is_type = bool(reloc & 0x80)
        if not is_type and reloc > 9:
            p -= 1
        end = obj.index(b'\0', p)
        name = obj[p:end].decode('ascii')
        p = end + 1
        at = code
        while True:
            seek = obj[p]
            step = 1
            if seek & 3:
                seek |= obj[p + 1] << 8
                step = 2
                if seek & 2:
                    seek |= obj[p + 2] << 16
                    step = 3
                    if seek & 1:
                        seek |= obj[p + 3] << 24
                        step = 4
            p += step
            at += seek & 0xfffffffc
            if is_type and struct.unpack_from('<I', obj, at)[0] == 0xffffffff:
                out.append((at - code, name))
            if obj[p] == 0:
                break
        p += 1
    # A basic's type slot is 16-byte aligned (its pointer is 16n + 4). An array's
    # content-type field (pointer + 8, so 16n + 12) is a type link too, not a basic.
    out = [(at, name) for at, name in out if at % 16 == 0]
    return code_size, sorted(out)


def by_type(obj):
    """Bytes of the object's data owned by each basic type: from each basic's type
    slot to the next basic's (structures between basics count for the one before)."""
    code_size, found = basics(obj)
    sizes = {}
    for (at, name), nxt in zip(found, [a for a, _ in found[1:]] + [code_size]):
        sizes[name] = sizes.get(name, 0) + nxt - at
    return code_size, sizes


CATEGORIES = (
    ('tfrag', ('tfragment', 'drawable-inline-array-tfrag', 'drawable-tree-tfrag', 'drawable-tree-trans-tfrag',
               'drawable-tree-dirt-tfrag', 'drawable-tree-ice-tfrag', 'drawable-tree-lowres-tfrag')),
    ('tie', ('instance-tie', 'tie-fragment', 'prototype-tie', 'prototype-bucket-tie', 'prototype-array-tie',
             'drawable-tree-instance-tie', 'drawable-inline-array-instance-tie', 'prototype-inline-array-shrub')),
    ('shrub', ('shrubbery', 'instance-shrubbery', 'generic-shrub-fragment', 'prototype-bucket-shrub',
               'prototype-generic-shrub', 'prototype-shrubbery', 'prototype-trans-shrubbery',
               'drawable-tree-instance-shrub', 'drawable-inline-array-instance-shrub')),
    ('visibility', ('level-vis-info',)),
    ('lighting', ('time-of-day-palette',)),
    ('collision', ('collide-frag-mesh', 'collide-fragment', 'drawable-inline-array-collide-fragment',
                   'drawable-tree-collide-fragment')),
    ('entities', ('entity-actor', 'entity-ambient', 'drawable-actor', 'drawable-ambient', 'nav-mesh',
                  'drawable-tree-actor', 'drawable-tree-ambient', 'actor-group', 'res-lump')),
    ('tree', ('draw-node', 'drawable-inline-array-node')),
)
# 'other' is mostly float, string, symbol, vector and int32 arrays: likely the
# entities' res properties, but this scan cannot tell whose they are.


def bsp_categories(obj):
    """The BSP object's data by category (bytes); unlisted types go to 'other'."""
    _, sizes = by_type(obj)
    out = {name: 0 for name, _ in CATEGORIES}
    out['other'] = 0
    for type_name, size in sizes.items():
        cat = next((name for name, members in CATEGORIES if type_name in members), 'other')
        out[cat] += size
    return out


ART_CATEGORIES = (
    ('models', ('merc-ctrl', 'art-joint-geo', 'shadow-geo', 'collide-mesh')),
    ('animations', ('joint-anim-compressed', 'art-joint-anim', 'joint-anim-compressed-control')),
    ('joints', ('joint',)),
)


def art_categories(path):
    """All of a DGO's art groups by category (bytes); unlisted types go to 'other'."""
    out = {name: 0 for name, _ in ART_CATEGORIES}
    out['other'] = 0
    data = path.read_bytes()
    count, = struct.unpack_from('<I', data, 0)
    offset = 64
    kinds = [kind for _, _, kind in objects(path)]
    for i in range(count):
        size, = struct.unpack_from('<I', data, offset)
        if kinds[i] == 'art':
            _, sizes = by_type(data[offset + 64:offset + 64 + size])
            for type_name, n in sizes.items():
                out[next((c for c, members in ART_CATEGORIES if type_name in members), 'other')] += n
        offset += 64 + size
    return out


def summarize(path):
    rows = objects(path)
    totals = {k: sum(s for _, s, kind in rows if kind == k) for k in KINDS}
    largest = max(rows, key=lambda r: r[1])
    return {'name': path.stem, 'objects': len(rows), 'total': sum(totals.values()), **totals, 'largest': largest}


def kib(n):
    return f'{n / 1024:,.0f}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dgo_dir', type=Path)
    parser.add_argument('--markdown', action='store_true', help='print a Markdown table (KiB)')
    parser.add_argument('--bsp', action='store_true', help="split each level's BSP object by category instead")
    parser.add_argument('--art', action='store_true', help="split each archive's art groups by category instead")
    args = parser.parse_args()
    paths = sorted([*args.dgo_dir.glob('*.DGO'), *(args.dgo_dir.glob('*.CGO') if args.art else [])])
    if not paths:
        sys.exit(f'no archives in {args.dgo_dir}')
    if args.art:
        names = [name for name, _ in ART_CATEGORIES] + ['other']
        print('| Archive | Art | ' + ' | '.join(names) + ' |')
        print('| --- | ---: |' + ' ---: |' * len(names))
        for path in paths:
            cats = art_categories(path)
            if sum(cats.values()):
                print(f'| {path.name} | {kib(sum(cats.values()))} | ' + ' | '.join(kib(cats[n]) for n in names) + ' |')
        return
    if args.bsp:
        names = [name for name, _ in CATEGORIES] + ['other']
        print('| Level | BSP | ' + ' | '.join(names) + ' |')
        print('| --- | ---: |' + ' ---: |' * len(names))
        for path in paths:
            vis = [name for name, _, kind in objects(path) if kind == 'level']
            if vis:
                cats = bsp_categories(object_bytes(path, vis[-1]))
                print(f'| {path.stem} | {kib(sum(cats.values()))} | ' + ' | '.join(kib(cats[n]) for n in names) + ' |')
        return
    levels = [summarize(p) for p in paths]
    if args.markdown:
        print('| Level | Objects | Total | Code | Textures | Level (BSP) | Art groups | Largest object |')
        print('| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |')
    for lv in sorted(levels, key=lambda r: -r['total']):
        name, size, kind = lv['largest']
        if args.markdown:
            print(f"| {lv['name']} | {lv['objects']} | {kib(lv['total'])} | {kib(lv['code'])} | {kib(lv['textures'])} "
                  f"| {kib(lv['level'])} | {kib(lv['art'])} | {name} ({kind}, {kib(size)}) |")
        else:
            print(f"{lv['name']:4} {lv['objects']:4} total {kib(lv['total']):>7} code {kib(lv['code']):>5} "
                  f"textures {kib(lv['textures']):>6} level {kib(lv['level']):>6} art {kib(lv['art']):>6} KiB")
    sums = {k: sum(lv[k] for lv in levels) for k in ('total',) + KINDS}
    share = ', '.join(f'{k} {100 * sums[k] / sums["total"]:.0f}%' for k in KINDS)
    print(f'\nAll {len(levels)} levels: {kib(sums["total"])} KiB; {share}')


if __name__ == '__main__':
    main()

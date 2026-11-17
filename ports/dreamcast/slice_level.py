"""A seam slice: the part of a level that touches its neighbour (AI-assisted), ISC.

The original keeps two levels resident wherever two meet: a load boundary asks
for the level Jak is in and the one he walks toward, and continue points on a
seam (beach-start, on Sandover's ground) display both. A 16 MB console holds
one whole level (LEVEL_PLAN.md). A slice of level N for level F is N's
collision near F, as a level of its own the second level slot holds while F is
the level resident:

  python slice_level.py VI1 BEA --iso <iso_data/jak1>   -> build/level-disc/VI1BEA.DGO

Kept: N's background collision (collide fragments) and tie collision
(instances and their prototypes' collide meshes) whose bounding spheres lie
within --margin meters of F's collide fragments or of F's continue points
(level-info.gc), or, across the ground, within --boundary meters of a vis
boundary between the two (load-boundary-data.gc: a vis command naming either
level and no third, on a line that passes within 50 m of both). Crossing one
changes the vis-nick, which makes level_logic.c load the other level whole;
Jak stands on the slice until it has. Each collision tree is rebuilt with two levels: one row of
draw-nodes, flags 0, over the kept leaves, at most eight a node, as the last
level of a retail tree is. Dropped: actors, ambients, cameras, every
visibility table and the visibility BSP (nodes: with none, the original
camera code takes every drawable as visible), the drawable trees only the PS2
renderers read, and every tie prototype no kept instance uses (its slot names
a kept one, which original login reads only for render fields, cut). The bsp-header's name and
nickname become the slice's name (N's nickname then F's, e.g. vi1bea), so it is
never taken for N itself: level_logic.c registers a level-load-info under that
name, and load-begin asks for <NAME>.DGO.

The output stays under build/ (git-ignored); it is derived from user data.
"""
import argparse
import bisect
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from bsp_graph import BspGraph
from build_logic_bsp import RENDER_TREES, encode_table
from stage_jak_art import archive_objects, decode_v4, header, u32

PORT = Path(__file__).resolve().parent
LEVEL_INFO = PORT.parent.parent / 'goal_src/jak1/engine/level/level-info.gc'
NAMES = {'TRA': 'training', 'VI1': 'village1', 'BEA': 'beach', 'JUN': 'jungle', 'JUB': 'jungleb',
         'MIS': 'misty', 'FIC': 'firecanyon', 'VI2': 'village2', 'SUN': 'sunken', 'SUB': 'sunkenb',
         'SWA': 'swamp', 'ROL': 'rolling', 'OGR': 'ogre', 'VI3': 'village3', 'SNO': 'snow',
         'MAI': 'maincave', 'DAR': 'darkcave', 'ROB': 'robocave', 'LAV': 'lavatube', 'CIT': 'citadel',
         'FIN': 'finalboss'}
# bsp-header fields (offsets from the type tag)
VIS_INFO, ACTORS, CAMERAS, NODES, AMBIENTS, BIRTH_ORDER, NAME, NICKNAME = 80, 112, 116, 120, 156, 172, 72, 76
TREES, GROUP_LENGTH, GROUP_DATA = 40, 6, 32
DROP_TREES = RENDER_TREES - {'drawable-tree-instance-tie'} | {'drawable-tree-actor', 'drawable-tree-ambient'}
LEAF_BYTES = {'drawable-inline-array-collide-fragment': 32, 'drawable-inline-array-instance-tie': 64}
BUCKET_COLLIDE = 140   # prototype-bucket-tie collide-frag
TIE_CUTS = (16, 20, 24, 28, 136, 144)  # its render geometry, envmap shader, colours (build_logic_bsp.py)
INSTANCE_COLORS = 8                    # instance-tie color-indices


class SliceGraph(BspGraph):
    """A BspGraph that also splits at what a collide mesh points to: its packed
    vertex and polygon data carry no tag, so without this they belong to
    whatever tagged object precedes them and cannot be dropped with the mesh.
    The same for the header's tables after the visibility data."""

    def __init__(self, obj):
        super().__init__(obj)
        extra = {u32(self.data, at) for at in self.pointers
                 if self.name(self.region(at)) == 'collide-frag-mesh'}
        # and before the header's own tables (all-visible list, pat, texture remap
        # and ids), which follow the visibility data with no tag of their own
        extra |= {u32(self.data, at) & ~15 for at in (32, 44, 52, 60) if at in self.pointers}
        starts = set(self.starts) | {at for at in extra if at % 16 == 0 and 0 < at < len(self.data)}
        starts = sorted(starts)
        self.starts, self.ends = starts, starts[1:] + [len(self.data)]


def continue_points(level):
    """(x, y, z) of each continue point of level-info.gc on `level`."""
    text = LEVEL_INFO.read_text()
    out = []
    for block in re.split(r"'continue-point", text)[1:]:
        m = re.search(r":level\s+'([\w-]+)", block)
        t = re.search(r":trans\s+\(new 'static 'vector((?:\s+:\w\s+-?[\d.]+)+)", block)
        if m and t and m.group(1) == level:
            v = dict(re.findall(r':(\w)\s+(-?[\d.]+)', t.group(1)))
            out.append(tuple(float(v.get(k, 0)) for k in 'xyz'))
    return out


LOAD_BOUNDARIES = PORT.parent.parent / 'goal_src/jak1/engine/level/load-boundary-data.gc'
ALL_LEVELS = set(NAMES.values()) | {k.lower() for k in NAMES}


def seam_boundaries(names, near_a, near_b, reach):
    """Vis boundaries naming levels of `names` (level names or nicknames) and no
    other, that pass within `reach` (game units, x/z) of both levels: the lines
    where the vis-nick, and with it the level level_logic.c loads whole, changes
    from one of the two to the other. Each is a list
    of (x, z) points, closed polygons with their first point again at the end."""
    text = LOAD_BOUNDARIES.read_text()
    out = []
    for block in text.split('(static-load-boundary')[1:]:
        m = re.search(r':points\s*\(([-\d.\s]+)\)', block)
        cmds = ' '.join(re.findall(r':(?:fwd|bwd)\s*\(([^)]*)\)', block))
        words = set(re.findall(r'[\w-]+', cmds))
        levels = words & ALL_LEVELS
        if not m or 'vis' not in words or not levels or not levels <= set(names):
            continue
        v = [float(t) for t in m.group(1).split()]
        pts = list(zip(v[::2], v[1::2]))
        if re.search(r':flags\s*\([^)]*closed', block):
            pts.append(pts[0])
        if any(near_a.flat(p, reach) for p in pts) and any(near_b.flat(p, reach) for p in pts):
            out.append(pts)
    return out


def along(points, step):
    """Points every `step` along a polyline."""
    out = [points[0]]
    for (x0, z0), (x1, z1) in zip(points, points[1:]):
        n = max(1, int(math.dist((x0, z0), (x1, z1)) // step))
        out += [(x0 + (x1 - x0) * k / n, z0 + (z1 - z0) * k / n) for k in range(1, n + 1)]
    return out


def bsp_object(dgo):
    objs = [(name, obj) for _, name, obj in archive_objects(dgo)
            if u32(obj, 8) == 4 and (name.endswith('-vis') or name.endswith('-bsp'))]
    if len(objs) != 1:
        raise SystemExit(f'{dgo}: expected one BSP object, found {len(objs)}')
    return objs[0]


def sphere(data, at):
    return struct.unpack_from('<4f', data, at + 16)


def leaf_spheres(g):
    """Bounding spheres of a BSP's collide fragments."""
    out = []
    for r, s in enumerate(g.starts):
        if g.name(r) == 'drawable-inline-array-collide-fragment':
            n = struct.unpack_from('<h', g.data, s + GROUP_LENGTH)[0]
            out += [sphere(g.data, s + 32 + 32 * i) for i in range(n)]
    return out


class Near:
    """Sphere test against F's fragments and continue points, on an x/z grid,
    and an x/z test against circles (around the seam's load boundaries)."""

    def __init__(self, spheres, margin, circles=(), cell=40960.0):
        self.margin, self.cell, self.grid = margin, cell, collections.defaultdict(list)
        self.circles = list(circles)
        self.reach = max((w for *_, w in spheres), default=0) + margin
        for s in spheres:
            self.grid[(int(s[0] // cell), int(s[2] // cell))].append(s)

    def flat(self, p, reach):
        """Whether x/z point p lies within reach of a sphere, ignoring height."""
        x, z = p
        span = int((self.reach + reach) // self.cell) + 1
        cx, cz = int(x // self.cell), int(z // self.cell)
        for i in range(cx - span, cx + span + 1):
            for k in range(cz - span, cz + span + 1):
                for a, b, c, d in self.grid.get((i, k), ()):
                    if (x - a) ** 2 + (z - c) ** 2 < (d + reach) ** 2:
                        return True
        return False

    def __call__(self, s):
        x, y, z, w = s
        if any((x - a) ** 2 + (z - c) ** 2 < (w + r) ** 2 for a, c, r in self.circles):
            return True
        span = int((w + self.reach) // self.cell) + 1
        cx, cz = int(x // self.cell), int(z // self.cell)
        for i in range(cx - span, cx + span + 1):
            for k in range(cz - span, cz + span + 1):
                for a, b, c, d in self.grid.get((i, k), ()):
                    if (x - a) ** 2 + (y - b) ** 2 + (z - c) ** 2 < (w + d + self.margin) ** 2:
                        return True
        return False


def enclose(spheres):
    lo = [min(s[k] - s[3] for s in spheres) for k in range(3)]
    hi = [max(s[k] + s[3] for s in spheres) for k in range(3)]
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    r = max(math.dist(c, s[:3]) + s[3] for s in spheres)
    return (*c, r)


def build(obj, near, name, whole=False):
    """A slice of obj (a BSP object) for name; with whole, the level itself with
    only its tie trees rebuilt, of the instances that collide (build_logic_bsp.py)."""
    g = SliceGraph(obj)
    data = bytearray(g.data)
    pointers = set(g.pointers)
    truncate = {}
    report = collections.Counter()

    def target(at):
        return u32(data, at) - 4

    def point(site, tag):
        struct.pack_into('<I', data, site, tag + 4)
        pointers.add(site)

    def clear(site):
        struct.pack_into('<I', data, site, 0)
        pointers.discard(site)

    for at in [] if whole else [VIS_INFO + 4 * i for i in range(8)] + [ACTORS, CAMERAS, NODES, AMBIENTS, BIRTH_ORDER]:
        if at in pointers:
            clear(at)

    def compact_leaves(arr, size, keep):
        """Moves the kept leaves of an inline array to its front."""
        count = struct.unpack_from('<h', data, arr + GROUP_LENGTH)[0]
        first = arr + 32
        old = bytes(data[first:first + size * count])
        inside = sorted(p for p in pointers if first <= p < first + size * count)
        for p in inside:
            pointers.discard(p)
        for j, i in enumerate(keep):
            data[first + size * j:first + size * (j + 1)] = old[size * i:size * (i + 1)]
            for p in inside:
                if size * i <= p - first < size * (i + 1):
                    pointers.add(p - size * i + size * j)
        struct.pack_into('<h', data, arr + GROUP_LENGTH, len(keep))
        truncate[g.region(arr)] = (32 + size * len(keep) + 15) // 16 * 16
        return [sphere(old, size * i) for i in keep]  # spheres of the kept leaves, in order

    def rebuild(tree):
        n = struct.unpack_from('<h', data, tree + GROUP_LENGTH)[0]
        levels = [target(tree + GROUP_DATA + 4 * i) for i in range(n)]
        leaf = levels[-1]
        kind = g.tags.get(leaf)
        size = LEAF_BYTES[kind]
        count = struct.unpack_from('<h', data, leaf + GROUP_LENGTH)[0]
        solid = lambda i: True
        if kind == 'drawable-inline-array-instance-tie':
            # an instance collides only through its prototype's collide meshes:
            # one whose prototype has none (grass, plants, lamps) is never collided with
            protos = target(target(tree + 8) + 4)  # proxy-prototype-array-tie -> prototype-array-tie
            solid = lambda i: target(protos + 16 + 4 * struct.unpack_from('<H', data, leaf + 32 + size * i + 6)[0]) \
                + BUCKET_COLLIDE in pointers
        keep = [i for i in range(count) if solid(i) and near(sphere(data, leaf + 32 + size * i))]
        report[f'{kind[22:]}s'] += count
        report[f'{kind[22:]}s kept'] += len(keep)
        if not keep:
            return None
        spheres = compact_leaves(leaf, size, keep)
        if kind == 'drawable-inline-array-instance-tie':  # color-indices: the PS2 renderer's alone
            for j in range(len(keep)):
                if leaf + 32 + size * j + INSTANCE_COLORS in pointers:
                    clear(leaf + 32 + size * j + INSTANCE_COLORS)
        for i in range(n):
            clear(tree + GROUP_DATA + 4 * i)
        if n == 1:
            point(tree + GROUP_DATA, leaf)
            return leaf
        nodes = levels[-2]
        if g.tags.get(nodes) != 'drawable-inline-array-node':
            raise ValueError(f'{kind}: the level above the leaves is {g.tags.get(nodes)}')
        old_count = struct.unpack_from('<h', data, nodes + GROUP_LENGTH)[0]
        template = bytes(data[nodes + 32:nodes + 64])
        for p in [p for p in pointers if nodes + 32 <= p < nodes + 32 + 32 * old_count]:
            pointers.discard(p)
        m = (len(keep) + 7) // 8
        for j in range(m):
            at = nodes + 32 + 32 * j
            data[at:at + 32] = template
            data[at + 6] = min(8, len(keep) - 8 * j)  # child-count
            data[at + 7] = 0                            # flags: the children are leaves
            point(at + 8, leaf + 32 + size * 8 * j)
            struct.pack_into('<4f', data, at + 16, *enclose(spheres[8 * j:8 * j + 8]))
        struct.pack_into('<h', data, nodes + GROUP_LENGTH, m)
        truncate[g.region(nodes)] = 32 + 32 * m
        struct.pack_into('<h', data, tree + GROUP_LENGTH, 2)
        point(tree + GROUP_DATA, nodes)
        point(tree + GROUP_DATA + 4, leaf)
        return leaf

    array, trees = g.trees()
    kept, used = [], collections.defaultdict(set)
    for slot, at, kind in trees:
        clear(slot)
        if whole and kind != 'drawable-tree-instance-tie':
            kept.append(at)
            continue
        if kind in DROP_TREES:
            report['trees dropped'] += 1
            continue
        if kind not in ('drawable-tree-collide-fragment', 'drawable-tree-instance-tie'):
            raise ValueError(f'unexpected tree {kind}')
        leaf = rebuild(at)
        if leaf is None:
            report['trees emptied'] += 1
            continue
        kept.append(at)
        if kind == 'drawable-tree-instance-tie':
            protos = target(target(at + 8) + 4)  # proxy-prototype-array-tie -> prototype-array-tie
            count = struct.unpack_from('<h', data, leaf + GROUP_LENGTH)[0]
            used[protos] |= {struct.unpack_from('<H', data, leaf + 32 + 64 * i + 6)[0] for i in range(count)}
    for i, at in enumerate(kept):
        point(array + GROUP_DATA + 4 * i, at)
    struct.pack_into('<h', data, array + GROUP_LENGTH, len(kept))
    # tie prototypes: collision only for those a kept instance uses, no render data at all
    # (original prototype-array-tie login reads only those render fields, so a
    # slot no kept instance indexes may name another, kept, bucket: the stub)
    for protos, indices in used.items():
        stub = target(protos + 16 + 4 * min(indices))
        for i in range(u32(data, protos + 4)):
            bucket = target(protos + 16 + 4 * i)
            for cut in TIE_CUTS:
                if bucket + cut in pointers:
                    clear(bucket + cut)
            if i not in indices:
                point(protos + 16 + 4 * i, stub)
            report['prototypes kept' if i in indices else 'prototypes cut'] += 1

    # the slice's own name and nickname
    bindings = []
    for sym, methods, sites in g.bindings:
        rest = [s for s in sites if whole or methods is not None or s not in (NAME, NICKNAME)]
        if rest:
            bindings.append((sym, methods, rest))
    if not whole:
        bindings.append((name, None, [NAME, NICKNAME]))

    # what stays: everything still reachable from the header
    # (a pointer's value: a basic's is its tag + 4, plain data's its address)
    edges = collections.defaultdict(set)
    for at in pointers:
        edges[g.region(at)].add(g.region(u32(data, at)))
    keep, stack = set(), [0]
    while stack:
        r = stack.pop()
        if r not in keep:
            keep.add(r)
            stack += edges[r]
    remap, pieces, out_at = {}, [], 0
    for r in sorted(keep):
        start = g.starts[r]
        end = start + truncate.get(r, g.ends[r] - start)
        if start % 16 or (end - start) % 16:
            raise ValueError('object extent is not 16-byte aligned')
        remap[r] = out_at - start
        pieces.append(bytes(data[start:end]))
        out_at += end - start
    new_data = bytearray(b''.join(pieces))

    def inside(offset):
        r = g.region(offset)
        return r in remap and offset < g.starts[r] + truncate.get(r, g.ends[r] - g.starts[r])

    new_pointers = []
    for site in sorted(pointers):
        if not inside(site):
            continue
        v = u32(data, site)
        if not inside(v):
            raise ValueError(f'kept pointer at {site:#x} reaches dropped data {v:#x}')
        new_site = site + remap[g.region(site)]
        struct.pack_into('<I', new_data, new_site, v + remap[g.region(v)])
        new_pointers.append(new_site)
    new_bindings = []
    for sym, methods, sites in bindings:
        moved = [s + remap[g.region(s)] for s in sites if inside(s)]
        if moved:
            new_bindings.append((sym, methods, moved))
    report.update(source_bytes=len(g.data), slice_bytes=len(new_data), objects=len(keep), tree_slots=len(trees))
    return new_data, new_pointers, new_bindings, dict(report)


def merge(a, b):
    """One BSP from two slices: b's objects after a's, and b's trees in a's
    drawable-tree-array (which keeps a slot for every tree a's level had). b's
    own header stays, unreferenced."""
    a_data, a_pointers, a_bindings, a_report = a
    b_data, b_pointers, b_bindings, _ = b
    off = len(a_data)
    data = bytearray(a_data) + bytearray(b_data)
    for p in b_pointers:
        struct.pack_into('<I', data, off + p, u32(b_data, p) + off)
    pointers = list(a_pointers) + [p + off for p in b_pointers]
    bindings = list(a_bindings) + [(n, m, [x + off for x in sites]) for n, m, sites in b_bindings]
    array, b_array = u32(a_data, TREES) - 4, u32(b_data, TREES) - 4
    n, k = (struct.unpack_from('<h', d, at + GROUP_LENGTH)[0] for d, at in ((a_data, array), (b_data, b_array)))
    if n + k > a_report['tree_slots']:
        raise ValueError(f'{n + k} trees for {a_report["tree_slots"]} slots')
    for i in range(k):
        at = array + GROUP_DATA + 4 * (n + i)
        struct.pack_into('<I', data, at, u32(b_data, b_array + GROUP_DATA + 4 * i) + off)
        pointers.append(at)
    struct.pack_into('<h', data, array + GROUP_LENGTH, n + k)
    return data, pointers, bindings


def encode(data, pointers, bindings):
    table = encode_table(pointers, bindings, len(data))
    out = struct.pack('<4I', 0xffffffff, len(table), 4, len(data)) + bytes(data) + table
    check_data, check_pointers, check_bindings = decode_v4(out, 'bsp-header')
    if sorted(check_pointers) != sorted(pointers):
        raise ValueError('pointer table round trip differs')
    key = lambda bs: sorted((n, -1 if m is None else m, tuple(sorted(x))) for n, m, x in bs)
    if key(check_bindings) != key(bindings):
        raise ValueError('binding table round trip differs')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', help='one level of the seam, e.g. BEA')
    ap.add_argument('neighbour', help='the other, e.g. VI1')
    ap.add_argument('--iso', type=Path, required=True, help='the extracted disc: has DGO/')
    ap.add_argument('--margin', type=float, default=20.0, help='meters around the other level kept')
    ap.add_argument('--boundary', type=float, default=40.0,
                    help="meters around the vis boundaries between the two levels kept")
    ap.add_argument('--output', type=Path, default=PORT / 'build/level-disc')
    a = ap.parse_args()
    codes = a.level.upper(), a.neighbour.upper()
    out_dir = a.output.resolve()
    if not out_dir.is_relative_to((PORT / 'build').resolve()):
        raise SystemExit('retail-derived output must be under ports/dreamcast/build/')
    name = ''.join(codes).lower()
    objs = [bsp_object(a.iso / 'DGO' / f'{c}.DGO')[1] for c in codes]
    leaves = [leaf_spheres(BspGraph(o)) for o in objs]
    points = [continue_points(NAMES[c]) for c in codes]
    near_all = [Near(l, 0.0) for l in leaves]
    names = [c.lower() for c in codes] + [NAMES[c] for c in codes]
    lines = seam_boundaries(names, near_all[0], near_all[1], 50 * 4096.0)
    circles = [(x, z, a.boundary * 4096.0) for line in lines for x, z in along(line, 8 * 4096.0)]
    parts, reports = [], []
    for i in (0, 1):
        other = 1 - i
        near = Near(leaves[other] + [(*p, 0.0) for p in points[other]], a.margin * 4096.0, circles)
        part = build(objs[i], near, name)
        parts.append(part)
        reports.append(dict(level=codes[i], **part[3]))
    out = encode(*merge(*parts))
    stem = name.upper()
    body = header(len(out), f'{name}-vis') + out + bytes((-len(out)) % 16)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f'{stem}.DGO').write_bytes(header(1, f'{stem}.DGO') + body)
    print(json.dumps(dict(name=name, dgo=f'{stem}.DGO', dgo_bytes=64 + len(body), parts=reports,
                          margin_m=a.margin, boundary_m=a.boundary, boundaries=len(lines),
                          continue_points=[len(p) for p in points], ai_assisted=True,
                          output_sha256=hashlib.sha256(out).hexdigest()), indent=1))


if __name__ == '__main__':
    main()

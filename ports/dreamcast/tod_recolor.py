"""Recolor Jak 1 background GLB corners using their own native TOD palettes.

    python tod_recolor.py IN.glb DGO --slot N --out OUT.glb [--json REPORT]
    python tod_recolor.py IN.glb DGO --noon LEVEL --out OUT.glb [--json REPORT]

Sources: decompiler/level_extractor/BspHeader.cpp (TimeOfDayPalette,
DrawableTreeTfrag, PrototypeBucketTie, DrawableTreeInstanceShrub),
extract_common.cpp:63-82 (row * 8 + sample, RGBA bytes, zero row padding),
extract_tfrag.cpp:2181-2183, extract_shrub.cpp:340,604 and
extract_tie.cpp:663-666. Native offsets follow goal_src/jak1/engine/{draw,
gfx/background,gfx/tfrag,gfx/shrub}/*-h.gc; pointers in v4 data are offsets
relative to the data segment, basic pointers are four bytes past their type.

Noon blends and level assignments are read from mood-tables.gc. As in
mood.gc update-mood-palette, morph-start at hour 12 weights snapshot2;
omitted fields are zero. This is the palette blend, not a complete light kit.

fr3_to_gltf.cpp:274-352,405-449,735 exports float32 RGB = palette[row][0]/255,
alpha=1. It omits the palette row. Matching uses exact float32 RGB bytes,
never nearest-color guesses or material factors. Multiple matching rows with
one target RGB are output-unique (reported separately from row-unique).
If target RGBs differ, use the lowest native row and count an ambiguous
corner. Unmatched corners retain their color. The original row cannot be
recovered from GLB draw order alone: TIE draws are regrouped by prototype,
material/category, and strips are unstripped/fixed up; native per-fragment
color-index streams and VU extraction would have to be replayed. This tool
does not claim to reconstruct those indices. Shrub instances have a native
row index, but this tool deliberately reports the RGB inversion ambiguity
on the same basis for all three kinds.

Each indexed TRIANGLES occurrence counts as one corner, including repeated
indices and placements; means are raw COLOR_0 RGB in byte units, before
material/texture multiplication. Only referenced color elements are patched;
unreferenced data, alpha, JSON, indices, textures, other chunks and padding
remain byte-identical. Shared color accessors are resolved per referenced
vertex, not recolored wholesale with one prototype's palette. Conflicting
overlapping storage is rejected instead of corrupting another node. Wind TIE
nodes are assigned their prototype palette by their native instance matrix,
not by a level-wide color search. Unsupported accessors/roots fail explicitly.

(AI-assisted), ISC.
"""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct

import level_contents


MOOD_SOURCE = 'goal_src/jak1/engine/gfx/mood/mood-tables.gc'


def noon_sources():
    """Read the four named palette day tables and their mood assignments."""
    text = (Path(__file__).resolve().parents[2] / MOOD_SOURCE).read_text()
    tables = {}
    definitions = list(re.finditer(r'\(define \*([^*]+)\*', text))
    for index, match in enumerate(definitions):
        name = match[1]
        if not name.endswith('-palette-interp-table'):
            continue
        end = definitions[index + 1].start() if index + 1 < len(definitions) else len(text)
        hours = re.findall(r"\(new 'static 'sky-color-hour\b([^()]*)\)", text[match.end():end])
        if len(hours) != 24:
            raise ValueError(f'{name}: expected 24 palette hours')
        fields = dict(re.findall(r':([\w-]+)\s+([\d.]+)', hours[12]))
        tables[name] = (int(fields.get('snapshot1', 0)), int(fields.get('snapshot2', 0)),
                        float(fields.get('morph-start', 0)))
    assignments = re.finditer(
        r'\(set! \(-> \*([^*]+)-mood\* palette-interp\) \*([^*]+)\*\)', text)
    return {m[1]: (m[2].removesuffix('-palette-interp-table'), tables[m[2]],
                    text.count('\n', 0, m.start()) + 1)
            for m in assignments if m[2] in tables}


NOON_SOURCES = noon_sources()
TFRAG_TYPES = {
    'drawable-tree-tfrag': 'normal', 'drawable-tree-trans-tfrag': 'trans',
    'drawable-tree-lowres-tfrag': 'lowres', 'drawable-tree-dirt-tfrag': 'dirt',
    'drawable-tree-ice-tfrag': 'ice',
}
POLICY = ('Ambiguous RGB inversion: choose lowest native row; unmatched: retain RGB. '
          'Draw order alone cannot recover original row indices; native VU/fragment '
          'extraction is not replayed. Equal-target duplicate rows count as unique RGB, '
          'with duplicate_rows_same_target_corners reported separately.')


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def rgb_bytes(rgb):
    return struct.pack('<3f', *(v / 255.0 for v in rgb[:3]))


class Native:
    """Bounds-checked subset of the Jak 1 v4 BSP, with all eight samples."""

    def __init__(self, obj):
        size, found = level_contents.basics(obj)
        if len(obj) < 16 + size:
            raise ValueError('truncated BSP data')
        self.data = obj[16:16 + size]
        self.types = {at + 4: kind for at, kind in found}
        arrays = [p for p, kind in self.types.items() if kind == 'drawable-tree-array']
        if len(arrays) != 1:
            raise ValueError('expected one drawable-tree-array')
        p = arrays[0]
        self.trees = [self.u32(p + 28 + 4 * i) for i in range(self.read('<h', p + 2)[0])]
        self.palettes = {}
        self.roots = {}
        self.wind = defaultdict(lambda: defaultdict(set))
        counts = Counter()
        for p in self.trees:
            kind = self.types[p]
            if kind in TFRAG_TYPES:
                key = 'tfrag-' + TFRAG_TYPES[kind]
                if key in self.roots:
                    raise ValueError('multiple tfrag trees with the same exported name')
                self.roots[key] = self.palette(self.u32(p + 8), padded=True)
            elif kind == 'drawable-tree-instance-shrub':
                key = f'shrub-{counts["shrub"]}'
                counts['shrub'] += 1
                self.roots[key] = self.palette(self.u32(p + 8), padded=True)
            elif kind == 'drawable-tree-instance-tie':
                key = f'tie-{counts["tie"]}'
                counts['tie'] += 1
                proxy = self.u32(p + 4)
                array = self.u32(proxy)
                if self.types.get(array) != 'prototype-array-tie':
                    raise ValueError('bad TIE prototype array')
                protos = {}
                buckets = set()
                for i in range(self.u32(array)):
                    bucket = self.u32(array + 12 + 4 * i)
                    if self.types.get(bucket) != 'prototype-bucket-tie':
                        raise ValueError('bad TIE bucket')
                    name_at = self.u32(bucket)
                    length = self.u32(name_at)
                    self.read(f'<{length}s', name_at + 4)
                    name = self.data[name_at + 4:name_at + 4 + length].decode('ascii')
                    palette = self.palette(self.u32(bucket + 140))
                    if name in protos and protos[name] != palette:
                        raise ValueError('duplicate prototype name with different palettes')
                    protos[name] = palette
                    buckets.add(bucket)
                self.roots[key] = protos
                # Matrices retain prototype provenance even on unnamed wind-group nodes.
                for inst, inst_kind in self.types.items():
                    if inst_kind != 'instance-tie' or self.u32(inst + 8) not in buckets:
                        continue
                    bucket = self.u32(inst + 8)
                    if not self.read('<f', bucket + 56)[0]:  # rdists.w / stiffness
                        continue
                    origin = self.read('<16h', inst + 28)
                    sphere = self.read('<4f', inst + 12)
                    matrix = [v / 4096.0 for v in origin[:12]]
                    matrix[3] = 0.0  # extract_tie.cpp collect_instance_info
                    matrix += [f32(f32(origin[12 + j] * 64.0) + sphere[j]) / 4096.0
                               for j in range(3)] + [origin[15] / 64.0]
                    self.wind[key][struct.pack('<16f', *matrix)].add(self.u32(bucket + 140))

    def read(self, fmt, at):
        if at < 0 or at + struct.calcsize(fmt) > len(self.data):
            raise ValueError(f'native data range at {at:#x}')
        return struct.unpack_from(fmt, self.data, at)

    def u32(self, at):
        return self.read('<I', at)[0]

    def palette(self, at, padded=False):
        if at not in self.palettes:
            width, height, pad = self.read('<3I', at)
            if width != 8 or not height or pad:
                raise ValueError('invalid time-of-day palette dimensions')
            self.read(f'<{height * 32}s', at + 12)
            self.palettes[at] = tuple(tuple(self.read('<4B', at + 12 + row * 32 + t * 4)
                                            for t in range(8)) for row in range(height))
        rows = self.palettes[at]
        return rows + (((0, 0, 0, 0),) * 8,) * (-len(rows) % 4) if padded else rows

    def for_node(self, root, node):
        palette = self.roots[root]
        if not root.startswith('tie-'):
            return palette
        name = node.get('name', '')
        if name in palette:
            return palette[name]
        if name.startswith('wind-group-'):
            matrix = struct.pack('<16f', *node.get('matrix', ()))
            candidates = self.wind[root].get(matrix, set())
            rows = {self.palette(at) for at in candidates}
            if len(rows) == 1:
                return rows.pop()
            raise ValueError(f'{root}/{name}: wind matrix does not identify one palette')
        raise ValueError(f'{root}: unknown TIE prototype {name!r}')


def chunks(blob):
    """Return (kind, payload offset, length); retain original chunk bytes."""
    if len(blob) < 12 or struct.unpack_from('<4sII', blob) != (b'glTF', 2, len(blob)):
        raise ValueError('expected complete GLB v2')
    out, at = [], 12
    while at < len(blob):
        if at + 8 > len(blob):
            raise ValueError('truncated GLB chunk header')
        size, kind = struct.unpack_from('<I4s', blob, at)
        if size % 4 or at + 8 + size > len(blob):
            raise ValueError('invalid GLB chunk length')
        out.append((kind, at + 8, size))
        at += 8 + size
    if not out or out[0][0] != b'JSON':
        raise ValueError('missing JSON chunk')
    return out


def accessor(doc, binary, index, types, shapes):
    a = doc['accessors'][index]
    if 'sparse' in a or a['componentType'] not in types or a['type'] not in shapes:
        raise ValueError('unsupported accessor (need dense float colors / unsigned indices)')
    if a.get('normalized', False):
        raise ValueError('unexpected normalized accessor')
    code = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
    fmt = '<' + code * {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
    size = struct.calcsize(fmt)
    v = doc['bufferViews'][a['bufferView']]
    if v.get('buffer', 0) != 0:
        raise ValueError('external/multiple buffers unsupported')
    start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = v.get('byteStride', 0) or size
    count = a['count']
    end = start + (count - 1) * stride + size if count else start
    if (count < 0 or a.get('byteOffset', 0) < 0 or v.get('byteOffset', 0) < 0
            or stride < size or end > len(binary)
            or end > v.get('byteOffset', 0) + v['byteLength']):
        raise ValueError('accessor outside buffer view')
    return start, stride, count, fmt


def color_map(palette, slot):
    first, second, weight = slot if isinstance(slot, tuple) else (slot, slot, 0.0)
    lookup = defaultdict(list)
    for row, samples in enumerate(palette):
        target = tuple((1.0 - weight) * a + weight * b
                       for a, b in zip(samples[first], samples[second]))
        lookup[rgb_bytes(samples[0])].append((row, rgb_bytes(target)))
    result = {}
    for source, candidates in lookup.items():
        targets = {target for _, target in candidates}
        result[source] = (candidates[0][1], 'ambiguous' if len(targets) > 1 else 'unique',
                          len(candidates) > 1 and len(targets) == 1)
    return result


def recolor(blob, native, slot):
    """Return patched GLB and exact corner-weighted report; no numpy needed."""
    first, second, blend_weight = slot if isinstance(slot, tuple) else (slot, slot, 0.0)
    if not (0 <= first < 8 and 0 <= second < 8 and 0.0 <= blend_weight <= 1.0):
        raise ValueError('slots must be in 0..7 and blend weight in 0..1')
    parts = chunks(blob)
    _, js, size = parts[0]
    doc = json.loads(blob[js:js + size])
    bins = [(at, n) for kind, at, n in parts if kind == b'BIN\0']
    if len(bins) != 1 or len(doc.get('buffers', [])) != 1 or 'uri' in doc['buffers'][0]:
        raise ValueError('expected one embedded binary buffer')
    bin_at, bin_size = bins[0]
    declared = doc['buffers'][0]['byteLength']
    if declared > bin_size or declared < 0:
        raise ValueError('invalid buffer length')
    binary = memoryview(blob)[bin_at:bin_at + declared]
    out = bytearray(blob)
    writes, maps, rows = {}, {}, []
    groups = defaultdict(list)
    scene = doc['scenes'][doc.get('scene', 0)]
    visited = set()
    for root_index in scene['nodes']:
        root = doc['nodes'][root_index]
        name = root.get('name', '')
        keys = [k for k in native.roots if name.endswith('-' + k)]
        if len(keys) != 1:
            raise ValueError(f'no native palette for GLB root {name!r}')
        key = keys[0]
        category = 'terrain' if key.startswith('tfrag-') else key.split('-')[0]
        stack = [(root_index, frozenset())]
        while stack:
            ni, ancestors = stack.pop()
            if ni in ancestors:
                raise ValueError('cyclic node graph')
            node = doc['nodes'][ni]
            stack.extend((child, ancestors | {ni}) for child in reversed(node.get('children', [])))
            if 'mesh' not in node:
                continue
            palette = native.for_node(key, node)
            # Identical palette tuples can share their small inversion table.
            if palette not in maps:
                maps[palette] = color_map(palette, slot)
            lookup = maps[palette]
            counts = Counter()
            before, after = [0.0] * 3, [0.0] * 3
            for prim in doc['meshes'][node['mesh']]['primitives']:
                if prim.get('mode', 4) != 4 or 'COLOR_0' not in prim['attributes']:
                    raise ValueError('expected TRIANGLES with COLOR_0')
                ca = prim['attributes']['COLOR_0']
                start, stride, count, _ = accessor(doc, binary, ca, (5126,), ('VEC3', 'VEC4'))
                if 'indices' in prim:
                    ia, istep, icount, fmt = accessor(doc, binary, prim['indices'],
                                                      (5121, 5123, 5125), ('SCALAR',))
                    indices = (struct.unpack_from(fmt, binary, ia + i * istep)[0] for i in range(icount))
                else:
                    icount, indices = count, range(count)
                if icount % 3:
                    raise ValueError('triangle index count is not divisible by three')
                # Count multiplicities first; repeated corners still contribute exactly.
                for vertex, weight in Counter(indices).items():
                    if vertex >= count:
                        raise ValueError('color vertex index out of bounds')
                    at = start + vertex * stride
                    source = bytes(binary[at:at + 12])
                    target, status, same = lookup.get(source, (source, 'unmatched', False))
                    counts[status] += weight
                    counts['duplicate_rows_same_target_corners'] += weight * same
                    for k, value in enumerate(struct.unpack('<3f', source)):
                        before[k] += value * 255 * weight
                    for k, value in enumerate(struct.unpack('<3f', target)):
                        after[k] += value * 255 * weight
                    if at in writes and writes[at] != target:
                        raise ValueError('conflicting palettes on overlapping COLOR_0 storage')
                    writes[at] = target
                    visited.add((ca, vertex))
            row = dict(root=name, node=node.get('name', ''), category=category,
                       **{k: counts[k] for k in ('unique', 'ambiguous', 'unmatched',
                                                 'duplicate_rows_same_target_corners')})
            row.update(corners=sum(counts[k] for k in ('unique', 'ambiguous', 'unmatched')),
                       rgb_sum_before=before, rgb_sum_after=after)
            rows.append(row)
            groups[category].append(row)
    previous_end = 0
    for at in sorted(writes):
        value = writes[at]
        overlap = max(0, previous_end - at)
        if overlap and out[bin_at + at:bin_at + at + overlap] != value[:overlap]:
            raise ValueError('conflicting palettes on overlapping COLOR_0 storage')
        out[bin_at + at:bin_at + at + 12] = value
        previous_end = at + 12

    def summary(items):
        result = {k: sum(r[k] for r in items) for k in
                  ('corners', 'unique', 'ambiguous', 'unmatched', 'duplicate_rows_same_target_corners')}
        for when in ('before', 'after'):
            result['mean_rgb_' + when] = [sum(r['rgb_sum_' + when][k] for r in items) / result['corners']
                                          for k in range(3)] if result['corners'] else None
        return result

    selected = first if first == second or blend_weight == 0 else second if blend_weight == 1 else None
    report = {'slot': selected, 'snapshots': [first, second], 'snapshot2_weight': blend_weight,
              'attribution': '(AI-assisted), ISC.', 'policy': POLICY,
              'mean_units': 'raw RGB bytes (0..255), weighted by triangle corners; alpha unchanged',
              'layout_preserved': True, 'json_byte_identical': True,
              'referenced_color_elements': len(visited),
              'changed_bytes': sum(a != b for a, b in zip(blob, out)),
              'total': summary(rows),
              'groups': {k: summary(groups[k]) for k in ('terrain', 'tie', 'shrub')},
              'vil3-norm-wallmed-02.mb': summary([r for r in rows if r['node'] == 'vil3-norm-wallmed-02.mb']),
              'nodes': [dict(root=r['root'], node=r['node'], category=r['category'], **summary([r])) for r in rows]}
    return bytes(out), report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('glb', type=Path)
    ap.add_argument('dgo', type=Path)
    choice = ap.add_mutually_exclusive_group(required=True)
    choice.add_argument('--slot', type=int, choices=range(8))
    choice.add_argument('--noon', choices=sorted(NOON_SOURCES))
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--json', type=Path)
    args = ap.parse_args(argv)
    destinations = [args.out] + ([args.json] if args.json else [])
    if len({p.resolve() for p in destinations}) != len(destinations):
        ap.error('output GLB and report paths must differ')
    for path in destinations:
        if path.exists():
            ap.error(f'output already exists: {path}')
    name = next(n for n, _, kind in level_contents.objects(args.dgo) if kind == 'level')
    slot = NOON_SOURCES[args.noon][1] if args.noon else args.slot
    output, report = recolor(args.glb.read_bytes(), Native(level_contents.object_bytes(args.dgo, name)), slot)
    report.update(input=str(args.glb), dgo=str(args.dgo), output=str(args.out))
    if args.noon:
        table, _, line = NOON_SOURCES[args.noon]
        report.update(level=args.noon, noon_source=f'{MOOD_SOURCE}:{line}', noon_table=table)
    for path in destinations:
        path.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('xb') as f:
        f.write(output)
    if args.json:
        with args.json.open('x', encoding='utf-8', newline='\n') as f:
            json.dump(report, f, indent=2, allow_nan=False)
            f.write('\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'nodes'}, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()

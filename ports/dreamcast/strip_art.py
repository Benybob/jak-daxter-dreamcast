"""Cut the PS2 merc geometry out of a retail art group (AI-assisted), ISC.

A level's art groups are mostly merc models: VU1 fragment data only the PS2
merc renderer reads (1.25 of Sandover Village's 1.55 MiB of art). The
Dreamcast draws models from converted bundles (make_skin_models.py), and the
game needs the rest of an art group as it is: the merc-ctrl itself (its name
selects the converted model), joint geometry, animations, shadow and eye data.

Each merc-ctrl keeps its header and one zeroed effect slot with effect-count 0
(village3-obs writes effect 0's bits without looking at the count); the
pointers its effects held are removed, and every 16-byte-aligned piece of the
object that nothing kept reaches any more is dropped. The result is a valid
retail-format v4 object, decoded again as a check.

Used by build_logic_bsp.py --art --strip-merc. Reads the owner's data; the
output stays under build/.
"""
import bisect
import struct

from stage_jak_art import decode_v4, u32

MERC_HEADER = 32        # merc-ctrl header, from the tag
EFFECT_COUNT = 24       # merc-ctrl-header effect-count
EFFECTS = 112           # merc-ctrl effect[0], from the tag
EFFECT_BYTES = 32


def strip(obj):
    """Return (stripped v4 object, report). Unchanged when it holds no merc-ctrl."""
    from build_logic_bsp import encode_table
    data, pointers, bindings = decode_v4(obj, 'art-group')
    data = bytearray(data)
    tags = {at: name for name, methods, sites in bindings if methods is not None for at in sites}
    mercs = sorted(at for at, name in tags.items() if name == 'merc-ctrl')
    if not mercs:
        return obj, dict(mercs=0, before=len(data), after=len(data))
    cut = set()
    for tag in mercs:
        count = u32(data, tag + MERC_HEADER + EFFECT_COUNT)
        first, end = tag + EFFECTS, tag + EFFECTS + EFFECT_BYTES * max(count, 1)
        if count > 64 or end > len(data):
            raise ValueError(f'merc-ctrl at {tag:#x}: effect count {count}')
        cut.update(at for at in pointers if first <= at < end)
        data[first:end] = bytes(end - first)
        struct.pack_into('<I', data, tag + MERC_HEADER + EFFECT_COUNT, 0)
    sites = [at for at in pointers if at not in cut]
    # Pieces start at 16-aligned tags and pointer targets (a basic's target is its tag + 4).
    starts = {0}
    for at in tags:
        if at % 16 == 0:
            starts.add(at)
    for at in pointers:
        target = u32(obj, 16 + at) if at in cut else u32(data, at)
        if target - 4 in tags:
            target -= 4
        if target % 16 == 0 and target < len(data):
            starts.add(target)
    starts = sorted(starts)
    ends = starts[1:] + [len(data)]

    def piece(offset):
        return bisect.bisect_right(starts, offset) - 1

    edges = {}
    for at in sites:
        edges.setdefault(piece(at), set()).add(piece(u32(data, at)))
    keep, stack = set(), [0]
    while stack:
        p = stack.pop()
        if p in keep:
            continue
        keep.add(p)
        stack.extend(edges.get(p, ()))
    remap, parts, out_at = {}, [], 0
    for p in sorted(keep):
        remap[p] = out_at - starts[p]
        parts.append(bytes(data[starts[p]:ends[p]]))
        out_at += ends[p] - starts[p]
    new = bytearray(b''.join(parts))
    new_pointers = []
    for at in sorted(sites):
        p = piece(at)
        if p not in keep:
            continue
        target = u32(data, at)
        struct.pack_into('<I', new, at + remap[p], target + remap[piece(target)])
        new_pointers.append(at + remap[p])
    new_bindings = []
    for name, methods, where in bindings:
        kept = [s + remap[piece(s)] for s in where if piece(s) in keep]
        if kept:
            new_bindings.append((name, methods, kept))
    table = encode_table(new_pointers, new_bindings, len(new))
    out = struct.pack('<4I', 0xffffffff, len(table), 4, len(new)) + bytes(new) + table
    check_data, check_pointers, check_bindings = decode_v4(out, 'art-group')
    if sorted(check_pointers) != new_pointers or bytes(check_data) != bytes(new):
        raise ValueError('pointer table round trip differs')
    if sorted((n, -1 if m is None else m, tuple(sorted(s))) for n, m, s in check_bindings) != \
       sorted((n, -1 if m is None else m, tuple(sorted(s))) for n, m, s in new_bindings):
        raise ValueError('binding table round trip differs')
    return out, dict(mercs=len(mercs), before=len(data), after=len(new))

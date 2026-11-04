"""Build a logic-only BSP from a retail level DGO (AI-assisted), ISC.

Keeps the level's bsp-header with collision, actors, ambients, nav and vis
info; drops the drawable trees only the PS2 renderers read (tfrag, lowres
tfrag, instance-tie, instance-shrub) and every object reachable only from
them. The result is a valid retail-format v4 object: its pointer and
binding tables are rebuilt and checked by decoding it again. The Dreamcast
draws the level from converted render packages instead.

Known gap: Jak 1 tie instances can carry collision through their prototypes'
collide-frag meshes (11,744 bytes on Geyser Rock). Dropping the tie trees
drops that collision too.

The output stays under build/ (git-ignored); it is derived from user data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from bsp_graph import BspGraph, GROUP_LENGTH
from stage_jak_art import archive_objects, decode_v4, header, u32

PORT = Path(__file__).resolve().parent
RENDER_TREES = {'drawable-tree-tfrag', 'drawable-tree-lowres-tfrag', 'drawable-tree-trans-tfrag',
                'drawable-tree-dirt-tfrag', 'drawable-tree-ice-tfrag',
                'drawable-tree-instance-tie', 'drawable-tree-instance-shrub'}


def encode_pointers(sites, size):
    """Inverse of the run-length pointer section decode_v4 reads."""
    out = bytearray()
    if not sites:
        return bytearray([0])
    words = sorted(at // 4 for at in sites)
    runs, at, i = [], 0, 0
    while i < len(words):
        skip = words[i] - at
        j = i
        while j + 1 < len(words) and words[j + 1] == words[j] + 1:
            j += 1
        runs += [skip, j - i + 1]
        at = words[j] + 1
        i = j + 1
    if runs[0] == 0:
        raise ValueError('a pointer at offset 0 cannot be encoded')
    for run in runs:
        while run >= 255:
            out.append(255)
            run -= 255
        out.append(run)
    # After 255 the decoder reads a 0 as a mode toggle, not as the end, so a
    # last run that is an exact multiple of 255 needs one more empty run.
    if len(out) >= 2 and out[-2] == 255 and out[-1] == 0:
        out.append(0)
    out.append(0)
    return out


def encode_delta(delta):
    if delta % 4:
        raise ValueError('unaligned binding')
    if delta < 256:
        return bytes([delta])
    if delta < 1 << 16:
        return struct.pack('<H', delta | 1)
    if delta < 1 << 24:
        return struct.pack('<I', delta | 2)[:3]
    return struct.pack('<I', delta | 3)


def encode_table(pointers, bindings, size):
    body = bytearray(encode_pointers(pointers, size))
    for name, methods, sites in bindings:
        if not sites:
            continue
        if methods is not None:
            body.append(128 | methods)
        elif ord(name[0]) <= 9:
            raise ValueError('symbol name cannot start a binding')
        body += name.encode('ascii') + b'\0'
        at = 0
        for site in sorted(sites):
            body += encode_delta(site - at)
            at = site
        body.append(0)
    body.append(0)
    table = bytearray(struct.pack('<3I', 0xffffffff, 0, 2)) + body
    table += bytes((-len(table)) % 16)
    struct.pack_into('<I', table, 4, len(table))
    return bytes(table)


def build(obj):
    g = BspGraph(obj)
    data = bytearray(g.data)
    array, trees = g.trees()
    kept_trees = [(slot, at, t) for slot, at, t in trees if t not in RENDER_TREES]
    dropped = [t for _, _, t in trees if t in RENDER_TREES]
    blocked = {g.region(at) for _, at, t in trees if t in RENDER_TREES}
    keep = g.reach([0], blocked)
    # The array keeps its kept trees first, then zeroed slots that are no longer pointers.
    old_slots = [slot for slot, _, _ in trees]
    values = [u32(g.data, slot) for slot, _, _ in kept_trees]
    struct.pack_into('<h', data, array + GROUP_LENGTH, len(values))
    for i, slot in enumerate(old_slots):
        struct.pack_into('<I', data, slot, values[i] if i < len(values) else 0)
    pointer_sites = set(g.pointers) - set(old_slots[len(values):])
    # Compact: kept regions in order; offsets keep their 16-byte phase.
    remap, pieces, out_at = {}, [], 0
    for r in sorted(keep):
        start, end = g.starts[r], g.ends[r]
        if start % 16 or (end - start) % 16:
            raise ValueError('object extent is not 16-byte aligned')
        remap[r] = out_at - start
        pieces.append(bytes(data[start:end]))
        out_at += end - start
    new_data = bytearray(b''.join(pieces))

    def moved(offset):
        r = g.region(offset)
        if r not in remap:
            raise KeyError(offset)
        return offset + remap[r]

    new_pointers = []
    for site in sorted(pointer_sites):
        if g.region(site) not in remap:
            continue
        target = u32(data, site)
        try:
            new_target = moved(target)
        except KeyError:
            raise ValueError(f'kept pointer at {site:#x} reaches dropped data {target:#x}')
        new_site = moved(site)
        struct.pack_into('<I', new_data, new_site, new_target)
        new_pointers.append(new_site)
    new_bindings = []
    for name, methods, sites in g.bindings:
        kept = [moved(s) for s in sites if g.region(s) in remap]
        if kept:
            new_bindings.append((name, methods, kept))
    table = encode_table(new_pointers, new_bindings, len(new_data))
    out = struct.pack('<4I', 0xffffffff, len(table), 4, len(new_data)) + bytes(new_data) + table
    # Validate: decodes, same pointer values and bindings, every pointer inside.
    check_data, check_pointers, check_bindings = decode_v4(out, 'bsp-header')
    if sorted(check_pointers) != sorted(new_pointers):
        raise ValueError('pointer table round trip differs')
    if sorted((n, m, tuple(sorted(s))) for n, m, s in check_bindings) != \
       sorted((n, m, tuple(sorted(s))) for n, m, s in new_bindings):
        raise ValueError('binding table round trip differs')
    return out, dict(source_bytes=len(g.data), logic_bytes=len(new_data), objects=len(keep),
                     regions=len(g.starts), kept_trees=[t for _, _, t in kept_trees], dropped_trees=dropped,
                     pointers=len(new_pointers), bindings=sum(len(s) for _, _, s in new_bindings))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dgo', type=Path, help="owner's extracted level DGO, e.g. TRA.DGO")
    parser.add_argument('--output', type=Path, default=PORT / 'build/logic-bsp')
    args = parser.parse_args()
    out_dir = args.output.resolve()
    if not out_dir.is_relative_to((PORT / 'build').resolve()):
        raise SystemExit('retail-derived output must be under ports/dreamcast/build/')
    bsps = [(name, obj) for _, name, obj in archive_objects(args.dgo)
            if u32(obj, 8) == 4 and (name.endswith('-vis') or name.endswith('-bsp'))]
    if len(bsps) != 1:
        raise SystemExit(f'expected one BSP object, found {len(bsps)}')
    name, obj = bsps[0]
    out, report = build(obj)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.dgo.stem.upper()
    (out_dir / f'{stem}-LOGIC.DGO').write_bytes(header(1, f'{stem}-LOGIC.DGO') + header(len(out), name) +
                                                 out + bytes((-len(out)) % 16))
    report.update(name=name, ai_assisted=True, source_sha256=hashlib.sha256(obj).hexdigest(),
                  output_sha256=hashlib.sha256(out).hexdigest())
    (out_dir / f'{stem}-LOGIC.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

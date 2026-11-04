"""Stage complete retail player art, without game code/textures (AI-assisted), ISC.

The output stays under build/. No model/animation is synthesized or trimmed.
Reads the v4 tables like game/kernel/jak1/klink.cpp and common/klink.cpp.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PORT = Path(__file__).resolve().parent
WANTED = ('eichar', 'sidekick')


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def archive_objects(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:4] == b'oZlB':
        raise ValueError('expected extracted uncompressed DGO')
    at = 64
    for index in range(u32(data, 0)):
        size = u32(data, at)
        name = data[at + 4:at + 64].split(b'\0', 1)[0].decode('ascii')
        start = at + 64
        if start + size > len(data):
            raise ValueError('short object')
        yield index, name, data[start:start + size]
        at = start + ((size + 15) & ~15)
    if at != len(data):
        raise ValueError('archive tail')


def decode_v4(obj):
    """Return data, pointer offsets and (name, method count, offsets) bindings.

    The variable-length delta low bits are length flags, not address bits.
    Only absolute data relocations are accepted; no MIPS instruction patching.
    """
    tag, length, version, size = struct.unpack_from('<4I', obj)
    link = 16 + size
    if tag != 0xffffffff or version != 4 or size % 16 or size < 32:
        raise ValueError('not aligned retail v4 data')
    if length < 14 or link + length > len(obj) or any(obj[link + length:]):
        raise ValueError('v4 length/padding')
    if struct.unpack_from('<3I', obj, link) != (0xffffffff, length, 2):
        raise ValueError('v2 link header')
    data = obj[16:link]
    table = obj[link:link + length]
    p, at, fixing, pointers, bindings = 12, 0, False, [], []
    if table[p] == 0:
        p += 1
    else:
        while True:
            while True:
                count = table[p]
                p += 1
                if fixing:
                    pointers.extend(range(at, at + count * 4, 4))
                at += count * 4
                if at > size:
                    raise ValueError('pointer run outside data')
                if count != 255:
                    break
                if table[p] == 0:
                    p += 1
                    fixing = not fixing
            fixing = not fixing
            if table[p] == 0:
                p += 1
                break
    while table[p]:
        kind = table[p]
        p += 1
        methods = (kind & 127) if kind & 128 else None
        if methods is None:
            if kind <= 9:
                raise ValueError('unsupported symbol link')
            p -= 1
        end = table.index(0, p)
        name = table[p:end].decode('ascii')
        p, at, offsets = end + 1, 0, []
        while True:
            seek, step = table[p], 1
            if seek & 3:
                seek |= table[p + 1] << 8
                step = 2
                if seek & 2:
                    seek |= table[p + 2] << 16
                    step = 3
                    if seek & 1:
                        seek |= table[p + 3] << 24
                        step = 4
            p += step
            at += seek & 0xfffffffc
            if at > size - 4 or u32(data, at) != 0xffffffff:
                raise ValueError('non-absolute or out-of-range binding')
            offsets.append(at)
            if table[p] == 0:
                p += 1
                break
        bindings.append((name, methods, offsets))
    p += 1
    if any(table[p:]):
        raise ValueError('link tail')
    for at in pointers:
        if u32(data, at) >= size:
            raise ValueError('pointer outside object')
    all_sites = pointers + [at for _, _, offsets in bindings for at in offsets]
    if len(set(all_sites)) != len(all_sites):
        raise ValueError('overlapping relocations')
    if not any(n == 'art-group' and 0 in offsets and m is not None for n, m, offsets in bindings):
        raise ValueError('root is not art-group')
    return data, pointers, bindings


def header(size, name):
    encoded = name.encode('ascii')
    if not encoded or len(encoded) >= 60:
        raise ValueError('DGO name')
    return struct.pack('<I', size) + encoded.ljust(60, b'\0')


def stage(source, out):
    out = out.resolve()
    if not out.is_relative_to((PORT / 'build').resolve()):
        raise ValueError('retail data output must be under ports/dreamcast/build/')
    selected = {}
    for index, name, obj in archive_objects(source):
        # GAME.CGO also contains a v3 code object called sidekick. Select DATA.
        if name in WANTED and u32(obj, 8) == 4:
            if name in selected:
                raise ValueError('ambiguous duplicate v4 art')
            selected[name] = (index, obj)
    if set(selected) != set(WANTED):
        raise ValueError('missing complete player art')
    rows, archive = [], bytearray(header(2, 'JAKART.DGO'))
    for name in sorted(WANTED, key=lambda n: selected[n][0]):
        index, obj = selected[name]
        data, pointers, bindings = decode_v4(obj)
        name_at = u32(data, 8)
        n = u32(data, name_at)
        if data[name_at + 4:name_at + 4 + n] != name.encode():
            raise ValueError('art-group name differs from DGO name')
        archive += header(len(obj), name) + obj + bytes((-len(obj)) % 16)
        rows.append(dict(name=name, source_index=index, file_bytes=len(obj),
                         resident_bytes=len(data), link_bytes=u32(obj, 4),
                         sha256=hashlib.sha256(obj).hexdigest(), pointer_relocations=len(pointers),
                         bindings=[dict(name=n, methods=m, count=len(o)) for n, m, o in bindings]))
    out.mkdir(parents=True, exist_ok=True)
    (out / 'JAKART.DGO').write_bytes(archive)
    # Optional one-object admission preserves every eichar byte, relocation,
    # animation and index. The existing two-object archive remains identical.
    eichar = selected['eichar'][1]
    (out / 'EICHAR.DGO').write_bytes(header(1, 'EICHAR.DGO') +
        header(len(eichar), 'eichar') + eichar + bytes((-len(eichar)) % 16))
    total = sum(r['resident_bytes'] for r in rows)
    manifest = dict(ai_assisted=True, license='ISC (tools only; retail data is user owned)',
                    source=str(source.resolve()), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    archive_sha256=hashlib.sha256(archive).hexdigest(), archive_bytes=len(archive),
                    resident_bytes=total, objects=rows)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # Metadata only. Never embed the 2 MB disc payload in the executable/boot image.
    sizes = {r['name']: r['resident_bytes'] for r in rows}
    (out / 'jak_art_sizes.h').write_text(
        '/* Generated player art sizes (AI-assisted), ISC; no retail bytes. */\n'
        f'#define DC_JAK_EICHAR_BYTES {sizes["eichar"]}u\n'
        f'#define DC_JAK_SIDEKICK_BYTES {sizes["sidekick"]}u\n'
        f'#define DC_JAK_ART_BYTES {total}u\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help="owner's extracted GAME.CGO (or ART.CGO)")
    parser.add_argument('--output', type=Path, default=PORT / 'build/jak-art-disc')
    args = parser.parse_args()
    result = stage(args.source, args.output)
    for row in result['objects']:
        print(f'{row["name"]}: file={row["file_bytes"]} resident={row["resident_bytes"]} '
              f'pointers={row["pointer_relocations"]}')
    print(f'JAKART.DGO={result["archive_bytes"]}; resident total={result["resident_bytes"]}')


if __name__ == '__main__':
    main()

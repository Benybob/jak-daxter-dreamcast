"""A level GLB cut down to its tfrag (terrain) roots (AI-assisted), ISC.

    python tfrag_glb.py LEVEL-background.glb OUT.glb [--roots normal,lowres] [--crop X0,X1,Z0,Z1]

The decompiler's background GLB has one scene root per draw tree:
`<level>-tfrag-{normal,trans,lowres,dirt}`, `<level>-tie-N` and `<level>-shrub-N`.
TIE and shrub are drawn instanced (instance_pass.c, from a DCI package), so the
terrain the sector builder (prepare_render_sectors.py) needs is the tfrag roots
alone. The default keeps the opaque ones: `trans` and `dirt` are BLEND materials
(village1: the oracle's eyes and the beach's blended sand), which the base pass
has no translucent list for yet. Only the JSON chunk's scene changes; the binary
chunk is copied as it is, so unreferenced meshes stay in the file unused.

--crop keeps only the triangles whose XZ bounds overlap a rectangle (meters):
new index arrays are appended to the binary chunk and the primitives point at
them. A diagnostic stages one terrain window this way; cropping to every 16 m
cell the window can touch leaves the window's faces exactly as the whole
level's package gives them. tfrag nodes carry no transforms (checked).
"""
import argparse
import json
from pathlib import Path
import struct


def read_chunks(blob):
    magic, version, length = struct.unpack_from('<4sII', blob, 0)
    if magic != b'glTF' or version != 2 or length != len(blob):
        raise ValueError('expected a complete glTF 2 binary (.glb)')
    chunks, at = [], 12
    while at < length:
        size, kind = struct.unpack_from('<I4s', blob, at)
        chunks.append((kind, blob[at + 8:at + 8 + size]))
        at += 8 + size
    if not chunks or chunks[0][0] != b'JSON':
        raise ValueError('GLB without a leading JSON chunk')
    return chunks


COMPONENTS = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4)}


def view_bytes(doc, binary, accessor, item):
    """(bytes, offset, stride) of an accessor's elements in the binary chunk."""
    view = doc['bufferViews'][accessor['bufferView']]
    start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    return binary, start, view.get('byteStride', item)


def crop(doc, binary, kept, rect):
    """Drop the triangles of the kept roots outside rect; returns the new binary."""
    x0, x1, z0, z1 = rect
    out = bytearray(binary)
    nodes, acc = doc['nodes'], doc['accessors']
    stack, seen = list(kept), set()
    while stack:
        n = nodes[stack.pop()]
        if any(k in n for k in ('matrix', 'translation', 'rotation', 'scale')):
            raise ValueError('tfrag node with a transform')
        stack += n.get('children', [])
        if 'mesh' not in n or n['mesh'] in seen:
            continue
        seen.add(n['mesh'])
        keep_prims = []
        for prim in doc['meshes'][n['mesh']]['primitives']:
            pa = acc[prim['attributes']['POSITION']]
            data, at, stride = view_bytes(doc, binary, pa, 12)
            xs = [struct.unpack_from('<3f', data, at + stride * i) for i in range(pa['count'])]
            ia = acc[prim['indices']]
            code, size = COMPONENTS[ia['componentType']]
            data, at, stride = view_bytes(doc, binary, ia, size)
            idx = [struct.unpack_from('<' + code, data, at + stride * i)[0] for i in range(ia['count'])]
            tris = []
            for k in range(0, len(idx) - 2, 3):
                pts = [xs[i] for i in idx[k:k + 3]]
                if min(p[0] for p in pts) <= x1 and max(p[0] for p in pts) >= x0 and \
                        min(p[2] for p in pts) <= z1 and max(p[2] for p in pts) >= z0:
                    tris += idx[k:k + 3]
            if not tris:
                continue
            out += b'\0' * (-len(out) % 4)
            doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(out), 'byteLength': 4 * len(tris)})
            out += struct.pack(f'<{len(tris)}I', *tris)
            acc.append({'bufferView': len(doc['bufferViews']) - 1, 'componentType': 5125, 'count': len(tris),
                        'type': 'SCALAR'})
            prim = dict(prim, indices=len(acc) - 1)
            keep_prims.append(prim)
        if keep_prims:
            doc['meshes'][n['mesh']]['primitives'] = keep_prims
        else:
            del n['mesh']
    out += b'\0' * (-len(out) % 4)
    doc['buffers'][0]['byteLength'] = len(out)
    return bytes(out)


def cut(blob, roots=('normal', 'lowres'), rect=None):
    """(new GLB bytes, {root name: kept}) keeping the tfrag roots named by suffix,
    cropped to rect (x0, x1, z0, z1) when given."""
    chunks = read_chunks(blob)
    doc = json.loads(chunks[0][1])
    scene = doc['scenes'][doc.get('scene', 0)]
    wanted = {f'-tfrag-{r}' for r in roots}
    kept, report = [], {}
    for i in scene['nodes']:
        name = doc['nodes'][i].get('name', '')
        keep = any(name.endswith(w) for w in wanted)
        report[name] = keep
        if keep:
            kept.append(i)
    if len(kept) != len(wanted):
        raise ValueError(f'expected roots {sorted(wanted)}, found {sorted(n for n, k in report.items() if k)}')
    scene['nodes'] = kept
    if rect is not None:
        if len(chunks) < 2 or chunks[1][0] != b'BIN\0':
            raise ValueError('GLB without a binary chunk')
        chunks[1] = (b'BIN\0', crop(doc, chunks[1][1], kept, rect))
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    body = struct.pack('<I4s', len(text), b'JSON') + text
    for kind, data in chunks[1:]:
        body += struct.pack('<I4s', len(data), kind) + data
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('glb', type=Path)
    ap.add_argument('out', type=Path)
    ap.add_argument('--roots', default='normal,lowres')
    ap.add_argument('--crop', help='X0,X1,Z0,Z1 in meters')
    a = ap.parse_args()
    rect = tuple(float(v) for v in a.crop.split(',')) if a.crop else None
    blob, report = cut(a.glb.read_bytes(), tuple(a.roots.split(',')), rect)
    a.out.write_bytes(blob)
    print(json.dumps({'kept': [n for n, k in report.items() if k], 'dropped': [n for n, k in report.items() if not k],
                      'bytes': len(blob)}, indent=1))


if __name__ == '__main__':
    main()

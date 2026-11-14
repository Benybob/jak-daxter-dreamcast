"""Pack sprite (particle) textures for the level ELF (AI-assisted), ISC.

    python make_sprite_textures.py GAME.CGO TEXTURE_PNGS --out build/level-disc/GEYSER/SPRITES.DSK [--page 2]

The particle system names its textures by texture-id (page, index). This reads
the retail texture-page objects in GAME.CGO (and any further DGOs given with
--dgo) for the names in each page, takes each named texture's PNG from the
decompiler's texture export (save_texture_pngs, TEXTURE_PNGS/<page name>/<name>.png)
and writes them, in the pages asked for, as 16-bit ARGB4444 twiddled PVR
textures (power-of-two sides, 8 to 128).

SPR1 file: 'SPR1', u32 count, then count 16-byte entries
  u16 page, u16 index, u16 width, u16 height, u32 offset, u32 bytes
then the texel data (offsets from the file start, 32-byte aligned).

The output is derived from the owner's disc and stays under build/.
"""
import argparse
import json
from pathlib import Path
import struct

from stage_jak_art import archive_objects, decode_v4, u32

PORT = Path(__file__).resolve().parent


def page_names(sources):
    """{page id: (page name, [texture name or None by index])} from texture-page objects."""
    out = {}
    for src in sources:
        for _, name, obj in archive_objects(src):
            if not name.startswith('tpage') or u32(obj, 8) != 4:
                continue
            data = decode_v4(obj, 'texture-page')[0]
            ok = lambda p: 0 < p < len(data) - 4  # noqa: E731
            at = 4  # the root basic's address: its tag is at 0
            pname_at, pid, length = u32(data, at + 4), u32(data, at + 8), u32(data, at + 12)
            pname = data[pname_at + 4:pname_at + 4 + u32(data, pname_at)].decode()
            names = []
            for i in range(length):
                tex = u32(data, at + 124 + 4 * i)
                s = u32(data, tex + 36) if ok(tex) else 0
                names.append(data[s + 4:s + 4 + u32(data, s)].decode() if ok(s) else None)
            out[pid] = (pname, names)
    return out


def twiddle(x, y, w, h):
    """PVR twiddled index: square m by m blocks (m the shorter side) along the
    longer side, each block's x and y bits interleaved, y in the low bit."""
    m = min(w, h)
    r = 0
    for b in range(m.bit_length() - 1):
        r |= ((y >> b) & 1) << (2 * b) | ((x >> b) & 1) << (2 * b + 1)
    block = x // m if w > h else y // m
    return block * m * m + r


def argb4444(image):
    from PIL import Image
    w, h = image.size
    side = lambda n: max(8, min(128, 1 << max(0, (n - 1).bit_length())))  # noqa: E731
    tw, th = side(w), side(h)
    if (tw, th) != (w, h):
        image = image.resize((tw, th), Image.LANCZOS)
    px = image.convert('RGBA').load()
    out = bytearray(tw * th * 2)
    for y in range(th):
        for x in range(tw):
            r, g, b, a = px[x, y]
            v = (a >> 4) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
            struct.pack_into('<H', out, 2 * twiddle(x, y, tw, th), v)
    return tw, th, bytes(out)


def main():
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game_cgo', type=Path)
    ap.add_argument('pngs', type=Path, help='the decompiler texture export (jak1/textures)')
    ap.add_argument('--dgo', type=Path, action='append', default=[])
    ap.add_argument('--page', type=int, action='append', help='texture page ids to pack (default 2, effects)')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    if not a.out.resolve().is_relative_to((PORT / 'build').resolve()):
        raise SystemExit('retail-derived output must be under ports/dreamcast/build/')
    pages = page_names([a.game_cgo] + a.dgo)
    entries, blobs = [], []
    for pid in a.page or [2]:
        pname, names = pages[pid]
        for index, name in enumerate(names):
            png = a.pngs / pname / f'{name}.png' if name else None
            if not png or not png.exists():
                continue
            w, h, data = argb4444(Image.open(png))
            entries.append((pid, index, w, h, name))
            blobs.append(data)
    at = (8 + 16 * len(entries) + 31) & ~31
    table, body = bytearray(), bytearray()
    for (pid, index, w, h, _), data in zip(entries, blobs):
        offset = at + len(body)
        table += struct.pack('<4H2I', pid, index, w, h, offset, len(data))
        body += data + bytes((-len(data)) % 32)
    head = b'SPR1' + struct.pack('<I', len(entries)) + table
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(head + bytes(at - len(head)) + body)
    print(json.dumps(dict(ai_assisted=True, textures=len(entries), bytes=at + len(body),
                          names={f'{p}:{i}': n for p, i, _, _, n in entries}), indent=1))


if __name__ == '__main__':
    main()

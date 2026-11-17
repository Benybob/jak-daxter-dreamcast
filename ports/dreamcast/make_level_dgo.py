"""Pack a level's disc DGO: its code, then its art and logic BSP (AI-assisted), ISC.

A retail level DGO holds the level's code objects, then its art groups and
texture pages, then its BSP. The Dreamcast one has the same order:

  1. the level's code as native DCSH modules (build_level_code.py), which the
     DGO server links in the level heap and starts (level_dgo_rpc.c);
  2. its art groups without merc geometry and its logic BSP
     (build_logic_bsp.py --art --strip-merc), which original level loading
     links and logs in.

With --art-out the art groups go to their own files instead, one each, which
level_logic.c loads when an actor first asks for one (art on demand).

The file is named as original load-begin asks for it, e.g. TRAINING.DGO for
TRA. Everything read and written is under build/ (git-ignored): the code is
built from source, the data from the owner's disc.

  python build_level_code.py TRA --with villagep-obs
  python build_logic_bsp.py --art --strip-merc <iso>/DGO/TRA.DGO
  python make_level_dgo.py TRA TRAINING
"""
import argparse
import json
import re
from pathlib import Path
import struct

PORT = Path(__file__).resolve().parent
HEADER = struct.Struct('<I60s')


def objects(path):
    """(name, payload) of each object of a DGO; payloads without their padding."""
    data = path.read_bytes()
    count, _ = HEADER.unpack_from(data, 0)
    at, out = HEADER.size, []
    for _ in range(count):
        size, raw = HEADER.unpack_from(data, at)
        at += HEADER.size
        if at + size > len(data):
            raise SystemExit(f'{path.name}: object runs past the file')
        out.append((raw.split(b'\0', 1)[0].decode('ascii'), data[at:at + size]))
        at += (size + 15) & ~15
    if at != len(data):
        raise SystemExit(f'{path.name}: {len(data) - at} bytes after its last object')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('level', help='level DGO name without .DGO, e.g. TRA')
    parser.add_argument('name', help='the name original load-begin asks for, without .DGO, e.g. TRAINING')
    parser.add_argument('--code', type=Path, help='default build/level-code/<LEVEL>-CODE.DGO')
    parser.add_argument('--data', type=Path, help='default build/logic-bsp/<LEVEL>-LEVEL.DGO')
    parser.add_argument('--output', type=Path, default=PORT / 'build/level-disc')
    parser.add_argument('--art-out', type=Path,
                        help='write each art group as <DIR>/<NAME>.DGO, loaded on demand, not in the level DGO')
    args = parser.parse_args()
    level = args.level.upper()
    code = objects(args.code or PORT / f'build/level-code/{level}-CODE.DGO')
    data = objects(args.data or PORT / f'build/logic-bsp/{level}-LEVEL.DGO')
    if any(payload[:4] != b'DCSH' for _, payload in code):
        raise SystemExit('a code object is not a DCSH module')
    if any(payload[:4] != b'\xff\xff\xff\xff' or len(payload) % 16 for _, payload in data):
        raise SystemExit('a data object is not aligned retail v4 data')
    name = f'{args.name.upper()}.DGO'
    art = []
    if args.art_out:
        # art on demand (level_logic.c): every object but the BSP (the last) as a
        # one-object DGO named after its art group, which original
        # load-to-heap-by-name asks for when an actor's skeleton is set up
        art_dir = args.art_out.resolve()
        if not art_dir.is_relative_to((PORT / 'build').resolve()):
            raise SystemExit('art output must be under ports/dreamcast/build/')
        art_dir.mkdir(parents=True, exist_ok=True)
        for old in art_dir.glob('*.DGO'):
            old.unlink()
        art, data = data[:-1], data[-1:]
        for object_name, payload in art:
            # the disc's names: letters, digits, '-' and '_', at most 29 (level_logic.c maps the same)
            file = re.sub(r'[^A-Z0-9_-]', '_', object_name.upper())[:25] + '.DGO'
            one = HEADER.pack(1, file.encode('ascii').ljust(60, b'\0'))
            one += HEADER.pack(len(payload), object_name.encode('ascii').ljust(60, b'\0')) + payload
            (art_dir / file).write_bytes(one + bytes((-len(payload)) % 16))
    out = bytearray(HEADER.pack(len(code) + len(data), name.encode('ascii').ljust(60, b'\0')))
    for object_name, payload in code + data:
        out += HEADER.pack(len(payload), object_name.encode('ascii').ljust(60, b'\0')) + payload
        out += bytes((-len(payload)) % 16)
    out_dir = args.output.resolve()
    if not out_dir.is_relative_to((PORT / 'build').resolve()):
        raise SystemExit('output must be under ports/dreamcast/build/')
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / name).write_bytes(out)
    print(json.dumps(dict(dgo=str(out_dir / name), bytes=len(out), code=[n for n, _ in code],
                          code_bytes=sum(len(p) for _, p in code), data=[n for n, _ in data],
                          data_bytes=sum(len(p) for _, p in data), art_files=len(art),
                          art_bytes=sum(len(p) for _, p in art), ai_assisted=True), indent=2))


if __name__ == '__main__':
    main()

"""Stage GAME.CGO art groups a level's actors need, one object a DGO (AI-assisted), ISC.

The port's GAME.CGO load runs its code, not its art; the player art comes from
stage_jak_art.py. Skeleton groups with :texture-level 2 (crates, precursor
orbs, fuel cells, scout flies) look their art group up in the default level,
where retail GAME.CGO's art lands at boot. level_player.c loads each DGO
written here after EICHAR.DGO and calls the original art-group relocate with
the default level loading, as it does for eichar.

Each output is a one-object DGO whose object is the retail v4 art group,
byte for byte. The output stays under build/ (derived from the owner's disc).
"""
import argparse
import json
from pathlib import Path

from stage_jak_art import archive_objects, decode_v4, header, u32

PORT = Path(__file__).resolve().parent
# art group -> disc name (8.3)
WANTED = {'crate': 'CRATE.DGO', 'money': 'MONEY.DGO', 'fuel-cell': 'FUELCELL.DGO', 'buzzer': 'BUZZER.DGO'}


def stage(source, out, wanted=WANTED):
    found = {n: o for _, n, o in archive_objects(source) if n in wanted and u32(o, 8) == 4}
    missing = sorted(set(wanted) - set(found))
    if missing:
        raise SystemExit(f'not in {source.name}: {missing}')
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, disc in wanted.items():
        obj = found[name]
        data = decode_v4(obj)[0]
        at = u32(data, 8)
        if data[at + 4:at + 4 + u32(data, at)] != name.encode():
            raise ValueError(f'{name}: art-group name differs')
        (out / disc).write_bytes(header(1, disc) + header(len(obj), name) + obj + bytes((-len(obj)) % 16))
        rows.append(dict(name=name, disc=disc, file_bytes=len(obj), resident_bytes=len(data)))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source', type=Path, help="owner's extracted GAME.CGO")
    ap.add_argument('--output', type=Path, default=PORT / 'build/level-disc')
    a = ap.parse_args()
    if not a.output.resolve().is_relative_to((PORT / 'build').resolve()):
        raise SystemExit('retail-derived output must be under ports/dreamcast/build/')
    rows = stage(a.source, a.output)
    print(json.dumps(dict(ai_assisted=True, objects=rows, resident_bytes=sum(r['resident_bytes'] for r in rows)), indent=1))


if __name__ == '__main__':
    main()
